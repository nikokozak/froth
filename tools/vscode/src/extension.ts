import * as vscode from "vscode";
import {
  DaemonClient,
  DaemonClientError,
  DaemonNotification,
  ConsoleEvent,
  ConnectedEvent,
  EvalResult,
  InfoResult,
  StatusResult,
} from "./daemon-client";

const RECONNECT_INTERVAL_MS = 3000;

type ConnectionState = "connected" | "disconnected" | "no-daemon";

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel("Froth Console");
  const statusItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    50,
  );

  const controller = new FrothController(output, statusItem);

  // Sidebar tree view
  const sidebarProvider = new FrothSidebarProvider(controller);
  const treeView = vscode.window.createTreeView("frothDeviceView", {
    treeDataProvider: sidebarProvider,
  });

  context.subscriptions.push(
    output,
    statusItem,
    treeView,
    { dispose: () => controller.dispose() },
    vscode.commands.registerCommand("froth.sendSelection", () =>
      controller.sendSelection(),
    ),
    vscode.commands.registerCommand("froth.sendFile", () =>
      controller.sendFile(),
    ),
    vscode.commands.registerCommand("froth.interrupt", () =>
      controller.interrupt(),
    ),
    vscode.commands.registerCommand("froth.reset", () =>
      controller.reset(),
    ),
    vscode.commands.registerCommand("froth.save", () =>
      controller.evalCommand("save"),
    ),
    vscode.commands.registerCommand("froth.wipe", () =>
      controller.evalCommand("wipe"),
    ),
    vscode.commands.registerCommand("froth.refreshSidebar", () =>
      controller.refreshDeviceInfo(),
    ),
  );

  // Refresh sidebar when controller state changes
  controller.onStateChange(() => sidebarProvider.refresh());

  statusItem.show();
  controller.start();
}

export function deactivate(): void {}

// --- Controller ---

type StateChangeListener = () => void;

class FrothController {
  private client: DaemonClient | null = null;
  private state: ConnectionState = "no-daemon";
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private connecting = false;
  private disposed = false;
  private deviceStatus: StatusResult | null = null;
  private liveInfo: InfoResult | null = null;
  private stateListeners: StateChangeListener[] = [];

  constructor(
    private readonly output: vscode.OutputChannel,
    private readonly statusItem: vscode.StatusBarItem,
  ) {
    this.updateStatusBar();
  }

  onStateChange(listener: StateChangeListener): void {
    this.stateListeners.push(listener);
  }

  private notifyStateChange(): void {
    for (const l of this.stateListeners) {
      l();
    }
  }

  getState(): ConnectionState {
    return this.state;
  }

  getDeviceStatus(): StatusResult | null {
    return this.deviceStatus;
  }

  getLiveInfo(): InfoResult | null {
    return this.liveInfo;
  }

  start(): void {
    this.tryConnect();
  }

  dispose(): void {
    this.disposed = true;
    this.clearReconnectTimer();
    if (this.client) {
      this.client.dispose();
      this.client = null;
    }
  }

  async sendSelection(): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showWarningMessage("No active editor");
      return;
    }

    let text: string;
    if (editor.selection.isEmpty) {
      text = editor.document.lineAt(editor.selection.active.line).text;
    } else {
      text = editor.document.getText(editor.selection);
    }

    if (text.trim().length === 0) {
      return;
    }

    await this.evalAndLog(text);
  }

  async sendFile(): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showWarningMessage("No active editor");
      return;
    }

    const text = editor.document.getText();
    if (text.trim().length === 0) {
      return;
    }

    if (!this.client) {
      vscode.window.showWarningMessage("Not connected to Froth daemon");
      return;
    }

    // ADR-037: Send File = reset + eval whole file
    this.output.appendLine("[froth] reset");
    try {
      await this.client.reset();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Reset failed: ${msg}`);
      return;
    }

    this.output.appendLine(`[froth] evaluating ${editor.document.fileName}`);
    await this.evalAndLog(text);
    await this.refreshDeviceInfo();
  }

  async interrupt(): Promise<void> {
    if (!this.client) {
      vscode.window.showWarningMessage("Not connected to Froth daemon");
      return;
    }

    try {
      await this.client.interrupt();
      this.output.appendLine("[froth] interrupt sent");
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Interrupt failed: ${msg}`);
    }
  }

  async reset(): Promise<void> {
    if (!this.client) {
      vscode.window.showWarningMessage("Not connected to Froth daemon");
      return;
    }

    try {
      await this.client.reset();
      this.output.appendLine("[froth] reset");
      await this.refreshDeviceInfo();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Reset failed: ${msg}`);
    }
  }

  async evalCommand(cmd: string): Promise<void> {
    if (!this.client) {
      vscode.window.showWarningMessage("Not connected to Froth daemon");
      return;
    }

    try {
      const result = await this.client.eval(cmd);
      // Error code 20 is FROTH_ERROR_RESET, returned by wipe and
      // dangerous-reset. It's a success signal, not a real error.
      if (result.status !== 0 && result.error_code !== 20) {
        this.output.appendLine(
          `[froth] ${cmd}: error(${result.error_code ?? 0})`,
        );
      } else {
        this.output.appendLine(`[froth] ${cmd}: ok`);
      }
      await this.refreshDeviceInfo();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`${cmd} failed: ${msg}`);
    }
  }

  async refreshDeviceInfo(): Promise<void> {
    if (!this.client) {
      return;
    }
    try {
      this.deviceStatus = await this.client.status();
      // Fetch live device metrics (heap, slots) via info(),
      // since status() only returns the cached HELLO snapshot.
      if (this.deviceStatus.connected) {
        const live = await this.client.info();
        this.liveInfo = live;
      } else {
        this.liveInfo = null;
      }
      this.notifyStateChange();
    } catch {
      // ignore
    }
  }

  // --- Connection lifecycle ---

  private async tryConnect(): Promise<void> {
    if (this.disposed || this.connecting) {
      return;
    }
    this.connecting = true;

    const client = new DaemonClient();
    client.onNotification((n: DaemonNotification) =>
      this.handleNotification(n),
    );
    client.onClose(() => this.handleSocketClose());

    try {
      await client.connect();
    } catch {
      client.dispose();
      this.connecting = false;
      this.setState("no-daemon");
      this.scheduleReconnect();
      return;
    }

    this.client = client;
    this.connecting = false;

    try {
      this.deviceStatus = await client.status();
      this.setState(this.deviceStatus.connected ? "connected" : "disconnected");
      if (this.deviceStatus.connected && this.deviceStatus.device) {
        this.output.appendLine(
          `[froth] connected: ${this.deviceStatus.device.board} (${this.deviceStatus.device.version})`,
        );
      }
    } catch {
      this.setState("disconnected");
    }
  }

  private handleNotification(n: DaemonNotification): void {
    switch (n.method) {
      case "console": {
        const params = n.params as ConsoleEvent | undefined;
        if (params) {
          this.output.append(params.text);
        }
        break;
      }
      case "connected": {
        this.setState("connected");
        const params = n.params as ConnectedEvent | undefined;
        if (params) {
          this.output.appendLine(
            `[froth] device connected: ${params.device.board} (${params.device.version})`,
          );
        }
        this.refreshDeviceInfo();
        break;
      }
      case "disconnected":
        this.setState("disconnected");
        this.deviceStatus = null;
        this.liveInfo = null;
        this.output.appendLine("[froth] device disconnected");
        break;
      case "reconnecting":
        this.output.appendLine("[froth] reconnecting...");
        break;
    }
  }

  private handleSocketClose(): void {
    if (this.client) {
      this.client.dispose();
      this.client = null;
    }
    this.deviceStatus = null;
    this.liveInfo = null;
    this.setState("no-daemon");
    this.output.appendLine("[froth] daemon connection lost");
    this.scheduleReconnect();
  }

  private scheduleReconnect(): void {
    if (this.disposed || this.reconnectTimer !== null) {
      return;
    }
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.tryConnect();
    }, RECONNECT_INTERVAL_MS);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  // --- Eval ---

  private async evalAndLog(source: string): Promise<void> {
    if (!this.client) {
      vscode.window.showWarningMessage("Not connected to Froth daemon");
      return;
    }

    const preview =
      source.length > 80 ? source.slice(0, 77) + "..." : source;
    this.output.appendLine(`> ${preview}`);

    let result: EvalResult;
    try {
      result = await this.client.eval(source);
    } catch (err: unknown) {
      if (err instanceof DaemonClientError && err.isNotConnected) {
        vscode.window.showWarningMessage("No device connected");
      } else {
        const msg = err instanceof Error ? err.message : String(err);
        vscode.window.showErrorMessage(`Eval failed: ${msg}`);
      }
      return;
    }

    this.logResult(result);
    this.output.show(true);
    await this.refreshDeviceInfo();
  }

  private logResult(result: EvalResult): void {
    if (result.status === 0) {
      if (result.stack_repr) {
        this.output.appendLine(result.stack_repr);
      }
    } else {
      let msg = `error(${result.error_code ?? 0})`;
      if (result.fault_word) {
        msg += ` in "${result.fault_word}"`;
      }
      this.output.appendLine(msg);
    }
  }

  // --- Status bar ---

  private setState(state: ConnectionState): void {
    this.state = state;
    this.updateStatusBar();
    this.notifyStateChange();
  }

  private updateStatusBar(): void {
    switch (this.state) {
      case "connected":
        this.statusItem.text = "$(plug) Froth: Connected";
        this.statusItem.tooltip = "Connected to Froth device via daemon";
        this.statusItem.backgroundColor = undefined;
        break;
      case "disconnected":
        this.statusItem.text = "$(debug-disconnect) Froth: No Device";
        this.statusItem.tooltip = "Daemon running, no device connected";
        this.statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.warningBackground",
        );
        break;
      case "no-daemon":
        this.statusItem.text = "$(circle-slash) Froth: No Daemon";
        this.statusItem.tooltip =
          "Cannot reach daemon at ~/.froth/daemon.sock";
        this.statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.errorBackground",
        );
        break;
    }
  }
}

// --- Sidebar ---

class FrothSidebarProvider implements vscode.TreeDataProvider<SidebarItem> {
  private _onDidChangeTreeData = new vscode.EventEmitter<
    SidebarItem | undefined
  >();
  readonly onDidChangeTreeData = this._onDidChangeTreeData.event;

  constructor(private readonly controller: FrothController) {}

  refresh(): void {
    this._onDidChangeTreeData.fire(undefined);
  }

  getTreeItem(element: SidebarItem): vscode.TreeItem {
    return element;
  }

  getChildren(element?: SidebarItem): SidebarItem[] {
    if (element) {
      return [];
    }

    const state = this.controller.getState();
    const status = this.controller.getDeviceStatus();
    const items: SidebarItem[] = [];

    if (state === "no-daemon") {
      items.push(
        new SidebarItem("No daemon running", "", "$(circle-slash)"),
      );
      items.push(
        new SidebarItem(
          "Run: froth daemon start",
          "",
          "$(terminal)",
        ),
      );
      return items;
    }

    if (state === "disconnected" || !status?.device) {
      items.push(
        new SidebarItem("No device connected", "", "$(debug-disconnect)"),
      );
      return items;
    }

    const dev = status.device;
    const live = this.controller.getLiveInfo();

    items.push(new SidebarItem(`${dev.board}`, `v${dev.version}`, "$(circuit-board)"));

    // Use live info for heap/slots when available (refreshed after each eval/reset)
    const heapUsed = live ? live.heap_used : dev.heap_used;
    const heapSize = live ? live.heap_size : dev.heap_size;
    const overlayUsed = live ? live.heap_overlay_used : 0;
    const slotCount = live ? live.slot_count : dev.slot_count;
    const overlaySlots = live ? live.slot_overlay_count : 0;

    items.push(
      new SidebarItem(
        `Heap: ${heapUsed} / ${heapSize}`,
        overlayUsed > 0 ? `(${overlayUsed} user)` : "",
        "$(database)",
      ),
    );
    items.push(
      new SidebarItem(
        `Slots: ${slotCount}`,
        overlaySlots > 0 ? `(${overlaySlots} user)` : "",
        "$(symbol-variable)",
      ),
    );
    items.push(
      new SidebarItem(
        `${dev.cell_bits}-bit cells`,
        "",
        "$(symbol-numeric)",
      ),
    );

    // Action buttons as tree items with commands
    items.push(new SidebarItem("", "", ""));
    items.push(actionItem("$(debug-stop) Interrupt", "froth.interrupt"));
    items.push(actionItem("$(refresh) Reset", "froth.reset"));
    items.push(actionItem("$(save) Save", "froth.save"));
    items.push(actionItem("$(trash) Wipe", "froth.wipe"));

    return items;
  }
}

function actionItem(label: string, commandId: string): SidebarItem {
  const item = new SidebarItem(label, "", "");
  item.command = { command: commandId, title: label };
  return item;
}

class SidebarItem extends vscode.TreeItem {
  constructor(label: string, description: string, icon: string) {
    super(label, vscode.TreeItemCollapsibleState.None);
    this.description = description;
    if (icon) {
      this.iconPath = new vscode.ThemeIcon(icon.replace("$(", "").replace(")", ""));
    }
  }
}

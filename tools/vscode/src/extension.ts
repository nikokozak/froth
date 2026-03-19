import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import * as vscode from "vscode";
import { exec } from "child_process";
import { promisify } from "util";
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

const execAsync = promisify(exec);

const SOCKET_PATH = path.join(os.homedir(), ".froth", "daemon.sock");
const RECONNECT_INTERVAL_MS = 3000;
const DAEMON_START_TIMEOUT_MS = 5000;
const DAEMON_START_POLL_MS = 100;

type ConnectionState =
  | "idle"
  | "connected"
  | "running"
  | "disconnected"
  | "no-daemon";

type TargetMode = "serial" | "local";

let activeController: FrothController | null = null;

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel("Froth Console");
  const statusItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    50,
  );
  const interruptItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    49,
  );

  const controller = new FrothController(output, statusItem, interruptItem);
  activeController = controller;

  const sidebarProvider = new FrothSidebarProvider(controller);
  const treeView = vscode.window.createTreeView("frothDeviceView", {
    treeDataProvider: sidebarProvider,
  });

  context.subscriptions.push(
    output,
    statusItem,
    interruptItem,
    treeView,
    { dispose: () => controller.dispose() },
    vscode.commands.registerCommand("froth.connect", () =>
      controller.connectToDevice(),
    ),
    vscode.commands.registerCommand("froth.tryLocal", () =>
      controller.tryLocal(),
    ),
    vscode.commands.registerCommand("froth.doctor", () =>
      controller.runDoctor(),
    ),
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
      controller.refresh(),
    ),
  );

  controller.onStateChange(() => sidebarProvider.refresh());

  statusItem.show();
  controller.start();
}

export function deactivate(): Thenable<void> | undefined {
  const controller = activeController;
  activeController = null;
  if (controller) {
    return controller.deactivate();
  }
  return undefined;
}

type StateChangeListener = () => void;

class FrothController {
  private client: DaemonClient | null = null;
  private state: ConnectionState = "idle";
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private connecting = false;
  private disposed = false;
  private deactivating = false;
  private deviceStatus: StatusResult | null = null;
  private liveInfo: InfoResult | null = null;
  private stateListeners: StateChangeListener[] = [];
  private extensionOwnsDaemon = false;
  private selectedTarget: TargetMode = "serial";

  constructor(
    private readonly output: vscode.OutputChannel,
    private readonly statusItem: vscode.StatusBarItem,
    private readonly interruptItem: vscode.StatusBarItem,
  ) {
    this.statusItem.command = "froth.connect";
    this.interruptItem.command = "froth.interrupt";
    this.updateStatusBar();
  }

  start(): void {
    this.notifyStateChange();
  }

  dispose(): void {
    this.disposed = true;
    this.clearReconnectTimer();
    this.disposeClient();
  }

  async deactivate(): Promise<void> {
    if (this.deactivating) {
      return;
    }
    this.deactivating = true;

    const stopOwnedDaemon = this.extensionOwnsDaemon;
    this.dispose();

    if (stopOwnedDaemon) {
      this.extensionOwnsDaemon = false;
      try {
        await this.execFroth("daemon stop");
      } catch {
        // Best effort only. The daemon may already be gone.
      }
    }
  }

  onStateChange(listener: StateChangeListener): void {
    this.stateListeners.push(listener);
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

  async connectToDevice(): Promise<void> {
    if (!(await this.ensureDaemonMode("serial"))) {
      return;
    }

    await this.refreshDeviceInfo();

    if (this.state === "disconnected") {
      vscode.window.showWarningMessage(
        "Froth daemon is running, but no device is connected.",
      );
    }
  }

  async tryLocal(): Promise<void> {
    if (!(await this.ensureDaemonMode("local"))) {
      return;
    }

    await this.refreshDeviceInfo();
  }

  async runDoctor(): Promise<void> {
    const terminal = vscode.window.createTerminal("Froth Doctor");
    terminal.show(true);
    terminal.sendText("froth doctor");
  }

  async refresh(): Promise<void> {
    if (!(await this.ensureDaemonMode(this.selectedTarget))) {
      return;
    }
    await this.refreshDeviceInfo();
  }

  async sendSelection(): Promise<void> {
    if (!(await this.ensureDaemonMode(this.selectedTarget))) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

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
    if (!(await this.ensureDaemonMode(this.selectedTarget))) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showWarningMessage("No active editor");
      return;
    }

    const text = editor.document.getText();
    if (text.trim().length === 0) {
      return;
    }

    this.setState("running");
    this.output.appendLine("[froth] reset");

    try {
      await this.client!.reset();
    } catch (err: unknown) {
      this.setState(this.deriveIdleState());
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Reset failed: ${msg}`);
      return;
    }

    this.output.appendLine(`[froth] evaluating ${editor.document.fileName}`);
    this.evalAndLog(text).then(() => this.refreshDeviceInfo());
  }

  async interrupt(): Promise<void> {
    const interruptClient = new DaemonClient();
    try {
      await interruptClient.connect();
      await interruptClient.interrupt();
      this.output.appendLine("[froth] interrupt sent");
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Interrupt failed: ${msg}`);
    } finally {
      interruptClient.dispose();
    }
  }

  async reset(): Promise<void> {
    if (!(await this.ensureDaemonMode(this.selectedTarget))) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    try {
      await this.client!.reset();
      this.output.appendLine("[froth] reset");
      await this.refreshDeviceInfo();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Reset failed: ${msg}`);
    }
  }

  async evalCommand(cmd: string): Promise<void> {
    if (!(await this.ensureDaemonMode(this.selectedTarget))) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    try {
      const result = await this.client!.eval(cmd);
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
      const status = this.normalizeStatus(await this.client.status());
      this.applyStatus(status);
      if (status.connected) {
        this.liveInfo = await this.client.info();
      } else {
        this.liveInfo = null;
      }
      this.notifyStateChange();
    } catch {
      // Leave the current UI state intact if the daemon vanishes mid-refresh.
    }
  }

  private async ensureDaemonMode(mode: TargetMode): Promise<boolean> {
    this.selectedTarget = mode;

    let status = await this.connectToDaemon(false);
    if (status && status.target !== mode) {
      if (!this.extensionOwnsDaemon) {
        vscode.window.showWarningMessage(
          `A Froth daemon is already running in ${status.target} mode. Stop it before switching to ${mode} mode.`,
        );
        return false;
      }

      await this.stopOwnedDaemon();
      this.disposeClient();
      this.deviceStatus = null;
      this.liveInfo = null;
      this.setState("idle");
      status = null;
    }

    if (!status) {
      const started = await this.startDaemon(mode);
      if (!started) {
        return false;
      }

      status = await this.connectToDaemon(true);
      if (!status) {
        vscode.window.showErrorMessage(
          "Froth daemon started, but the extension could not connect to it.",
        );
        return false;
      }
    }

    this.applyStatus(status);
    return true;
  }

  private async connectToDaemon(
    waitForSocket: boolean,
  ): Promise<StatusResult | null> {
    if (this.disposed) {
      return null;
    }

    if (this.client) {
      try {
        const status = this.normalizeStatus(await this.client.status());
        this.applyStatus(status);
        return status;
      } catch {
        this.disposeClient();
      }
    }

    if (waitForSocket) {
      const ready = await this.waitForSocket();
      if (!ready) {
        this.setState("no-daemon");
        return null;
      }
    }

    if (this.connecting) {
      return null;
    }
    this.connecting = true;

    const client = new DaemonClient();
    client.onNotification((n: DaemonNotification) => this.handleNotification(n));
    client.onClose(() => this.handleSocketClose());

    try {
      await client.connect();
      this.client = client;
      const status = this.normalizeStatus(await client.status());
      this.applyStatus(status);
      return status;
    } catch {
      client.dispose();
      this.setState("no-daemon");
      return null;
    } finally {
      this.connecting = false;
    }
  }

  private async startDaemon(mode: TargetMode): Promise<boolean> {
    const modeArgs = mode === "local" ? " --local" : "";

    try {
      await this.execFroth(`daemon start --background${modeArgs}`);
      this.extensionOwnsDaemon = true;
      return true;
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Failed to start Froth daemon: ${msg}`);
      this.setState("no-daemon");
      return false;
    }
  }

  private async stopOwnedDaemon(): Promise<void> {
    if (!this.extensionOwnsDaemon) {
      return;
    }

    this.extensionOwnsDaemon = false;

    try {
      await this.execFroth("daemon stop");
    } catch {
      // Daemon stop is best effort. The socket may already be gone.
    }
  }

  private async execFroth(args: string): Promise<void> {
    const cwd = this.workspaceCwd();
    await execAsync(`froth ${args}`, { cwd });
  }

  private workspaceCwd(): string {
    const editor = vscode.window.activeTextEditor;
    if (editor) {
      const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
      if (folder) {
        return folder.uri.fsPath;
      }
    }

    const firstFolder = vscode.workspace.workspaceFolders?.[0];
    if (firstFolder) {
      return firstFolder.uri.fsPath;
    }

    return process.cwd();
  }

  private waitForSocket(): Promise<boolean> {
    const deadline = Date.now() + DAEMON_START_TIMEOUT_MS;

    return new Promise<boolean>((resolve) => {
      const poll = () => {
        if (this.disposed) {
          resolve(false);
          return;
        }

        if (fs.existsSync(SOCKET_PATH)) {
          resolve(true);
          return;
        }

        if (Date.now() >= deadline) {
          resolve(false);
          return;
        }

        setTimeout(poll, DAEMON_START_POLL_MS);
      };

      poll();
    });
  }

  private requireIdle(): boolean {
    if (this.state === "running") {
      vscode.window.showWarningMessage(
        "Target is running a program. Use Interrupt first.",
      );
      return false;
    }

    if (!this.client || this.state !== "connected") {
      const targetLabel =
        this.selectedTarget === "local" ? "local target" : "device";
      vscode.window.showWarningMessage(`No ${targetLabel} connected`);
      return false;
    }

    return true;
  }

  private deriveIdleState(): ConnectionState {
    if (!this.client) {
      return "no-daemon";
    }
    if (this.deviceStatus?.connected) {
      return "connected";
    }
    return "disconnected";
  }

  private applyStatus(status: StatusResult): void {
    this.deviceStatus = status;
    this.selectedTarget = status.target;
    if (this.state !== "running") {
      this.setState(status.connected ? "connected" : "disconnected");
    } else {
      this.notifyStateChange();
      this.updateStatusBar();
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
        const params = n.params as ConnectedEvent | undefined;
        if (params) {
          this.output.appendLine(
            `[froth] target connected: ${params.device.board} (${params.device.version})`,
          );
        }
        this.setState("connected");
        void this.refreshDeviceInfo();
        break;
      }
      case "disconnected":
        this.liveInfo = null;
        this.setState("disconnected");
        this.output.appendLine("[froth] target disconnected");
        break;
      case "reconnecting":
        this.output.appendLine("[froth] reconnecting...");
        break;
    }
  }

  private handleSocketClose(): void {
    if (this.disposed) {
      return;
    }

    this.disposeClient();
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
      void this.connectToDaemon(false);
    }, RECONNECT_INTERVAL_MS);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private async evalAndLog(source: string): Promise<void> {
    if (!this.client) {
      vscode.window.showWarningMessage("Not connected to Froth daemon");
      return;
    }

    const preview = source.length > 80 ? source.slice(0, 77) + "..." : source;
    this.output.appendLine(`> ${preview}`);
    this.setState("running");

    let result: EvalResult;
    try {
      result = await this.client.eval(source);
    } catch (err: unknown) {
      if (this.state === "running") {
        this.setState(this.deriveIdleState());
      }
      if (err instanceof DaemonClientError && err.isNotConnected) {
        const targetLabel =
          this.selectedTarget === "local" ? "local target" : "device";
        vscode.window.showWarningMessage(`No ${targetLabel} connected`);
      } else {
        const msg = err instanceof Error ? err.message : String(err);
        vscode.window.showErrorMessage(`Eval failed: ${msg}`);
      }
      return;
    }

    if (this.state === "running") {
      this.setState("connected");
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
      return;
    }

    let msg = `error(${result.error_code ?? 0})`;
    if (result.fault_word) {
      msg += ` in "${result.fault_word}"`;
    }
    this.output.appendLine(msg);
  }

  private setState(state: ConnectionState): void {
    this.state = state;
    this.updateStatusBar();
    this.notifyStateChange();
  }

  private updateStatusBar(): void {
    switch (this.state) {
      case "running":
        this.statusItem.text = "$(sync~spin) Froth: Running";
        this.statusItem.tooltip =
          "Program running on the active Froth target. Use Interrupt to stop.";
        this.statusItem.backgroundColor = undefined;
        this.statusItem.command = this.selectedTargetCommand();
        this.interruptItem.text = "$(debug-stop) Interrupt";
        this.interruptItem.tooltip = "Send Ctrl-C to the active Froth target";
        this.interruptItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.errorBackground",
        );
        this.interruptItem.color = new vscode.ThemeColor(
          "statusBarItem.errorForeground",
        );
        this.interruptItem.show();
        return;
      case "connected":
        if (this.selectedTarget === "local") {
          this.statusItem.text = "$(terminal) Froth: Local POSIX";
          this.statusItem.tooltip = "Connected to the local POSIX Froth target";
        } else if (this.deviceStatus?.device) {
          this.statusItem.text = `$(plug) Froth: ${this.deviceStatus.device.board}`;
          this.statusItem.tooltip = `Connected to ${this.deviceStatus.device.board} via the Froth daemon`;
        } else {
          this.statusItem.text = "$(plug) Froth: Connected";
          this.statusItem.tooltip = "Connected to Froth via the daemon";
        }
        this.statusItem.backgroundColor = undefined;
        break;
      case "disconnected":
        if (this.deviceStatus?.reconnecting) {
          this.statusItem.text = "$(sync~spin) Froth: Reconnecting";
          this.statusItem.tooltip =
            "Daemon is reconnecting to the active Froth target.";
        } else {
          this.statusItem.text =
            this.selectedTarget === "local"
              ? "$(debug-disconnect) Froth: Local Offline"
              : "$(debug-disconnect) Froth: No Device";
          this.statusItem.tooltip =
            this.selectedTarget === "local"
              ? "Daemon running, but the local POSIX target is not available"
              : "Daemon running, no device connected";
        }
        this.statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.warningBackground",
        );
        break;
      case "no-daemon":
        this.statusItem.text = "$(circle-slash) Froth: No Daemon";
        this.statusItem.tooltip =
          "No Froth daemon is running. Click to start one.";
        this.statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.errorBackground",
        );
        break;
      case "idle":
        this.statusItem.text = "$(circle-large-outline) Froth: Idle";
        this.statusItem.tooltip =
          "Open a .froth file and connect to a device or local target.";
        this.statusItem.backgroundColor = undefined;
        break;
    }

    this.statusItem.command = this.selectedTargetCommand();
    this.interruptItem.hide();
  }

  private selectedTargetCommand(): string {
    return this.selectedTarget === "local" ? "froth.tryLocal" : "froth.connect";
  }

  private normalizeStatus(status: StatusResult): StatusResult {
    const target = status.target === "local" ? "local" : "serial";
    return { ...status, target };
  }

  private disposeClient(): void {
    if (this.client) {
      this.client.dispose();
      this.client = null;
    }
  }

  private notifyStateChange(): void {
    for (const listener of this.stateListeners) {
      listener();
    }
  }
}

class FrothSidebarProvider implements vscode.TreeDataProvider<SidebarItem> {
  private readonly changeEmitter = new vscode.EventEmitter<
    SidebarItem | undefined
  >();

  readonly onDidChangeTreeData = this.changeEmitter.event;

  constructor(private readonly controller: FrothController) {}

  refresh(): void {
    this.changeEmitter.fire(undefined);
  }

  getTreeItem(element: SidebarItem): vscode.TreeItem {
    return element;
  }

  getChildren(element?: SidebarItem): SidebarItem[] {
    if (element) {
      return [];
    }

    const status = this.controller.getDeviceStatus();
    if (!status?.connected || !status.device) {
      return [];
    }

    const live = this.controller.getLiveInfo();
    const dev = status.device;
    const heapUsed = live ? live.heap_used : dev.heap_used;
    const heapSize = live ? live.heap_size : dev.heap_size;
    const overlayUsed = live ? live.heap_overlay_used : 0;
    const slotCount = live ? live.slot_count : dev.slot_count;
    const overlaySlots = live ? live.slot_overlay_count : 0;
    const targetLabel =
      status.target === "local" ? "Local POSIX" : "Serial Device";
    const portLabel =
      status.target === "local" ? "stdin/stdout" : (status.port ?? "unknown");

    return [
      new SidebarItem("Target", targetLabel, new vscode.ThemeIcon("radio-tower")),
      new SidebarItem(
        "Board",
        dev.board,
        new vscode.ThemeIcon("circuit-board"),
      ),
      new SidebarItem("Port", portLabel, new vscode.ThemeIcon("plug")),
      new SidebarItem(
        "Heap",
        overlayUsed > 0
          ? `${heapUsed} / ${heapSize} (${overlayUsed} user)`
          : `${heapUsed} / ${heapSize}`,
        new vscode.ThemeIcon("database"),
      ),
      new SidebarItem(
        "Slots",
        overlaySlots > 0
          ? `${slotCount} (${overlaySlots} user)`
          : `${slotCount}`,
        new vscode.ThemeIcon("symbol-variable"),
      ),
      new SidebarItem(
        "Cell Bits",
        `${dev.cell_bits}`,
        new vscode.ThemeIcon("symbol-numeric"),
      ),
    ];
  }
}

class SidebarItem extends vscode.TreeItem {
  constructor(
    label: string,
    description: string,
    icon: vscode.ThemeIcon,
  ) {
    super(label, vscode.TreeItemCollapsibleState.None);
    this.description = description;
    this.iconPath = icon;
  }
}

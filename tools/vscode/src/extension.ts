import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import * as vscode from "vscode";
import { execFile, spawn } from "child_process";
import { promisify } from "util";
import { DaemonSupervisor } from "./daemon-supervisor";
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

const execFileAsync = promisify(execFile);

const SOCKET_PATH = path.join(os.homedir(), ".froth", "daemon.sock");

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
  private readonly supervisor: DaemonSupervisor;
  private client: DaemonClient | null = null;
  private state: ConnectionState = "idle";
  private disposed = false;
  private deactivating = false;
  private deviceStatus: StatusResult | null = null;
  private liveInfo: InfoResult | null = null;
  private stateListeners: StateChangeListener[] = [];
  private selectedTarget: TargetMode = "serial";
  private cliPathCache: string | null = null;

  constructor(
    private readonly output: vscode.OutputChannel,
    private readonly statusItem: vscode.StatusBarItem,
    private readonly interruptItem: vscode.StatusBarItem,
  ) {
    this.supervisor = new DaemonSupervisor(
      SOCKET_PATH,
      (args) => this.execFroth(args),
      {
        onNotification: (n) => this.handleNotification(n),
        onClose: () => this.handleSocketClose(),
      },
    );
    this.statusItem.command = "froth.connect";
    this.interruptItem.command = "froth.interrupt";
    this.updateStatusBar();
  }

  start(): void {
    this.notifyStateChange();
  }

  dispose(): void {
    this.disposed = true;
    this.disposeClient();
  }

  async deactivate(): Promise<void> {
    if (this.deactivating) {
      return;
    }
    this.deactivating = true;

    this.dispose();
    await this.supervisor.deactivate();
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
    const cliPath = await this.getCliPath();
    if (!cliPath) {
      return;
    }

    const terminal = vscode.window.createTerminal({
      name: "Froth Doctor",
      shellPath: cliPath,
      shellArgs: ["doctor"],
    });
    terminal.show(true);
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

    const document = editor.document;
    if (document.uri.scheme !== "file") {
      vscode.window.showWarningMessage(
        "Save the file to disk before sending it to Froth.",
      );
      return;
    }

    if (document.isDirty) {
      const saved = await document.save();
      if (!saved) {
        vscode.window.showErrorMessage("Save failed. File was not sent.");
        return;
      }
    }

    const cliPath = await this.getCliPath();
    if (!cliPath) {
      return;
    }

    const filePath = document.uri.fsPath;
    this.output.show(true);
    this.output.appendLine(`[froth] send ${filePath}`);
    this.setState("running");

    let exitCode: number | null = null;
    try {
      exitCode = await this.runCliSend(
        cliPath,
        filePath,
        this.workspaceCwdForUri(document.uri),
      );
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`froth send failed: ${msg}`);
      return;
    } finally {
      this.setState("idle");
      await this.refreshDeviceInfo();
    }

    if (exitCode !== 0) {
      vscode.window.showErrorMessage(
        `froth send failed with exit code ${exitCode}. See Froth Console for details.`,
      );
    }
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
    let status: StatusResult | null;
    try {
      status = await this.supervisor.refreshStatus();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Froth daemon error: ${msg}`);
      this.disposeClient();
      this.deviceStatus = null;
      this.liveInfo = null;
      this.setState("no-daemon");
      return;
    }

    this.client = this.supervisor.getClient();
    if (!status) {
      return;
    }

    this.applyStatus(this.normalizeStatus(status));
    if (!status.connected || !this.client) {
      this.liveInfo = null;
      this.notifyStateChange();
      return;
    }

    try {
      this.liveInfo = await this.client.info();
      this.notifyStateChange();
    } catch {
      // Leave the current UI state intact if the daemon vanishes mid-refresh.
    }
  }

  private async ensureDaemonMode(mode: TargetMode): Promise<boolean> {
    this.selectedTarget = mode;
    try {
      const status = this.normalizeStatus(
        await this.supervisor.ensureMode(mode, this.localRuntimePath()),
      );
      this.client = this.supervisor.getClient();
      this.applyStatus(status);
      return true;
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Froth daemon error: ${msg}`);
      this.setState("no-daemon");
      return false;
    }
  }

  private async execFroth(
    args: string[],
  ): Promise<{ stdout: string; stderr: string }> {
    const cliPath = await this.getCliPath();
    if (!cliPath) {
      throw new Error("Froth CLI not found");
    }

    const cwd = this.workspaceCwd();
    return execFileAsync(cliPath, args, { cwd });
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

  private workspaceCwdForUri(uri: vscode.Uri): string {
    const folder = vscode.workspace.getWorkspaceFolder(uri);
    if (folder) {
      return folder.uri.fsPath;
    }
    return this.workspaceCwd();
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

  private localRuntimePath(): string {
    return (
      vscode.workspace
        .getConfiguration("froth")
        .get<string>("localRuntimePath") ?? ""
    );
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

  private async getCliPath(): Promise<string | null> {
    if (this.cliPathCache) {
      return this.cliPathCache;
    }

    const configuredPath = vscode.workspace
      .getConfiguration("froth")
      .get<string>("cliPath");
    if (configuredPath && configuredPath.trim().length > 0) {
      const candidate = configuredPath.trim();
      const resolved = this.resolveCliCandidate(candidate);
      if (resolved) {
        this.cliPathCache = resolved;
        return resolved;
      }

      vscode.window.showErrorMessage(
        `Configured Froth CLI not found: ${candidate}. Install froth or update froth.cliPath.`,
      );
      return null;
    }

    for (const candidate of this.cliCandidates()) {
      const resolved = this.resolveCliCandidate(candidate);
      if (resolved) {
        this.cliPathCache = resolved;
        return resolved;
      }
    }

    vscode.window.showErrorMessage(
      "Froth CLI not found. Install `froth` and ensure it is on PATH, or set froth.cliPath.",
    );
    return null;
  }

  private cliCandidates(): string[] {
    return ["froth"];
  }

  private resolveCliCandidate(candidate: string): string | null {
    if (path.isAbsolute(candidate)) {
      return fs.existsSync(candidate) ? candidate : null;
    }

    if (candidate.includes(path.sep)) {
      const absolute = path.resolve(this.workspaceCwd(), candidate);
      return fs.existsSync(absolute) ? absolute : null;
    }

    const pathEnv = process.env.PATH ?? "";
    for (const dir of pathEnv.split(path.delimiter)) {
      if (!dir) {
        continue;
      }
      const fullPath = path.join(dir, candidate);
      if (fs.existsSync(fullPath)) {
        return fullPath;
      }
    }

    return null;
  }

  private normalizeStatus(status: StatusResult): StatusResult {
    const target = status.target === "local" ? "local" : "serial";
    return { ...status, target };
  }

  private async runCliSend(
    cliPath: string,
    filePath: string,
    cwd: string,
  ): Promise<number> {
    const cliOutput = new PrefixedOutputWriter(this.output, "[froth-cli]");

    return new Promise<number>((resolve, reject) => {
      const child = spawn(cliPath, ["--daemon", "send", filePath], { cwd });

      child.stdout.on("data", (chunk: Buffer | string) => {
        cliOutput.write(chunk.toString());
      });
      child.stderr.on("data", (chunk: Buffer | string) => {
        cliOutput.write(chunk.toString());
      });

      child.on("error", (err: Error & { code?: string }) => {
        cliOutput.flush();
        if (err.code === "ENOENT") {
          reject(
            new Error(
              "Froth CLI not found. Install `froth` and ensure it is on PATH, or set froth.cliPath.",
            ),
          );
          return;
        }
        reject(err);
      });

      child.on("close", (code, signal) => {
        cliOutput.flush();
        if (signal) {
          reject(new Error(`froth send terminated by signal ${signal}`));
          return;
        }
        resolve(code ?? 1);
      });
    });
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

class PrefixedOutputWriter {
  private pending = "";

  constructor(
    private readonly output: vscode.OutputChannel,
    private readonly prefix: string,
  ) {}

  write(text: string): void {
    const normalized = text.replace(/\r\n?/g, "\n");
    const parts = normalized.split("\n");
    parts[0] = this.pending + parts[0];
    this.pending = parts.pop() ?? "";

    for (const line of parts) {
      this.output.appendLine(`${this.prefix} ${line}`);
    }
  }

  flush(): void {
    if (this.pending.length === 0) {
      return;
    }
    this.output.appendLine(`${this.prefix} ${this.pending}`);
    this.pending = "";
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

import * as vscode from "vscode";
import {
  DaemonClient,
  DaemonClientError,
  DaemonNotification,
  ConsoleEvent,
  ConnectedEvent,
  EvalResult,
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

  context.subscriptions.push(
    output,
    statusItem,
    { dispose: () => controller.dispose() },
    vscode.commands.registerCommand("froth.sendSelection", () =>
      controller.sendSelection(),
    ),
    vscode.commands.registerCommand("froth.sendFile", () =>
      controller.sendFile(),
    ),
  );

  statusItem.show();
  controller.start();
}

export function deactivate(): void {
  // Cleanup handled by ExtensionContext.subscriptions
}

// Owns the daemon connection lifecycle and command handlers.
class FrothController {
  private client: DaemonClient | null = null;
  private state: ConnectionState = "no-daemon";
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private connecting = false;
  private disposed = false;

  constructor(
    private readonly output: vscode.OutputChannel,
    private readonly statusItem: vscode.StatusBarItem,
  ) {
    this.updateStatusBar();
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

    await this.evalAndLog(text);
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
      // Socket doesn't exist or daemon isn't running.
      // Dispose the client so its deferred close event doesn't
      // trigger our handleSocketClose with a spurious log line.
      client.dispose();
      this.connecting = false;
      this.setState("no-daemon");
      this.scheduleReconnect();
      return;
    }

    this.client = client;
    this.connecting = false;

    // Ask the daemon whether a device is attached
    try {
      const st = await client.status();
      this.setState(st.connected ? "connected" : "disconnected");
      if (st.connected && st.device) {
        this.output.appendLine(
          `[froth] connected: ${st.device.board} (${st.device.version})`,
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
        break;
      }
      case "disconnected":
        this.setState("disconnected");
        this.output.appendLine("[froth] device disconnected");
        break;
      case "reconnecting":
        this.output.appendLine("[froth] reconnecting...");
        break;
    }
  }

  private handleSocketClose(): void {
    // The daemon socket dropped. Dispose old client, retry later.
    if (this.client) {
      this.client.dispose();
      this.client = null;
    }
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

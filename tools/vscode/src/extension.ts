import * as vscode from "vscode";
import {
  ConnectionState,
  ControllerSnapshot,
  FrothController,
} from "./controller";
import { resolveSendSourceCommand } from "./send-file";
import {
  BufferedOutputChannel,
  VSCodeHost,
  createControlSessionClient,
  createVSCodeCliPathResolver,
} from "./vscode-host";

let activeController: FrothController | null = null;

export interface ExtensionTestApi {
  getSnapshot(): ControllerSnapshot;
  getOutputText(): string;
  clearOutput(): void;
  enqueueInputBoxResponse(value: string | undefined): void;
  enqueueWarningResponse(value: string | undefined): void;
  waitForState(state: ConnectionState, timeoutMs?: number): Promise<void>;
}

export function activate(
  context: vscode.ExtensionContext,
): ExtensionTestApi {
  const bufferedOutput = new BufferedOutputChannel(
    vscode.window.createOutputChannel("Froth Console"),
  );
  const statusItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    50,
  );
  const interruptItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    49,
  );
  const rerunItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    48,
  );
  const pinnedRunItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    47,
  );
  const host = new VSCodeHost(context, bufferedOutput);
  const controller = new FrothController({
    host,
    resolveCliPath: createVSCodeCliPathResolver(host),
    createClient: createControlSessionClient,
    resolveSendSource: resolveSendSourceCommand,
  });
  activeController = controller;

  const sidebarProvider = new FrothSidebarProvider(controller);
  const treeView = vscode.window.createTreeView("frothDeviceView", {
    treeDataProvider: sidebarProvider,
  });

  const commands: Array<[string, () => Promise<void> | void]> = [
    ["froth.connect", async () => controller.connectToDevice()],
    ["froth.forceReconnect", async () => controller.forceReconnect()],
    ["froth.disconnect", () => controller.disconnect()],
    ["froth.sendSelection", () => controller.sendSelection()],
    ["froth.runBinding", () => controller.runBinding()],
    ["froth.pinRunBinding", () => controller.pinRunBinding()],
    ["froth.runLast", () => controller.runLast()],
    ["froth.runPinned", () => controller.runPinned()],
    ["froth.sendFile", () => controller.sendFile()],
    ["froth.interrupt", () => controller.interrupt()],
    ["froth.words", () => controller.showWords()],
    ["froth.see", () => controller.showSee()],
    ["froth.core", () => controller.showCore()],
    ["froth.slotInfo", () => controller.showSlotInfo()],
    ["froth.save", () => controller.saveSnapshot()],
    ["froth.restore", () => controller.restoreSnapshot()],
    ["froth.wipe", () => controller.wipeSnapshot()],
    ["froth.doctor", () => controller.runDoctor()],
    ["froth.showConsole", () => controller.showConsole()],
  ];

  for (const [command, handler] of commands) {
    context.subscriptions.push(vscode.commands.registerCommand(command, handler));
  }

  context.subscriptions.push(
    statusItem,
    interruptItem,
    rerunItem,
    pinnedRunItem,
    treeView,
    { dispose: () => bufferedOutput.dispose() },
    { dispose: () => controller.dispose() },
  );

  controller.onStateChange(() => {
    sidebarProvider.refresh();
    const snapshot = controller.getSnapshot();
    updateStatusBar(statusItem, interruptItem, rerunItem, pinnedRunItem, snapshot);
    updateCommandContext(snapshot);
  });

  const snapshot = controller.getSnapshot();
  updateStatusBar(statusItem, interruptItem, rerunItem, pinnedRunItem, snapshot);
  updateCommandContext(snapshot);
  statusItem.show();
  controller.start();

  const api: ExtensionTestApi = {
    getSnapshot: () => controller.getSnapshot(),
    getOutputText: () => bufferedOutput.getText(),
    clearOutput: () => bufferedOutput.clearBuffer(),
    enqueueInputBoxResponse: (value) => host.enqueueInputBoxResponse(value),
    enqueueWarningResponse: (value) => host.enqueueWarningResponse(value),
    waitForState: (state, timeoutMs = 5000) =>
      waitForState(controller, state, timeoutMs),
  };
  return api;
}

export function deactivate(): Thenable<void> | undefined {
  const controller = activeController;
  activeController = null;
  if (controller) {
    return controller.deactivate();
  }
  return undefined;
}

function updateStatusBar(
  statusItem: vscode.StatusBarItem,
  interruptItem: vscode.StatusBarItem,
  rerunItem: vscode.StatusBarItem,
  pinnedRunItem: vscode.StatusBarItem,
  snapshot: ControllerSnapshot,
): void {
  switch (snapshot.state) {
    case "idle":
      statusItem.text = "$(circle-large-outline) Froth: Idle";
      statusItem.tooltip =
        "Open a .froth file and connect to a Froth device.";
      statusItem.command = "froth.connect";
      statusItem.backgroundColor = undefined;
      break;
    case "connecting":
      statusItem.text = "$(sync~spin) Froth: Connecting";
      statusItem.tooltip = "Connecting to a Froth device";
      statusItem.command = "froth.connect";
      statusItem.backgroundColor = undefined;
      break;
    case "connected":
      if (snapshot.degradedSendFile) {
        statusItem.text = snapshot.device
          ? `$(warning) Froth: ${snapshot.device.board} (additive)`
          : "$(warning) Froth: Connected (additive)";
        statusItem.tooltip =
          "Send File is in additive fallback mode because the connected firmware does not support control reset.";
        statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.warningBackground",
        );
      } else {
        statusItem.text = snapshot.device
          ? `$(plug) Froth: ${snapshot.device.board}`
          : "$(plug) Froth: Connected";
        statusItem.tooltip = snapshot.device
          ? `Connected to ${snapshot.device.board} on ${snapshot.device.port}`
          : "Connected to a Froth device";
        statusItem.backgroundColor = undefined;
      }
      statusItem.command = "froth.disconnect";
      break;
    case "running":
      statusItem.text = "$(sync~spin) Froth: Running";
      statusItem.tooltip =
        "A Froth program is running. Force reconnect if the board was reset.";
      statusItem.command = "froth.forceReconnect";
      statusItem.backgroundColor = undefined;
      break;
    case "disconnected":
      statusItem.text = "$(debug-disconnect) Froth: Disconnected";
      statusItem.tooltip = "No active Froth control session";
      statusItem.command = "froth.connect";
      statusItem.backgroundColor = new vscode.ThemeColor(
        "statusBarItem.warningBackground",
      );
      break;
  }

  if (snapshot.state === "running") {
    interruptItem.text = "$(debug-stop) Interrupt";
    interruptItem.tooltip = "Interrupt the running Froth program";
    interruptItem.command = "froth.interrupt";
    interruptItem.backgroundColor = new vscode.ThemeColor(
      "statusBarItem.errorBackground",
    );
    interruptItem.color = new vscode.ThemeColor(
      "statusBarItem.errorForeground",
    );
    interruptItem.show();
  } else {
    interruptItem.hide();
  }

  if (snapshot.state === "connected" && snapshot.lastRunPreview) {
    rerunItem.text = "$(debug-rerun) Rerun";
    rerunItem.tooltip = `Run last Froth form: ${snapshot.lastRunPreview}`;
    rerunItem.command = "froth.runLast";
    rerunItem.show();
  } else {
    rerunItem.hide();
  }

  if (snapshot.state === "connected" && snapshot.pinnedRunPreview) {
    pinnedRunItem.text = "$(debug-start) Run Pin";
    pinnedRunItem.tooltip = `Run pinned Froth binding: ${snapshot.pinnedRunPreview}`;
    pinnedRunItem.command = "froth.runPinned";
    pinnedRunItem.show();
  } else {
    pinnedRunItem.hide();
  }
}

function updateCommandContext(snapshot: ControllerSnapshot): void {
  void vscode.commands.executeCommand(
    "setContext",
    "froth.isRunning",
    snapshot.state === "running",
  );
  void vscode.commands.executeCommand(
    "setContext",
    "froth.hasLastRun",
    snapshot.lastRunPreview !== null,
  );
  void vscode.commands.executeCommand(
    "setContext",
    "froth.hasPinnedRun",
    snapshot.pinnedRunPreview !== null,
  );
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

    const snapshot = this.controller.getSnapshot();
    const items = [
      new SidebarItem("Session", snapshot.state, new vscode.ThemeIcon("pulse")),
    ];

    if (snapshot.degradedSendFile) {
      items.push(
        new SidebarItem(
          "Send File",
          "additive fallback",
          new vscode.ThemeIcon("warning"),
        ),
      );
    } else {
      items.push(
        new SidebarItem(
          "Send File",
          "reset + eval",
          new vscode.ThemeIcon("history"),
        ),
      );
    }

    if (!snapshot.device) {
      return appendPinnedRunItem(items, snapshot);
    }

    return appendPinnedRunItem(items.concat([
      new SidebarItem(
        "Board",
        snapshot.device.board,
        new vscode.ThemeIcon("circuit-board"),
      ),
      new SidebarItem(
        "Port",
        snapshot.device.port,
        new vscode.ThemeIcon("plug"),
      ),
      new SidebarItem(
        "Version",
        snapshot.device.version,
        new vscode.ThemeIcon("tag"),
      ),
      new SidebarItem(
        "Cell Bits",
        `${snapshot.device.cell_bits}`,
        new vscode.ThemeIcon("symbol-numeric"),
      ),
    ]), snapshot);
  }
}

function appendPinnedRunItem(
  items: SidebarItem[],
  snapshot: ControllerSnapshot,
): SidebarItem[] {
  items.push(
    new SidebarItem(
      "Pinned Run",
      snapshot.pinnedRunPreview ?? "none",
      new vscode.ThemeIcon(snapshot.pinnedRunPreview ? "pinned" : "circle-slash"),
    ),
  );
  return items;
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

async function waitForState(
  controller: FrothController,
  state: ConnectionState,
  timeoutMs: number,
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (controller.getSnapshot().state === state) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  throw new Error(`timed out waiting for controller state ${state}`);
}

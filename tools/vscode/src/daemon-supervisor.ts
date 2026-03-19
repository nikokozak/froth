import * as fs from "fs";
import * as net from "net";
import {
  DaemonClient,
  DaemonNotification,
  StatusResult,
} from "./daemon-client";

export const EXPECTED_DAEMON_API_VERSION = 1;

export interface ExecResult {
  stdout: string;
  stderr: string;
}

export type ExecFroth = (args: string[]) => Promise<ExecResult>;

interface Hooks {
  onNotification: (n: DaemonNotification) => void;
  onClose: () => void;
}

export class DaemonSupervisor {
  private client: DaemonClient | null = null;
  private ownedPID: number | null = null;
  private readonly hooks: Hooks;

  constructor(
    private readonly socketPath: string,
    private readonly execFroth: ExecFroth,
    hooks: Hooks,
  ) {
    this.hooks = hooks;
  }

  getClient(): DaemonClient | null {
    return this.client;
  }

  async deactivate(): Promise<void> {
    this.disposeClient();

    if (this.ownedPID !== null) {
      try {
        await this.execFroth(["daemon", "stop", "--pid", String(this.ownedPID)]);
      } catch {
        // Best effort.
      }
      this.ownedPID = null;
    }
  }

  async ensureMode(
    mode: "serial" | "local",
    localRuntimePath: string,
  ): Promise<StatusResult> {
    let status = await this.connectStatus();

    if (status && status.target !== mode) {
      await this.stopRunningDaemon();
      status = null;
    }

    if (!status) {
      status = await this.startAndConnect(mode, localRuntimePath);
    }

    return status;
  }

  async refreshStatus(): Promise<StatusResult | null> {
    if (!this.client) {
      return null;
    }

    try {
      const status = await this.client.status();
      this.validateStatus(status);
      return status;
    } catch (err) {
      if (isAPIMismatch(err)) {
        throw err;
      }
      this.disposeClient();
      return null;
    }
  }

  private async startAndConnect(
    mode: "serial" | "local",
    localRuntimePath: string,
  ): Promise<StatusResult> {
    const args = ["daemon", "start", "--background"];
    if (mode === "local") {
      args.push("--local");
      if (localRuntimePath.trim().length > 0) {
        args.push("--local-runtime", localRuntimePath.trim());
      }
    }

    const result = await this.execFroth(args);
    const pid = Number.parseInt(result.stdout.trim(), 10);
    if (!Number.isFinite(pid)) {
      throw new Error("daemon start did not return a pid");
    }
    this.ownedPID = pid;

    const status = await this.connectRequired();
    if (status.pid !== pid) {
      this.ownedPID = status.pid;
    }
    return status;
  }

  private async stopRunningDaemon(): Promise<void> {
    try {
      await this.execFroth(["daemon", "stop"]);
    } catch {
      // Best effort.
    }
    await this.waitForDaemonStop(5000);
    this.disposeClient();
    this.ownedPID = null;
  }

  private async connectStatus(): Promise<StatusResult | null> {
    if (this.client) {
      try {
        const status = await this.client.status();
        this.validateStatus(status);
        return status;
      } catch (err) {
        if (isAPIMismatch(err)) {
          throw err;
        }
        this.disposeClient();
      }
    }

    try {
      return await this.connectRequired();
    } catch (err) {
      if (isAPIMismatch(err)) {
        throw err;
      }
      return null;
    }
  }

  private async connectRequired(): Promise<StatusResult> {
    const client = new DaemonClient();
    client.onNotification(this.hooks.onNotification);
    client.onClose(this.hooks.onClose);

    try {
      await client.connect(this.socketPath);
      const status = await client.status();
      this.validateStatus(status);
      this.disposeClient();
      this.client = client;
      return status;
    } catch (err) {
      client.dispose();
      throw err;
    }
  }

  private validateStatus(status: StatusResult): void {
    if (status.api_version !== EXPECTED_DAEMON_API_VERSION) {
      throw new Error(
        `daemon api mismatch: expected ${EXPECTED_DAEMON_API_VERSION}, got ${status.api_version}`,
      );
    }
  }

  private async waitForDaemonStop(timeoutMs: number): Promise<void> {
    const deadline = Date.now() + timeoutMs;

    for (;;) {
      if (Date.now() >= deadline) {
        throw new Error("daemon did not stop");
      }

      if (!fs.existsSync(this.socketPath)) {
        return;
      }

      const ready = await this.socketAlive();
      if (!ready) {
        return;
      }

      await sleep(100);
    }
  }

  private async socketAlive(): Promise<boolean> {
    return new Promise<boolean>((resolve) => {
      const sock = net.createConnection(this.socketPath);

      let settled = false;

      sock.on("connect", () => {
        settled = true;
        sock.destroy();
        resolve(true);
      });

      sock.on("error", () => {
        if (!settled) {
          settled = true;
          resolve(false);
        }
      });
    });
  }

  private disposeClient(): void {
    if (this.client) {
      this.client.dispose();
      this.client = null;
    }
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function isAPIMismatch(err: unknown): boolean {
  return err instanceof Error && err.message.startsWith("daemon api mismatch:");
}

#!/usr/bin/env node

"use strict";

const fs = require("fs");
const net = require("net");
const os = require("os");
const path = require("path");
const {
  DaemonSupervisor,
  EXPECTED_DAEMON_API_VERSION,
} = require("../out/daemon-supervisor");

let passed = 0;
let failed = 0;
const failures = [];

function assert(cond, msg) {
  if (!cond) throw new Error("assertion failed: " + msg);
}

function assertEq(a, b, msg) {
  if (a !== b) {
    throw new Error(
      `${msg}: expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`
    );
  }
}

async function assertRejects(promise, pattern, msg) {
  try {
    await promise;
    throw new Error("expected rejection: " + msg);
  } catch (err) {
    if (err.message === "expected rejection: " + msg) throw err;
    if (pattern && !pattern.test(String(err.message))) {
      throw new Error(`${msg}: wrong error ${err.message}`);
    }
    return err;
  }
}

async function test(name, fn) {
  try {
    await fn();
    passed++;
    process.stdout.write(`  ok  ${name}\n`);
  } catch (err) {
    failed++;
    failures.push({ name, err });
    process.stdout.write(`  FAIL ${name}: ${err.message}\n`);
  }
}

class MockDaemon {
  constructor(socketPath, status) {
    this.socketPath = socketPath;
    this.status = {
      pid: 1000,
      api_version: EXPECTED_DAEMON_API_VERSION,
      daemon_version: "0.1.0-test",
      running: true,
      connected: false,
      reconnecting: false,
      target: "serial",
      port: "/dev/mock",
      ...status,
    };
    this.server = null;
    this.connections = [];
  }

  async start() {
    try {
      fs.unlinkSync(this.socketPath);
    } catch {
      // ignore
    }

    await new Promise((resolve, reject) => {
      this.server = net.createServer((conn) => this.handleConnection(conn));
      this.server.on("error", reject);
      this.server.listen(this.socketPath, resolve);
    });
  }

  async stop() {
    for (const conn of this.connections) {
      conn.destroy();
    }
    this.connections = [];

    if (this.server) {
      await new Promise((resolve) => this.server.close(resolve));
      this.server = null;
    }

    try {
      fs.unlinkSync(this.socketPath);
    } catch {
      // ignore
    }
  }

  setStatus(patch) {
    Object.assign(this.status, patch);
  }

  handleConnection(conn) {
    this.connections.push(conn);
    let buf = "";

    conn.on("data", (chunk) => {
      buf += chunk.toString("utf8");
      let idx;
      while ((idx = buf.indexOf("\n")) !== -1) {
        const line = buf.slice(0, idx);
        buf = buf.slice(idx + 1);
        if (line) this.handleRequest(conn, line);
      }
    });

    conn.on("close", () => {
      this.connections = this.connections.filter((c) => c !== conn);
    });
  }

  handleRequest(conn, line) {
    let req;
    try {
      req = JSON.parse(line);
    } catch {
      conn.write(
        JSON.stringify({
          jsonrpc: "2.0",
          error: { code: -32700, message: "parse error" },
          id: null,
        }) + "\n"
      );
      return;
    }

    if (req.method === "status") {
      conn.write(
        JSON.stringify({
          jsonrpc: "2.0",
          result: this.status,
          id: req.id,
        }) + "\n"
      );
      return;
    }

    conn.write(
      JSON.stringify({
        jsonrpc: "2.0",
        error: { code: -32601, message: "unknown method: " + req.method },
        id: req.id,
      }) + "\n"
    );
  }
}

function tmpSocket() {
  return path.join(
    os.tmpdir(),
    `fsv-${process.pid}-${Date.now().toString(36)}-${Math.random()
      .toString(16)
      .slice(2, 6)}.sock`
  );
}

function hooks() {
  return {
    onNotification() {},
    onClose() {},
  };
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function main() {
  process.stdout.write("\n=== Froth daemon-supervisor smoke tests ===\n\n");

  await test("starts local daemon from scratch and stops owned pid on deactivate", async () => {
    const socketPath = tmpSocket();
    const calls = [];
    let daemon = null;

    const execFroth = async (args) => {
      calls.push(args);
      if (args[0] === "daemon" && args[1] === "start") {
        daemon = new MockDaemon(socketPath, {
          pid: 4101,
          target: "local",
          connected: true,
          port: "stdin/stdout",
        });
        await daemon.start();
        return { stdout: "4101\n", stderr: "" };
      }
      if (args[0] === "daemon" && args[1] === "stop") {
        await daemon.stop();
        daemon = null;
        return { stdout: "", stderr: "" };
      }
      throw new Error("unexpected exec args: " + args.join(" "));
    };

    const supervisor = new DaemonSupervisor(socketPath, execFroth, hooks());
    const status = await supervisor.ensureMode("local", "/tmp/Froth");
    assertEq(status.pid, 4101, "status.pid");
    assertEq(status.target, "local", "status.target");
    assertEq(calls.length, 1, "start call count");
    assertEq(
      calls[0].join(" "),
      "daemon start --background --local --local-runtime /tmp/Froth",
      "start args"
    );

    await supervisor.deactivate();
    assertEq(calls.length, 2, "call count after deactivate");
    assertEq(calls[1].join(" "), "daemon stop --pid 4101", "stop args");
  });

  await test("serial mode omits local runtime args", async () => {
    const socketPath = tmpSocket();
    const calls = [];
    let daemon = null;

    const execFroth = async (args) => {
      calls.push(args);
      if (args[1] === "start") {
        daemon = new MockDaemon(socketPath, {
          pid: 4201,
          target: "serial",
          connected: false,
        });
        await daemon.start();
        return { stdout: "4201\n", stderr: "" };
      }
      if (args[1] === "stop") {
        await daemon.stop();
        daemon = null;
        return { stdout: "", stderr: "" };
      }
      throw new Error("unexpected exec args: " + args.join(" "));
    };

    const supervisor = new DaemonSupervisor(socketPath, execFroth, hooks());
    const status = await supervisor.ensureMode("serial", "/tmp/ignored");
    assertEq(status.target, "serial", "status.target");
    assertEq(calls[0].join(" "), "daemon start --background", "start args");
    await supervisor.deactivate();
  });

  await test("wrong-mode restart waits for socket removal before start", async () => {
    const socketPath = tmpSocket();
    let daemon = new MockDaemon(socketPath, {
      pid: 4301,
      target: "serial",
      connected: false,
    });
    await daemon.start();

    const calls = [];
    let socketRemovedAt = 0;
    let startedAfterStop = false;

    const execFroth = async (args) => {
      calls.push(args);
      if (args[0] !== "daemon") {
        throw new Error("unexpected exec args: " + args.join(" "));
      }

      if (args[1] === "stop") {
        setTimeout(async () => {
          await daemon.stop();
          socketRemovedAt = Date.now();
        }, 150);
        return { stdout: "", stderr: "" };
      }

      if (args[1] === "start") {
        startedAfterStop = socketRemovedAt !== 0 && Date.now() >= socketRemovedAt;
        daemon = new MockDaemon(socketPath, {
          pid: 4302,
          target: "local",
          connected: true,
          port: "stdin/stdout",
        });
        await daemon.start();
        return { stdout: "4302\n", stderr: "" };
      }

      throw new Error("unexpected exec args: " + args.join(" "));
    };

    const supervisor = new DaemonSupervisor(socketPath, execFroth, hooks());
    const status = await supervisor.ensureMode("local", "/tmp/Froth");
    assertEq(status.pid, 4302, "restarted pid");
    assertEq(calls[0].join(" "), "daemon stop", "stop call");
    assertEq(
      calls[1].join(" "),
      "daemon start --background --local --local-runtime /tmp/Froth",
      "start call"
    );
    assert(startedAfterStop, "start ran before old socket was removed");
    await supervisor.deactivate();
  });

  await test("existing daemon in the right mode is reused", async () => {
    const socketPath = tmpSocket();
    const daemon = new MockDaemon(socketPath, {
      pid: 4401,
      target: "local",
      connected: true,
      port: "stdin/stdout",
    });
    await daemon.start();

    const calls = [];
    const supervisor = new DaemonSupervisor(
      socketPath,
      async (args) => {
        calls.push(args);
        return { stdout: "", stderr: "" };
      },
      hooks()
    );

    const status = await supervisor.ensureMode("local", "/tmp/Froth");
    assertEq(status.pid, 4401, "status.pid");
    assertEq(calls.length, 0, "exec should not be used");

    await daemon.stop();
    await supervisor.deactivate();
  });

  await test("daemon api mismatch fails closed without restart", async () => {
    const socketPath = tmpSocket();
    const daemon = new MockDaemon(socketPath, {
      pid: 4501,
      api_version: 99,
      target: "local",
    });
    await daemon.start();

    const calls = [];
    const supervisor = new DaemonSupervisor(
      socketPath,
      async (args) => {
        calls.push(args);
        return { stdout: "", stderr: "" };
      },
      hooks()
    );

    await assertRejects(
      supervisor.ensureMode("local", "/tmp/Froth"),
      /daemon api mismatch:/,
      "ensureMode should fail closed on api mismatch"
    );
    assertEq(calls.length, 0, "mismatch should not trigger exec");

    await daemon.stop();
  });

  await test("refreshStatus rethrows daemon api mismatch", async () => {
    const socketPath = tmpSocket();
    let daemon = null;

    const execFroth = async (args) => {
      if (args[1] === "start") {
        daemon = new MockDaemon(socketPath, {
          pid: 4601,
          target: "local",
          connected: true,
          port: "stdin/stdout",
        });
        await daemon.start();
        return { stdout: "4601\n", stderr: "" };
      }
      if (args[1] === "stop") {
        await daemon.stop();
        daemon = null;
        return { stdout: "", stderr: "" };
      }
      throw new Error("unexpected exec args: " + args.join(" "));
    };

    const supervisor = new DaemonSupervisor(socketPath, execFroth, hooks());
    await supervisor.ensureMode("local", "/tmp/Froth");
    daemon.setStatus({ api_version: 77 });

    await assertRejects(
      supervisor.refreshStatus(),
      /daemon api mismatch:/,
      "refreshStatus should fail closed on api mismatch"
    );

    await supervisor.deactivate();
  });

  await test("owned pid follows daemon status when start stdout drifts", async () => {
    const socketPath = tmpSocket();
    const calls = [];
    let daemon = null;

    const execFroth = async (args) => {
      calls.push(args);
      if (args[1] === "start") {
        daemon = new MockDaemon(socketPath, {
          pid: 4702,
          target: "local",
          connected: true,
          port: "stdin/stdout",
        });
        await daemon.start();
        return { stdout: "4701\n", stderr: "" };
      }
      if (args[1] === "stop") {
        await daemon.stop();
        daemon = null;
        return { stdout: "", stderr: "" };
      }
      throw new Error("unexpected exec args: " + args.join(" "));
    };

    const supervisor = new DaemonSupervisor(socketPath, execFroth, hooks());
    const status = await supervisor.ensureMode("local", "/tmp/Froth");
    assertEq(status.pid, 4702, "status.pid");
    await supervisor.deactivate();
    assertEq(calls[1].join(" "), "daemon stop --pid 4702", "owned stop pid");
  });

  await test("non-numeric daemon start output is rejected", async () => {
    const socketPath = tmpSocket();
    const supervisor = new DaemonSupervisor(
      socketPath,
      async () => ({ stdout: "not-a-pid\n", stderr: "" }),
      hooks()
    );

    await assertRejects(
      supervisor.ensureMode("local", "/tmp/Froth"),
      /daemon start did not return a pid/,
      "start should reject invalid pid output"
    );
  });

  process.stdout.write(
    `\n=== Results ===\n${passed} passed, ${failed} failed\n`
  );
  if (failed > 0) {
    for (const failure of failures) {
      process.stdout.write(`\n- ${failure.name}: ${failure.err.stack}\n`);
    }
    process.exit(1);
  }
}

main().catch((err) => {
  process.stderr.write(err.stack + "\n");
  process.exit(1);
});

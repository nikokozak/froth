#!/usr/bin/env node
//
// Smoke test for daemon-client.ts. Spins up a mock daemon that speaks
// the same JSON-RPC 2.0 protocol as the real Go daemon (rpc.go), then
// hammers the TypeScript client against it.
//
// Run: node tools/vscode/test/smoke.js
// Requires: npm run compile first (uses out/daemon-client.js)

"use strict";

const net = require("net");
const fs = require("fs");
const path = require("path");
const os = require("os");
const { DaemonClient, DaemonClientError } = require("../out/daemon-client");

// --- Test infrastructure ---

let passed = 0;
let failed = 0;
let skipped = 0;
const failures = [];

function assert(cond, msg) {
  if (!cond) throw new Error("assertion failed: " + msg);
}

function assertEq(a, b, msg) {
  if (a !== b)
    throw new Error(
      `${msg}: expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`
    );
}

function assertThrows(fn, msg) {
  let threw = false;
  try {
    fn();
  } catch {
    threw = true;
  }
  if (!threw) throw new Error("expected throw: " + msg);
}

async function assertRejects(promise, msg) {
  try {
    await promise;
    throw new Error("expected rejection: " + msg);
  } catch (err) {
    if (err.message === "expected rejection: " + msg) throw err;
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

// --- Mock daemon ---
//
// Mimics the Go daemon's JSON-RPC server. Each connection gets a
// newline-delimited JSON stream. Requests have "id", notifications don't.

class MockDaemon {
  constructor(socketPath) {
    this.socketPath = socketPath;
    this.server = null;
    this.connections = [];
    this.deviceConnected = true;
    this.evalDelay = 0;
    this.consoleTextDuringEval = null;
    this.evalHandler = null;
  }

  start() {
    return new Promise((resolve, reject) => {
      try {
        fs.unlinkSync(this.socketPath);
      } catch {
        // ignore
      }
      this.server = net.createServer((conn) => this.handleConnection(conn));
      this.server.on("error", reject);
      this.server.listen(this.socketPath, () => resolve());
    });
  }

  stop() {
    return new Promise((resolve) => {
      for (const c of this.connections) c.destroy();
      this.connections = [];
      if (this.server) {
        this.server.close(() => resolve());
        this.server = null;
      } else {
        resolve();
      }
      try {
        fs.unlinkSync(this.socketPath);
      } catch {
        // ignore
      }
    });
  }

  handleConnection(conn) {
    this.connections.push(conn);
    let buf = "";

    conn.on("data", (chunk) => {
      buf += chunk.toString("utf-8");
      let idx;
      while ((idx = buf.indexOf("\n")) !== -1) {
        const line = buf.slice(0, idx);
        buf = buf.slice(idx + 1);
        if (line.length > 0) this.handleRequest(conn, line);
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
      this.sendResponse(conn, null, { code: -32700, message: "parse error" });
      return;
    }

    if (req.jsonrpc !== "2.0") {
      this.sendResponse(conn, req.id, {
        code: -32600,
        message: "invalid jsonrpc version",
      });
      return;
    }

    switch (req.method) {
      case "hello":
        this.handleHello(conn, req);
        break;
      case "eval":
        this.handleEval(conn, req);
        break;
      case "info":
        this.handleInfo(conn, req);
        break;
      case "status":
        this.handleStatus(conn, req);
        break;
      default:
        this.sendResponse(conn, req.id, {
          code: -32601,
          message: "unknown method: " + req.method,
        });
    }
  }

  handleHello(conn, req) {
    if (!this.deviceConnected) {
      this.sendResponse(conn, req.id, {
        code: -32001,
        message: "device not connected",
      });
      return;
    }
    this.sendResult(conn, req.id, {
      cell_bits: 32,
      max_payload: 240,
      heap_size: 4096,
      heap_used: 708,
      slot_count: 64,
      version: "0.1.0-test",
      board: "mock-board",
    });
  }

  handleEval(conn, req) {
    if (!this.deviceConnected) {
      this.sendResponse(conn, req.id, {
        code: -32001,
        message: "device not connected",
      });
      return;
    }

    if (!req.params || typeof req.params.source !== "string") {
      this.sendResponse(conn, req.id, {
        code: -32600,
        message: "invalid params",
      });
      return;
    }

    const source = req.params.source;

    // Custom handler for special test scenarios
    if (this.evalHandler) {
      const result = this.evalHandler(source, conn, req);
      if (result !== undefined) return;
    }

    const doEval = () => {
      // Send console text if configured (simulates device output during eval)
      if (this.consoleTextDuringEval) {
        this.sendNotification(conn, "console", {
          text: this.consoleTextDuringEval,
        });
      }

      // Simulate simple arithmetic for "N M +"
      const match = source.match(/^(\d+)\s+(\d+)\s+\+$/);
      if (match) {
        const sum = parseInt(match[1]) + parseInt(match[2]);
        this.sendResult(conn, req.id, {
          status: 0,
          stack_repr: `[${sum}]`,
        });
        return;
      }

      // Simulate error for "bad"
      if (source === "bad") {
        this.sendResult(conn, req.id, {
          status: 1,
          error_code: 3,
          fault_word: "bad",
          stack_repr: "",
        });
        return;
      }

      // Simulate undefined word
      if (source === "nosuchword") {
        this.sendResult(conn, req.id, {
          status: 1,
          error_code: 5,
          fault_word: "nosuchword",
        });
        return;
      }

      // Default: success with empty stack
      this.sendResult(conn, req.id, { status: 0 });
    };

    if (this.evalDelay > 0) {
      setTimeout(doEval, this.evalDelay);
    } else {
      doEval();
    }
  }

  handleInfo(conn, req) {
    if (!this.deviceConnected) {
      this.sendResponse(conn, req.id, {
        code: -32001,
        message: "device not connected",
      });
      return;
    }
    this.sendResult(conn, req.id, {
      heap_size: 4096,
      heap_used: 708,
      heap_overlay_used: 20,
      slot_count: 64,
      slot_overlay_count: 3,
      version: "0.1.0-test",
    });
  }

  handleStatus(conn, req) {
    const result = {
      running: true,
      connected: this.deviceConnected,
      port: "/dev/ttyUSB0",
    };
    if (this.deviceConnected) {
      result.device = {
        cell_bits: 32,
        max_payload: 240,
        heap_size: 4096,
        heap_used: 708,
        slot_count: 64,
        version: "0.1.0-test",
        board: "mock-board",
      };
    }
    this.sendResult(conn, req.id, result);
  }

  sendResult(conn, id, result) {
    const msg = JSON.stringify({ jsonrpc: "2.0", result, id }) + "\n";
    conn.write(msg);
  }

  sendResponse(conn, id, error) {
    const msg = JSON.stringify({ jsonrpc: "2.0", error, id }) + "\n";
    conn.write(msg);
  }

  sendNotification(conn, method, params) {
    const msg = JSON.stringify({ jsonrpc: "2.0", method, params }) + "\n";
    conn.write(msg);
  }

  // Broadcast notification to all connected clients
  broadcast(method, params) {
    for (const conn of this.connections) {
      this.sendNotification(conn, method, params);
    }
  }
}

// --- Test helpers ---

function tmpSocket() {
  return path.join(
    os.tmpdir(),
    `froth-test-${process.pid}-${Date.now()}.sock`
  );
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// --- Tests ---

async function main() {
  process.stdout.write("\n=== Froth daemon-client smoke tests ===\n\n");

  // -------------------------------------------------------
  process.stdout.write("## Connection\n\n");
  // -------------------------------------------------------

  await test("connect to mock daemon", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      assert(client.isConnected, "should be connected");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("connect to nonexistent socket rejects", async () => {
    const client = new DaemonClient();
    const err = await assertRejects(
      client.connect("/tmp/froth-does-not-exist.sock"),
      "should reject"
    );
    assert(err instanceof Error, "should be Error");
    assert(!client.isConnected, "should not be connected");
    client.dispose();
  });

  await test("connect after dispose rejects", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      client.dispose();
      const err = await assertRejects(
        client.connect(sock),
        "should reject after dispose"
      );
      assert(err.message === "client disposed", "wrong error: " + err.message);
    } finally {
      await daemon.stop();
    }
  });

  await test("onClose fires when daemon drops connection", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();

    const client = new DaemonClient();
    let closeFired = false;
    client.onClose(() => {
      closeFired = true;
    });
    await client.connect(sock);

    // Kill daemon (closes all connections)
    await daemon.stop();
    await sleep(50);

    assert(closeFired, "onClose should have fired");
    assert(!client.isConnected, "should not be connected after close");
    client.dispose();
  });

  await test("double dispose is safe", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      client.dispose();
      client.dispose(); // should not throw
    } finally {
      await daemon.stop();
    }
  });

  // -------------------------------------------------------
  process.stdout.write("\n## RPC methods\n\n");
  // -------------------------------------------------------

  await test("hello returns device info", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.hello();
      assertEq(result.cell_bits, 32, "cell_bits");
      assertEq(result.max_payload, 240, "max_payload");
      assertEq(result.heap_size, 4096, "heap_size");
      assertEq(result.heap_used, 708, "heap_used");
      assertEq(result.slot_count, 64, "slot_count");
      assertEq(result.version, "0.1.0-test", "version");
      assertEq(result.board, "mock-board", "board");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval success with stack result", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.eval("1 2 +");
      assertEq(result.status, 0, "status");
      assertEq(result.stack_repr, "[3]", "stack_repr");
      assert(result.error_code === undefined, "no error_code on success");
      assert(result.fault_word === undefined, "no fault_word on success");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval error returns structured result", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.eval("bad");
      assertEq(result.status, 1, "status");
      assertEq(result.error_code, 3, "error_code");
      assertEq(result.fault_word, "bad", "fault_word");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval with undefined word", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.eval("nosuchword");
      assertEq(result.status, 1, "status");
      assertEq(result.error_code, 5, "error_code");
      assertEq(result.fault_word, "nosuchword", "fault_word");
      // stack_repr should be undefined (omitempty in Go)
      assert(
        result.stack_repr === undefined,
        "stack_repr should be absent: " + result.stack_repr
      );
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval empty source succeeds", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.eval("");
      assertEq(result.status, 0, "status");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("info returns heap/slot data", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.info();
      assertEq(result.heap_size, 4096, "heap_size");
      assertEq(result.heap_used, 708, "heap_used");
      assertEq(result.heap_overlay_used, 20, "heap_overlay_used");
      assertEq(result.slot_count, 64, "slot_count");
      assertEq(result.slot_overlay_count, 3, "slot_overlay_count");
      assertEq(result.version, "0.1.0-test", "version");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("status with device connected", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.status();
      assertEq(result.running, true, "running");
      assertEq(result.connected, true, "connected");
      assertEq(result.port, "/dev/ttyUSB0", "port");
      assert(result.device !== undefined, "device present");
      assertEq(result.device.board, "mock-board", "device.board");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("status with device disconnected", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    daemon.deviceConnected = false;
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.status();
      assertEq(result.running, true, "running");
      assertEq(result.connected, false, "connected");
      assert(
        result.device === undefined,
        "device absent when disconnected"
      );
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  // -------------------------------------------------------
  process.stdout.write("\n## Error handling\n\n");
  // -------------------------------------------------------

  await test("hello when device not connected throws DaemonClientError", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    daemon.deviceConnected = false;
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const err = await assertRejects(client.hello(), "should reject");
      assert(err instanceof DaemonClientError, "should be DaemonClientError");
      assert(err.isNotConnected, "should be not-connected error");
      assertEq(err.code, -32001, "error code");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval when device not connected throws DaemonClientError", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    daemon.deviceConnected = false;
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const err = await assertRejects(
        client.eval("1 2 +"),
        "should reject"
      );
      assert(err instanceof DaemonClientError, "should be DaemonClientError");
      assert(err.isNotConnected, "should be not-connected error");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("call on disposed client rejects", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      client.dispose();
      const err = await assertRejects(client.hello(), "should reject");
      assert(
        err.message === "not connected",
        "wrong error: " + err.message
      );
    } finally {
      await daemon.stop();
    }
  });

  await test("pending requests rejected on connection drop", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    daemon.evalDelay = 5000; // delay response so we can kill the connection
    await daemon.start();

    const client = new DaemonClient();
    await client.connect(sock);

    const evalPromise = client.eval("1 2 +");

    // Kill daemon while eval is pending
    await sleep(20);
    await daemon.stop();

    const err = await assertRejects(evalPromise, "should reject on drop");
    assert(err instanceof Error, "should be Error");
    client.dispose();
  });

  await test("unknown RPC method returns error", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      // Call a method that doesn't exist. We need to reach into the
      // private call method. The compiled JS exposes it indirectly
      // through the class, but we can test via an explicit prototype call.
      // Instead, let's just verify the daemon handles it correctly by
      // checking that our real methods work (covered above).
      // Skip this: no public API for arbitrary method calls.
      skipped++;
      process.stdout.write("  skip unknown RPC method (no public API)\n");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  // -------------------------------------------------------
  process.stdout.write("\n## Notifications\n\n");
  // -------------------------------------------------------

  await test("console notification during eval", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    daemon.consoleTextDuringEval = "hello from device\n";
    await daemon.start();
    try {
      const client = new DaemonClient();
      const notifications = [];
      client.onNotification((n) => notifications.push(n));
      await client.connect(sock);

      await client.eval("1 2 +");
      await sleep(10); // let notification propagate

      assert(notifications.length >= 1, "should receive notification");
      assertEq(notifications[0].method, "console", "notification method");
      assertEq(
        notifications[0].params.text,
        "hello from device\n",
        "console text"
      );
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("connected notification", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      const notifications = [];
      client.onNotification((n) => notifications.push(n));
      await client.connect(sock);

      daemon.broadcast("connected", {
        device: {
          cell_bits: 32,
          max_payload: 240,
          heap_size: 4096,
          heap_used: 708,
          slot_count: 64,
          version: "0.1.0-test",
          board: "mock-board",
        },
        port: "/dev/ttyUSB0",
      });
      await sleep(20);

      assert(notifications.length >= 1, "should receive connected event");
      assertEq(notifications[0].method, "connected", "event method");
      assertEq(
        notifications[0].params.device.board,
        "mock-board",
        "device board"
      );
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("disconnected notification (no params)", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      const notifications = [];
      client.onNotification((n) => notifications.push(n));
      await client.connect(sock);

      // Go daemon sends disconnected with nil params (omitted from JSON)
      for (const conn of daemon.connections) {
        conn.write(
          JSON.stringify({ jsonrpc: "2.0", method: "disconnected" }) + "\n"
        );
      }
      await sleep(20);

      assert(notifications.length >= 1, "should receive disconnected event");
      assertEq(notifications[0].method, "disconnected", "event method");
      // params should be undefined (omitted from JSON)
      assert(
        notifications[0].params === undefined,
        "params should be undefined"
      );
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("reconnecting notification (no params)", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      const notifications = [];
      client.onNotification((n) => notifications.push(n));
      await client.connect(sock);

      for (const conn of daemon.connections) {
        conn.write(
          JSON.stringify({ jsonrpc: "2.0", method: "reconnecting" }) + "\n"
        );
      }
      await sleep(20);

      assert(
        notifications.length >= 1,
        "should receive reconnecting event"
      );
      assertEq(notifications[0].method, "reconnecting", "event method");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("multiple rapid console notifications", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      const texts = [];
      client.onNotification((n) => {
        if (n.method === "console") texts.push(n.params.text);
      });
      await client.connect(sock);

      // Blast 100 console notifications
      for (let i = 0; i < 100; i++) {
        daemon.broadcast("console", { text: `line ${i}\n` });
      }
      await sleep(100);

      assertEq(texts.length, 100, "should receive all 100 notifications");
      assertEq(texts[0], "line 0\n", "first line");
      assertEq(texts[99], "line 99\n", "last line");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  // -------------------------------------------------------
  process.stdout.write("\n## Concurrent requests\n\n");
  // -------------------------------------------------------

  await test("multiple concurrent evals resolve correctly", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);

      // Fire 10 evals concurrently with different operands
      const promises = [];
      for (let i = 0; i < 10; i++) {
        promises.push(client.eval(`${i} ${i} +`));
      }

      const results = await Promise.all(promises);

      for (let i = 0; i < 10; i++) {
        assertEq(results[i].status, 0, `eval ${i} status`);
        assertEq(
          results[i].stack_repr,
          `[${i + i}]`,
          `eval ${i} stack_repr`
        );
      }
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval interleaved with info", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);

      const [evalResult, infoResult, helloResult] = await Promise.all([
        client.eval("3 4 +"),
        client.info(),
        client.hello(),
      ]);

      assertEq(evalResult.stack_repr, "[7]", "eval result");
      assertEq(infoResult.heap_size, 4096, "info heap_size");
      assertEq(helloResult.board, "mock-board", "hello board");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  // -------------------------------------------------------
  process.stdout.write("\n## Edge cases\n\n");
  // -------------------------------------------------------

  await test("eval with special characters", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);

      // These should all reach the daemon without corruption
      const sources = [
        '"Hello\\nWorld" s.emit',
        ': test 1 + ;',
        "'foo [ 1 2 + ] def",
        "0xFF 0b1010 +",
        "( a comment ) 42",
      ];

      for (const src of sources) {
        const result = await client.eval(src);
        assertEq(result.status, 0, `eval "${src}" status`);
      }
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval with unicode", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);
      const result = await client.eval('"Hej v\u00e4rlden" s.emit');
      assertEq(result.status, 0, "unicode eval status");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval with newlines in source", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);

      // Multi-line source (send-file scenario)
      const multiline = ": double\n  dup +\n;\n5 double";
      const result = await client.eval(multiline);
      assertEq(result.status, 0, "multiline eval status");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("eval with very long source", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      await client.connect(sock);

      // 10KB of source (well within JSON limits, tests buffering)
      const longSource = "1 ".repeat(5000) + "drop";
      const result = await client.eval(longSource);
      assertEq(result.status, 0, "long eval status");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  await test("rapid connect/dispose cycles", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      for (let i = 0; i < 20; i++) {
        const client = new DaemonClient();
        await client.connect(sock);
        assert(client.isConnected, `cycle ${i} connected`);
        client.dispose();
        assert(!client.isConnected, `cycle ${i} disposed`);
      }
    } finally {
      await daemon.stop();
    }
  });

  await test("notification storm does not block RPC", async () => {
    const sock = tmpSocket();
    const daemon = new MockDaemon(sock);
    await daemon.start();
    try {
      const client = new DaemonClient();
      let consoleCount = 0;
      client.onNotification((n) => {
        if (n.method === "console") consoleCount++;
      });
      await client.connect(sock);

      // Blast notifications while also doing an eval
      for (let i = 0; i < 50; i++) {
        daemon.broadcast("console", { text: `noise ${i}\n` });
      }

      // Eval should still complete despite notification storm
      const result = await client.eval("5 5 +");
      assertEq(result.stack_repr, "[10]", "eval during storm");
      assert(consoleCount > 0, "received some notifications");
      client.dispose();
    } finally {
      await daemon.stop();
    }
  });

  // -------------------------------------------------------
  process.stdout.write("\n## Wire format verification\n\n");
  // -------------------------------------------------------

  await test("request wire format matches rpc.go", async () => {
    const sock = tmpSocket();
    let capturedRequest = null;

    // Raw server to capture the exact bytes
    const server = net.createServer((conn) => {
      let buf = "";
      conn.on("data", (chunk) => {
        buf += chunk.toString("utf-8");
        const idx = buf.indexOf("\n");
        if (idx !== -1) {
          capturedRequest = JSON.parse(buf.slice(0, idx));
          // Send a valid response
          conn.write(
            JSON.stringify({
              jsonrpc: "2.0",
              result: { running: true, connected: false },
              id: capturedRequest.id,
            }) + "\n"
          );
        }
      });
    });

    await new Promise((resolve) => {
      try { fs.unlinkSync(sock); } catch { /* ignore */ }
      server.listen(sock, resolve);
    });

    try {
      const client = new DaemonClient();
      await client.connect(sock);
      await client.status();
      client.dispose();

      // Verify the request matches rpc.go's rpcRequest struct
      assert(capturedRequest !== null, "should capture request");
      assertEq(capturedRequest.jsonrpc, "2.0", "jsonrpc field");
      assertEq(capturedRequest.method, "status", "method field");
      assert(typeof capturedRequest.id === "number", "id is number");
      assert(capturedRequest.id > 0, "id is positive");
      // status has no params, so params should be absent
      assert(
        capturedRequest.params === undefined,
        "no params for status"
      );
    } finally {
      await new Promise((resolve) => server.close(resolve));
      try { fs.unlinkSync(sock); } catch { /* ignore */ }
    }
  });

  await test("eval request includes params.source", async () => {
    const sock = tmpSocket();
    let capturedRequest = null;

    const server = net.createServer((conn) => {
      let buf = "";
      conn.on("data", (chunk) => {
        buf += chunk.toString("utf-8");
        const idx = buf.indexOf("\n");
        if (idx !== -1) {
          capturedRequest = JSON.parse(buf.slice(0, idx));
          conn.write(
            JSON.stringify({
              jsonrpc: "2.0",
              result: { status: 0, stack_repr: "[3]" },
              id: capturedRequest.id,
            }) + "\n"
          );
        }
      });
    });

    await new Promise((resolve) => {
      try { fs.unlinkSync(sock); } catch { /* ignore */ }
      server.listen(sock, resolve);
    });

    try {
      const client = new DaemonClient();
      await client.connect(sock);
      await client.eval("1 2 +");
      client.dispose();

      assert(capturedRequest !== null, "should capture request");
      assertEq(capturedRequest.method, "eval", "method");
      assert(capturedRequest.params !== undefined, "params present");
      assertEq(capturedRequest.params.source, "1 2 +", "params.source");
    } finally {
      await new Promise((resolve) => server.close(resolve));
      try { fs.unlinkSync(sock); } catch { /* ignore */ }
    }
  });

  await test("request IDs are sequential and unique", async () => {
    const sock = tmpSocket();
    const capturedIds = [];

    const server = net.createServer((conn) => {
      let buf = "";
      conn.on("data", (chunk) => {
        buf += chunk.toString("utf-8");
        let idx;
        while ((idx = buf.indexOf("\n")) !== -1) {
          const line = buf.slice(0, idx);
          buf = buf.slice(idx + 1);
          const req = JSON.parse(line);
          capturedIds.push(req.id);
          conn.write(
            JSON.stringify({
              jsonrpc: "2.0",
              result: { running: true, connected: false },
              id: req.id,
            }) + "\n"
          );
        }
      });
    });

    await new Promise((resolve) => {
      try { fs.unlinkSync(sock); } catch { /* ignore */ }
      server.listen(sock, resolve);
    });

    try {
      const client = new DaemonClient();
      await client.connect(sock);
      await client.status();
      await client.status();
      await client.status();
      client.dispose();

      assertEq(capturedIds.length, 3, "three requests");
      assertEq(capturedIds[0], 1, "first id");
      assertEq(capturedIds[1], 2, "second id");
      assertEq(capturedIds[2], 3, "third id");
    } finally {
      await new Promise((resolve) => server.close(resolve));
      try { fs.unlinkSync(sock); } catch { /* ignore */ }
    }
  });

  // -------------------------------------------------------
  // Summary
  // -------------------------------------------------------

  process.stdout.write("\n=== Results ===\n");
  process.stdout.write(
    `${passed} passed, ${failed} failed, ${skipped} skipped\n`
  );

  if (failures.length > 0) {
    process.stdout.write("\nFailures:\n");
    for (const { name, err } of failures) {
      process.stdout.write(`  ${name}: ${err.message}\n`);
      if (err.stack) {
        const lines = err.stack.split("\n").slice(1, 4);
        for (const line of lines) {
          process.stdout.write(`    ${line.trim()}\n`);
        }
      }
    }
  }

  process.exit(failed > 0 ? 1 : 0);
}

main().catch((err) => {
  process.stderr.write(`Fatal: ${err.message}\n${err.stack}\n`);
  process.exit(2);
});

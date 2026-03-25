package cmd

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
)

var (
	localRuntimeOnce sync.Once
	localRuntimePath string
	localRuntimeErr  error
)

func TestDaemonBackgroundLocalHappyPath(t *testing.T) {
	repoRoot := repoRoot(t)
	runtimePath := ensureLocalRuntime(t, repoRoot)

	cliPath := buildCLI(t, repoRoot)
	home := shortHome(t)

	pid := startBackgroundDaemon(t, cliPath, home, runtimePath)
	defer stopDaemonBestEffort(t, cliPath, home)

	waitForOutput(
		t,
		5*time.Second,
		func() (string, error) {
			return runCLI(cliPath, home, "daemon", "status")
		},
		func(out string) bool {
			return strings.Contains(out, fmt.Sprintf("pid: %d", pid)) &&
				strings.Contains(out, "target: local") &&
				strings.Contains(out, "daemon: 0.1.0 (api 2)") &&
				strings.Contains(out, "device: 0.1.0 on posix (32-bit)")
		},
	)

	sendOut, err := runCLI(cliPath, home, "send", "1 2 +", "--daemon")
	if err != nil {
		t.Fatalf("send failed: %v\n%s", err, sendOut)
	}
	if strings.TrimSpace(sendOut) != "[3]" {
		t.Fatalf("unexpected send output: %q", sendOut)
	}

	resetOut, err := runCLI(cliPath, home, "reset", "--daemon")
	if err != nil {
		t.Fatalf("reset failed: %v\n%s", err, resetOut)
	}
	if !strings.Contains(resetOut, "Reset result: OK [0]") {
		t.Fatalf("unexpected reset output:\n%s", resetOut)
	}

	againOut, err := runCLI(cliPath, home, "daemon", "start", "--background", "--local", "--local-runtime", runtimePath)
	if err != nil {
		t.Fatalf("second start failed: %v\n%s", err, againOut)
	}
	if !strings.Contains(againOut, fmt.Sprintf("daemon already running (pid %d)", pid)) {
		t.Fatalf("unexpected second-start output:\n%s", againOut)
	}

	stopDaemon(t, cliPath, home)
	assertNotRunning(t, cliPath, home)
}

func TestDaemonBackgroundReadyBeforeHandshake(t *testing.T) {
	repoRoot := repoRoot(t)
	cliPath := buildCLI(t, repoRoot)
	home := shortHome(t)
	runtimePath := fakeRuntime(t)

	started := time.Now()
	pid := startBackgroundDaemon(t, cliPath, home, runtimePath)
	defer stopDaemonBestEffort(t, cliPath, home)

	if elapsed := time.Since(started); elapsed > 3*time.Second {
		t.Fatalf("background start waited for handshake: %v", elapsed)
	}

	status := waitForOutput(
		t,
		2*time.Second,
		func() (string, error) {
			return runCLI(cliPath, home, "daemon", "status")
		},
		func(out string) bool {
			return strings.Contains(out, fmt.Sprintf("pid: %d", pid)) &&
				strings.Contains(out, "target: local")
		},
	)
	if strings.Contains(status, "device: 0.1.0 on posix (32-bit)") {
		t.Fatalf("fake runtime unexpectedly connected:\n%s", status)
	}

	infoOut, err := runCLI(cliPath, home, "info", "--daemon")
	if err == nil {
		t.Fatalf("info unexpectedly succeeded:\n%s", infoOut)
	}
	if !strings.Contains(infoOut, "device not connected") {
		t.Fatalf("unexpected info failure:\n%s", infoOut)
	}
}

func TestDaemonStopPIDGuard(t *testing.T) {
	repoRoot := repoRoot(t)
	runtimePath := ensureLocalRuntime(t, repoRoot)
	cliPath := buildCLI(t, repoRoot)
	home := shortHome(t)

	pid := startBackgroundDaemon(t, cliPath, home, runtimePath)
	defer stopDaemonBestEffort(t, cliPath, home)

	out, err := runCLI(cliPath, home, "daemon", "stop", "--pid", strconv.Itoa(pid+1))
	if err != nil {
		t.Fatalf("guarded stop failed: %v\n%s", err, out)
	}
	if !strings.Contains(out, "daemon pid changed") {
		t.Fatalf("unexpected guarded-stop output:\n%s", out)
	}

	statusOut, err := runCLI(cliPath, home, "daemon", "status")
	if err != nil {
		t.Fatalf("status after guarded stop failed: %v\n%s", err, statusOut)
	}
	if !strings.Contains(statusOut, fmt.Sprintf("pid: %d", pid)) {
		t.Fatalf("daemon stopped unexpectedly:\n%s", statusOut)
	}

	stopDaemon(t, cliPath, home)
	assertNotRunning(t, cliPath, home)
}

func TestDaemonChunkedEvalThroughLocalRuntime(t *testing.T) {
	repoRoot := repoRoot(t)
	runtimePath := ensureLocalRuntime(t, repoRoot)
	cliPath := buildCLI(t, repoRoot)
	home := shortHome(t)

	_ = startBackgroundDaemon(t, cliPath, home, runtimePath)
	defer stopDaemonBestEffort(t, cliPath, home)

	waitForOutput(
		t,
		5*time.Second,
		func() (string, error) {
			return runCLI(cliPath, home, "daemon", "status")
		},
		func(out string) bool {
			return strings.Contains(out, "device: 0.1.0 on posix (32-bit)")
		},
	)

	source := strings.Repeat("\\ [ : ; ]\n0 drop\n", 16) +
		strings.Repeat("\"[ : ; ]\" drop\n", 8) +
		"1 2 +"

	out, err := runCLI(cliPath, home, "send", source, "--daemon")
	if err != nil {
		t.Fatalf("chunked send failed: %v\n%s", err, out)
	}
	if strings.TrimSpace(out) != "[3]" {
		t.Fatalf("unexpected chunked send output: %q", out)
	}
}

func TestLiveInfoRoundTrip(t *testing.T) {
	_, home := startConnectedDaemon(t)

	client, err := daemon.DialPath(daemonSocketPath(home))
	if err != nil {
		t.Fatalf("dial daemon: %v", err)
	}
	defer client.Close()

	info, err := client.Info()
	if err != nil {
		t.Fatalf("info failed: %v", err)
	}
	if info.Version != "0.1.0" {
		t.Fatalf("version = %q, want %q", info.Version, "0.1.0")
	}
	if info.HeapSize <= 0 || info.SlotCount <= 0 {
		t.Fatalf("unexpected info result: %#v", info)
	}
}

func TestLiveEvalWithOutputData(t *testing.T) {
	cliPath, home := startConnectedDaemon(t)

	out, err := runCLI(cliPath, home, "send", ": foo 42 . ; foo", "--daemon")
	if err != nil {
		t.Fatalf("send failed: %v\n%s", err, out)
	}
	if !strings.Contains(out, "42 ") {
		t.Fatalf("missing OUTPUT_DATA content in output:\n%s", out)
	}
}

func TestLiveResetThenEval(t *testing.T) {
	cliPath, home := startConnectedDaemon(t)

	out1, err := runCLI(cliPath, home, "send", ": myword 99 ;", "--daemon")
	if err != nil {
		t.Fatalf("first send failed: %v\n%s", err, out1)
	}

	resetOut, err := runCLI(cliPath, home, "reset", "--daemon")
	if err != nil {
		t.Fatalf("reset failed: %v\n%s", err, resetOut)
	}
	if !strings.Contains(resetOut, "Reset result: OK") {
		t.Fatalf("unexpected reset output:\n%s", resetOut)
	}

	out2, err := runCLI(cliPath, home, "send", ": fresh 77 ; fresh", "--daemon")
	if err != nil {
		t.Fatalf("post-reset send failed: %v\n%s", err, out2)
	}
	if !strings.Contains(out2, "[77]") {
		t.Fatalf("unexpected post-reset eval result:\n%s", out2)
	}
}

func TestLiveEvalDangerousResetClearsStack(t *testing.T) {
	cliPath, home := startConnectedDaemon(t)

	out1, err := runCLI(cliPath, home, "send", "1 2 +", "--daemon")
	if err != nil {
		t.Fatalf("seed send failed: %v\n%s", err, out1)
	}
	if !strings.Contains(out1, "[3]") {
		t.Fatalf("unexpected seed eval result:\n%s", out1)
	}

	resetOut, err := runCLI(cliPath, home, "send", "dangerous-reset", "--daemon")
	if err != nil {
		t.Fatalf("dangerous-reset send failed: %v\n%s", err, resetOut)
	}
	if strings.Contains(resetOut, "error(20)") {
		t.Fatalf("dangerous-reset should suppress reset sentinel output:\n%s", resetOut)
	}

	out2, err := runCLI(cliPath, home, "send", "8 9 +", "--daemon")
	if err != nil {
		t.Fatalf("post-reset send failed: %v\n%s", err, out2)
	}
	if strings.TrimSpace(out2) != "[17]" {
		t.Fatalf("unexpected post-reset eval result:\n%s", out2)
	}
}

func TestLiveInterruptDuringEval(t *testing.T) {
	_, home := startConnectedDaemon(t)

	evalClient, err := daemon.DialPath(daemonSocketPath(home))
	if err != nil {
		t.Fatalf("dial daemon: %v", err)
	}
	defer evalClient.Close()

	controlClient, err := daemon.DialPath(daemonSocketPath(home))
	if err != nil {
		t.Fatalf("dial control client: %v", err)
	}
	defer controlClient.Close()

	evalRunning := make(chan struct{}, 1)
	evalClient.EventHandler = func(method string, params json.RawMessage) {
		if method != daemon.EventConsole {
			return
		}
		var evt daemon.ConsoleEvent
		if err := json.Unmarshal(params, &evt); err != nil {
			return
		}
		if strings.Contains(string(evt.Data), "42") {
			select {
			case evalRunning <- struct{}{}:
			default:
			}
		}
	}

	type evalResult struct {
		result *daemon.EvalResult
		err    error
	}

	ch := make(chan evalResult, 1)
	go func() {
		result, err := evalClient.Eval("42 . cr [ -1 ] [ ] while")
		ch <- evalResult{result: result, err: err}
	}()

	select {
	case <-evalRunning:
	case <-time.After(5 * time.Second):
		t.Fatal("eval did not start producing output within 5s")
	}

	if err := controlClient.Interrupt(); err != nil {
		t.Fatalf("interrupt failed: %v", err)
	}

	select {
	case got := <-ch:
		if got.err != nil {
			if !strings.Contains(got.err.Error(), "interrupted") {
				t.Fatalf("unexpected eval error: %v", got.err)
			}
			return
		}
		if got.result == nil {
			t.Fatal("eval returned nil result")
		}
		if got.result.Status == 0 {
			t.Fatalf("eval succeeded despite interrupt: %#v", got.result)
		}
		if got.result.ErrorCode != 14 {
			t.Fatalf("error code = %d, want 14", got.result.ErrorCode)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("eval did not return after interrupt")
	}
}

func TestLiveKeyInputWaitRoundTrip(t *testing.T) {
	_, home := startConnectedDaemon(t)

	evalClient, err := daemon.DialPath(daemonSocketPath(home))
	if err != nil {
		t.Fatalf("dial daemon: %v", err)
	}
	defer evalClient.Close()

	inputClient, err := daemon.DialPath(daemonSocketPath(home))
	if err != nil {
		t.Fatalf("dial input client: %v", err)
	}
	defer inputClient.Close()

	var (
		consoleOutput strings.Builder
		consoleMu     sync.Mutex
		sendOnce      sync.Once
	)

	evalClient.EventHandler = func(method string, params json.RawMessage) {
		switch method {
		case daemon.EventConsole:
			var evt daemon.ConsoleEvent
			if err := json.Unmarshal(params, &evt); err == nil {
				consoleMu.Lock()
				consoleOutput.Write(evt.Data)
				consoleMu.Unlock()
			}
		case daemon.EventInputWait:
			var evt daemon.InputWaitEvent
			if err := json.Unmarshal(params, &evt); err != nil {
				t.Errorf("decode input wait failed: %v", err)
				return
			}
			sendOnce.Do(func() {
				go func() {
					if err := inputClient.SendInput([]byte("A"), evt.Seq); err != nil {
						t.Errorf("SendInput failed: %v", err)
					}
				}()
			})
		}
	}

	result, err := evalClient.Eval("key .")
	if err != nil {
		t.Fatalf("eval failed: %v", err)
	}
	if result == nil || result.Status != 0 {
		t.Fatalf("unexpected eval result: %#v err=%v", result, err)
	}

	consoleMu.Lock()
	output := consoleOutput.String()
	consoleMu.Unlock()
	if !strings.Contains(output, "65 ") {
		t.Fatalf("missing key output in console stream:\n%s", output)
	}
}

func TestDaemonLocalRuntimeValidation(t *testing.T) {
	repoRoot := repoRoot(t)
	cliPath := buildCLI(t, repoRoot)
	home := shortHome(t)

	out, err := runCLI(cliPath, home, "daemon", "start", "--local-runtime", "/tmp/nope")
	if err == nil {
		t.Fatalf("expected --local-runtime validation failure:\n%s", out)
	}
	if !strings.Contains(out, "--local-runtime requires --local") {
		t.Fatalf("unexpected validation output:\n%s", out)
	}

	out, err = runCLI(cliPath, home, "daemon", "start", "--local", "--local-runtime", "/tmp/nope")
	if err == nil {
		t.Fatalf("expected missing runtime failure:\n%s", out)
	}
	if !strings.Contains(out, "local Froth binary not found at /tmp/nope") {
		t.Fatalf("unexpected missing-runtime output:\n%s", out)
	}
}

func repoRoot(t *testing.T) string {
	t.Helper()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	return filepath.Clean(filepath.Join(wd, "..", "..", ".."))
}

func daemonSocketPath(home string) string {
	return filepath.Join(home, ".froth", "daemon.sock")
}

func startConnectedDaemon(t *testing.T) (cliPath string, home string) {
	t.Helper()

	repoRoot := repoRoot(t)
	runtimePath := ensureLocalRuntime(t, repoRoot)
	cliPath = buildCLI(t, repoRoot)
	home = shortHome(t)

	_ = startBackgroundDaemon(t, cliPath, home, runtimePath)
	t.Cleanup(func() {
		stopDaemonBestEffort(t, cliPath, home)
	})

	waitForOutput(
		t,
		5*time.Second,
		func() (string, error) {
			return runCLI(cliPath, home, "daemon", "status")
		},
		func(out string) bool {
			return strings.Contains(out, "device: 0.1.0 on posix (32-bit)")
		},
	)

	return cliPath, home
}

func ensureLocalRuntime(t *testing.T, repoRoot string) string {
	t.Helper()

	localRuntimeOnce.Do(func() {
		buildDir := filepath.Join(repoRoot, "build64")
		localRuntimePath = filepath.Join(buildDir, "Froth")

		configure := exec.Command("cmake", "-S", repoRoot, "-B", buildDir, "-DFROTH_HAS_LIVE=ON")
		configure.Dir = repoRoot
		if output, err := configure.CombinedOutput(); err != nil {
			localRuntimeErr = fmt.Errorf("configure local runtime: %w\n%s", err, output)
			return
		}

		build := exec.Command("cmake", "--build", buildDir)
		build.Dir = repoRoot
		if output, err := build.CombinedOutput(); err != nil {
			localRuntimeErr = fmt.Errorf("build local runtime: %w\n%s", err, output)
			return
		}

		if _, err := os.Stat(localRuntimePath); err != nil {
			localRuntimeErr = fmt.Errorf("local runtime missing after build: %w", err)
		}
	})

	if localRuntimeErr != nil {
		t.Fatal(localRuntimeErr)
	}

	return localRuntimePath
}

func buildCLI(t *testing.T, repoRoot string) string {
	t.Helper()

	binPath := filepath.Join(t.TempDir(), "froth-cli")
	cmd := exec.Command("go", "build", "-o", binPath, ".")
	cmd.Dir = filepath.Join(repoRoot, "tools", "cli")
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("build cli: %v\n%s", err, output)
	}
	return binPath
}

func runCLI(cliPath string, home string, args ...string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, cliPath, args...)
	cmd.Env = testEnv(home)

	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out

	err := cmd.Run()
	if ctx.Err() != nil {
		return out.String(), ctx.Err()
	}
	return out.String(), err
}

func testEnv(home string) []string {
	env := os.Environ()
	env = append(env, "HOME="+home)
	return env
}

func startBackgroundDaemon(t *testing.T, cliPath string, home string, runtimePath string) int {
	t.Helper()

	out, err := runCLI(
		cliPath,
		home,
		"daemon",
		"start",
		"--background",
		"--local",
		"--local-runtime",
		runtimePath,
	)
	if err != nil {
		t.Fatalf("background start failed: %v\n%s", err, out)
	}

	pid, err := strconv.Atoi(strings.TrimSpace(out))
	if err != nil {
		t.Fatalf("parse daemon pid from %q: %v", out, err)
	}
	return pid
}

func stopDaemon(t *testing.T, cliPath string, home string) {
	t.Helper()

	out, err := runCLI(cliPath, home, "daemon", "stop")
	if err != nil {
		t.Fatalf("daemon stop failed: %v\n%s", err, out)
	}
	if !strings.Contains(out, "daemon stopping") {
		t.Fatalf("unexpected stop output:\n%s", out)
	}
}

func stopDaemonBestEffort(t *testing.T, cliPath string, home string) {
	t.Helper()
	_, _ = runCLI(cliPath, home, "daemon", "stop")
}

func assertNotRunning(t *testing.T, cliPath string, home string) {
	t.Helper()

	waitForOutput(
		t,
		5*time.Second,
		func() (string, error) {
			return runCLI(cliPath, home, "daemon", "status")
		},
		func(out string) bool {
			return strings.Contains(out, "daemon: not running")
		},
	)
}

func waitForOutput(
	t *testing.T,
	timeout time.Duration,
	fn func() (string, error),
	match func(string) bool,
) string {
	t.Helper()

	deadline := time.Now().Add(timeout)
	var last string
	for time.Now().Before(deadline) {
		out, err := fn()
		last = out
		if err == nil && match(out) {
			return out
		}
		time.Sleep(100 * time.Millisecond)
	}

	t.Fatalf("timed out waiting for output match, last output:\n%s", last)
	return ""
}

func fakeRuntime(t *testing.T) string {
	t.Helper()

	path := filepath.Join(t.TempDir(), "fake-froth")
	script := "#!/bin/sh\ntrap 'exit 0' TERM INT\nwhile :; do sleep 1; done\n"
	if err := os.WriteFile(path, []byte(script), 0755); err != nil {
		t.Fatalf("write fake runtime: %v", err)
	}
	return path
}

func shortHome(t *testing.T) string {
	t.Helper()

	home := filepath.Join(os.TempDir(), fmt.Sprintf("froth-home-%d", time.Now().UnixNano()))
	if err := os.MkdirAll(home, 0755); err != nil {
		t.Fatalf("mkdir short home: %v", err)
	}
	t.Cleanup(func() {
		_ = os.RemoveAll(home)
	})
	return home
}

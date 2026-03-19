package cmd

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func TestDaemonBackgroundLocalHappyPath(t *testing.T) {
	repoRoot := repoRoot(t)
	runtimePath := filepath.Join(repoRoot, "build64", "Froth")
	if _, err := os.Stat(runtimePath); err != nil {
		t.Fatalf("local runtime missing: %v", err)
	}

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
				strings.Contains(out, "daemon: 0.1.0 (api 1)") &&
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
	runtimePath := filepath.Join(repoRoot, "build64", "Froth")
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
	runtimePath := filepath.Join(repoRoot, "build64", "Froth")
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

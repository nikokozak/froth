package daemon

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestFindLocalBinaryExplicitPath(t *testing.T) {
	dir := t.TempDir()
	binaryPath := filepath.Join(dir, "Froth")
	if err := os.WriteFile(binaryPath, []byte("#!/bin/sh\n"), 0755); err != nil {
		t.Fatalf("write binary: %v", err)
	}

	got, err := findLocalBinary(binaryPath)
	if err != nil {
		t.Fatalf("findLocalBinary failed: %v", err)
	}
	if got != binaryPath {
		t.Fatalf("unexpected binary path: %s", got)
	}
}

func TestPrepareSocketPathRemovesStaleSocket(t *testing.T) {
	socketPath := shortSocketPath(t)

	ln, err := net.Listen("unix", socketPath)
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	ln.Close()
	defer os.Remove(socketPath)

	d := New("", false, "")
	d.socketPath = socketPath

	if err := d.prepareSocketPath(); err != nil {
		t.Fatalf("prepareSocketPath failed: %v", err)
	}
	if _, err := os.Stat(socketPath); !os.IsNotExist(err) {
		t.Fatalf("expected stale socket to be removed, stat err=%v", err)
	}
}

func TestPrepareSocketPathRejectsHealthyDaemon(t *testing.T) {
	socketPath := shortSocketPath(t)

	ln, err := net.Listen("unix", socketPath)
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer ln.Close()
	defer os.Remove(socketPath)

	done := make(chan struct{})
	go serveStatusOnce(t, ln, done)
	defer close(done)

	d := New("", false, "")
	d.socketPath = socketPath

	err = d.prepareSocketPath()
	if err != ErrAlreadyRunning {
		t.Fatalf("expected ErrAlreadyRunning, got %v", err)
	}
}

func serveStatusOnce(t *testing.T, ln net.Listener, done <-chan struct{}) {
	t.Helper()

	conn, err := ln.Accept()
	if err != nil {
		select {
		case <-done:
			return
		default:
			t.Errorf("accept: %v", err)
			return
		}
	}
	defer conn.Close()

	scanner := bufio.NewScanner(conn)
	if !scanner.Scan() {
		return
	}

	_, _ = fmt.Fprintf(
		conn,
		"{\"jsonrpc\":\"2.0\",\"result\":{\"pid\":123,\"api_version\":1,\"daemon_version\":\"0.1.0\",\"running\":true,\"connected\":false,\"reconnecting\":false,\"target\":\"serial\"},\"id\":1}\n",
	)
}

func shortSocketPath(t *testing.T) string {
	t.Helper()
	return filepath.Join(os.TempDir(), fmt.Sprintf("froth-%d.sock", time.Now().UnixNano()))
}

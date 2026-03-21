package cmd

import (
	"bufio"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	daemonpkg "github.com/nikokozak/froth/tools/cli/internal/daemon"
)

func resetCommandGlobals(t *testing.T) {
	t.Helper()

	oldPort := portFlag
	oldTarget := targetFlag
	oldSerial := serialFlag
	oldDaemon := daemonFlag
	oldClean := cleanFlag

	portFlag = ""
	targetFlag = ""
	serialFlag = false
	daemonFlag = false
	cleanFlag = false

	t.Cleanup(func() {
		portFlag = oldPort
		targetFlag = oldTarget
		serialFlag = oldSerial
		daemonFlag = oldDaemon
		cleanFlag = oldClean
	})
}

func withChdir(t *testing.T, dir string) {
	t.Helper()

	oldWD, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatalf("chdir %s: %v", dir, err)
	}
	t.Cleanup(func() {
		if err := os.Chdir(oldWD); err != nil {
			t.Fatalf("restore cwd: %v", err)
		}
	})
}

func captureOutput(t *testing.T, fn func()) (string, string) {
	t.Helper()

	oldStdout := os.Stdout
	oldStderr := os.Stderr

	stdoutR, stdoutW, err := os.Pipe()
	if err != nil {
		t.Fatalf("stdout pipe: %v", err)
	}
	stderrR, stderrW, err := os.Pipe()
	if err != nil {
		t.Fatalf("stderr pipe: %v", err)
	}

	os.Stdout = stdoutW
	os.Stderr = stderrW

	var stdoutBuf strings.Builder
	var stderrBuf strings.Builder
	var wg sync.WaitGroup
	wg.Add(2)

	go func() {
		defer wg.Done()
		_, _ = io.Copy(&stdoutBuf, stdoutR)
	}()
	go func() {
		defer wg.Done()
		_, _ = io.Copy(&stderrBuf, stderrR)
	}()

	fn()

	_ = stdoutW.Close()
	_ = stderrW.Close()
	os.Stdout = oldStdout
	os.Stderr = oldStderr
	wg.Wait()

	return stdoutBuf.String(), stderrBuf.String()
}

func mustWriteFile(t *testing.T, path string, content string) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func mustWriteExecutable(t *testing.T, path string, content string) {
	t.Helper()

	mustWriteFile(t, path, content)
	if err := os.Chmod(path, 0755); err != nil {
		t.Fatalf("chmod %s: %v", path, err)
	}
}

func prependPath(t *testing.T, dir string) {
	t.Helper()

	oldPath := os.Getenv("PATH")
	t.Setenv("PATH", dir+string(os.PathListSeparator)+oldPath)
}

func startFakeDaemon(t *testing.T) (<-chan string, func()) {
	t.Helper()

	home, err := os.MkdirTemp("/tmp", "froth-home-")
	if err != nil {
		t.Fatalf("mkdirtemp /tmp: %v", err)
	}
	t.Setenv("FROTH_HOME", home)
	if err := os.MkdirAll(home, 0755); err != nil {
		t.Fatalf("mkdir FROTH_HOME: %v", err)
	}

	socketPath := daemonpkg.SocketPath()
	if err := os.MkdirAll(filepath.Dir(socketPath), 0755); err != nil {
		t.Fatalf("mkdir socket dir: %v", err)
	}

	ln, err := net.Listen("unix", socketPath)
	if err != nil {
		t.Fatalf("listen %s: %v", socketPath, err)
	}

	sourceCh := make(chan string, 4)
	done := make(chan struct{})

	go func() {
		defer close(done)
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			go handleFakeDaemonConn(conn, sourceCh)
		}
	}()

	cleanup := func() {
		_ = ln.Close()
		_ = os.Remove(socketPath)
		_ = os.RemoveAll(home)
		<-done
	}
	return sourceCh, cleanup
}

func handleFakeDaemonConn(conn net.Conn, sourceCh chan<- string) {
	defer conn.Close()

	scanner := bufio.NewScanner(conn)
	enc := json.NewEncoder(conn)

	for scanner.Scan() {
		var req struct {
			JSONRPC string          `json:"jsonrpc"`
			Method  string          `json:"method"`
			Params  json.RawMessage `json:"params"`
			ID      interface{}     `json:"id"`
		}
		if err := json.Unmarshal(scanner.Bytes(), &req); err != nil {
			_ = enc.Encode(map[string]interface{}{
				"jsonrpc": "2.0",
				"error": map[string]interface{}{
					"code":    -32700,
					"message": "parse error",
				},
				"id": nil,
			})
			continue
		}

		switch req.Method {
		case "eval":
			var params struct {
				Source string `json:"source"`
			}
			if err := json.Unmarshal(req.Params, &params); err != nil {
				_ = enc.Encode(map[string]interface{}{
					"jsonrpc": "2.0",
					"error": map[string]interface{}{
						"code":    -32600,
						"message": "invalid params",
					},
					"id": req.ID,
				})
				continue
			}
			sourceCh <- params.Source
			_ = enc.Encode(map[string]interface{}{
				"jsonrpc": "2.0",
				"result": map[string]interface{}{
					"status":     0,
					"stack_repr": "[]",
				},
				"id": req.ID,
			})
		default:
			_ = enc.Encode(map[string]interface{}{
				"jsonrpc": "2.0",
				"error": map[string]interface{}{
					"code":    -32601,
					"message": "unknown method: " + req.Method,
				},
				"id": req.ID,
			})
		}
	}
}

func writeFakeBuildTools(t *testing.T, dir string, logPath string) {
	t.Helper()

	mustWriteExecutable(t, filepath.Join(dir, "cmake"), "#!/bin/sh\nprintf 'cmake %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
	mustWriteExecutable(t, filepath.Join(dir, "make"), "#!/bin/sh\nprintf 'make %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
}

func mustReadFile(t *testing.T, path string) string {
	t.Helper()

	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(data)
}

package daemon

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
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

func TestTransportReadLoopReturnsToConsoleModeAfterFrame(t *testing.T) {
	wire, err := protocol.EncodeWireFrame(0, protocol.EvalRes, 7, []byte("ok"))
	if err != nil {
		t.Fatalf("encode frame: %v", err)
	}

	stream := append([]byte("pre\n"), wire...)
	stream = append(stream, []byte("post\n")...)

	d := New("", false, "")
	d.conn = &fakeTransport{reads: bytesAsReads(stream)}
	d.disconnectCh = make(chan struct{})
	d.clients = make(map[*rpcConn]struct{})

	client := &rpcConn{
		notifyCh: make(chan *rpcNotification, 8),
		done:     make(chan struct{}),
	}
	d.clients[client] = struct{}{}

	d.wg.Add(1)
	go d.transportReadLoop()

	first := readNotification(t, client.notifyCh)
	second := readNotification(t, client.notifyCh)

	if first.Method != EventConsole {
		t.Fatalf("first notification = %#v, want console", first)
	}
	firstEvt, ok := first.Params.(*ConsoleEvent)
	if !ok || firstEvt.Text != "pre\n" {
		t.Fatalf("first console = %#v, want %q", first.Params, "pre\n")
	}

	if second.Method != EventConsole {
		t.Fatalf("second notification = %#v, want console", second)
	}
	secondEvt, ok := second.Params.(*ConsoleEvent)
	if !ok || secondEvt.Text != "post\n" {
		t.Fatalf("second console = %#v, want %q", second.Params, "post\n")
	}

	close(d.done)
	d.wg.Wait()
	close(client.done)
}

func TestDeviceInterruptCancelsInterruptibleWaiter(t *testing.T) {
	conn := &fakeTransport{}
	d := New("", false, "")
	d.conn = conn
	d.disconnectCh = make(chan struct{})
	d.done = make(chan struct{})

	ch := d.registerWaiter(42, protocol.EvalReq, true)

	resultCh := make(chan error, 1)
	go func() {
		_, _, err := d.waitResponse(ch, 0)
		resultCh <- err
	}()

	if err := d.deviceInterrupt(); err != nil {
		t.Fatalf("deviceInterrupt: %v", err)
	}

	select {
	case err := <-resultCh:
		if !errors.Is(err, ErrEvalInterrupted) {
			t.Fatalf("waitResponse error = %v, want %v", err, ErrEvalInterrupted)
		}
	case <-time.After(200 * time.Millisecond):
		t.Fatal("waitResponse did not unblock after interrupt")
	}

	if got := conn.writtenBytes(); len(got) != 1 || got[0] != 0x03 {
		t.Fatalf("interrupt write = %v, want [3]", got)
	}
}

func TestSuspiciousConsoleSummaryFlagsBinaryBurstWithWaiterContext(t *testing.T) {
	summary, ok := suspiciousConsoleSummary(
		[]byte{0x9d, 0x4c, 0xff, 0x01, 0x02},
		interruptibleWaiter{messageType: protocol.InfoReq, seq: 7},
	)
	if !ok {
		t.Fatal("suspiciousConsoleSummary returned ok=false, want true")
	}
	if !containsAll(summary, "suspicious binary burst", "INFO_REQ seq=7", "9d 4c ff 01 02") {
		t.Fatalf("summary = %q", summary)
	}
}

func TestSuspiciousConsoleSummaryIgnoresNormalText(t *testing.T) {
	if summary, ok := suspiciousConsoleSummary([]byte("froth> ok\n"), interruptibleWaiter{}); ok {
		t.Fatalf("summary = %q, want no diagnostic", summary)
	}
}

func TestRecoverConsoleFrameDeliversLeakedInfoResponse(t *testing.T) {
	payload := make([]byte, 0, 32)
	payload = append(payload,
		0x00, 0x10, 0x00, 0x00, // heap_size = 4096
		0x03, 0x00, 0x00, 0x00, // heap_used = 3
		0x00, 0x00, 0x00, 0x00, // heap_overlay_used = 0
		0x48, 0x00, // slot_count = 72
		0x01, 0x00, // slot_overlay_count = 1
		0x00,             // flags = 0
		0x05, 0x00,       // version length = 5
		'0', '.', '1', '.', '0', // version
	)

	wire, err := protocol.EncodeWireFrame(0, protocol.InfoRes, 3, payload)
	if err != nil {
		t.Fatalf("encode frame: %v", err)
	}
	leaked := wire[1 : len(wire)-1] // Missing the opening/closing delimiters.

	d := New("", false, "")
	ch := d.registerWaiter(3, protocol.InfoReq, false)

	if !d.recoverConsoleFrame(leaked) {
		t.Fatal("recoverConsoleFrame returned false, want true")
	}

	select {
	case resp := <-ch:
		if resp.header == nil {
			t.Fatal("recovered response header is nil")
		}
		if resp.header.MessageType != protocol.InfoRes {
			t.Fatalf("message type = 0x%02x, want INFO_RES", resp.header.MessageType)
		}
		if resp.header.Seq != 3 {
			t.Fatalf("seq = %d, want 3", resp.header.Seq)
		}
		if len(resp.payload) != len(payload) {
			t.Fatalf("payload len = %d, want %d", len(resp.payload), len(payload))
		}
	case <-time.After(200 * time.Millisecond):
		t.Fatal("timed out waiting for recovered frame delivery")
	}
}

func containsAll(s string, needles ...string) bool {
	for _, needle := range needles {
		if !strings.Contains(s, needle) {
			return false
		}
	}
	return true
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

type fakeTransport struct {
	mu      sync.Mutex
	reads   [][]byte
	writes  []byte
	readErr error
	closed  bool
}

func (f *fakeTransport) Read(buf []byte) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if len(f.reads) == 0 {
		if f.readErr != nil {
			return 0, f.readErr
		}
		return 0, io.EOF
	}

	chunk := f.reads[0]
	f.reads = f.reads[1:]
	copy(buf, chunk)
	return len(chunk), nil
}

func (f *fakeTransport) Write(data []byte) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.writes = append(f.writes, data...)
	return nil
}

func (f *fakeTransport) Close() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.closed = true
	return nil
}

func (f *fakeTransport) Path() string { return "fake" }

func (f *fakeTransport) SetReadTimeout(d time.Duration) error { return nil }

func (f *fakeTransport) ResetInputBuffer() {}

func (f *fakeTransport) Drain(duration time.Duration) {}

func (f *fakeTransport) writtenBytes() []byte {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]byte, len(f.writes))
	copy(out, f.writes)
	return out
}

func bytesAsReads(data []byte) [][]byte {
	reads := make([][]byte, 0, len(data))
	for _, b := range data {
		reads = append(reads, []byte{b})
	}
	return reads
}

func readNotification(t *testing.T, ch <-chan *rpcNotification) *rpcNotification {
	t.Helper()
	select {
	case n := <-ch:
		if n == nil {
			t.Fatal("notification channel delivered nil")
		}
		return n
	case <-time.After(200 * time.Millisecond):
		t.Fatal("timed out waiting for notification")
		return nil
	}
}

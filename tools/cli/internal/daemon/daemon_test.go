package daemon

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
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

func TestTransportReadLoopDeliversOutputDataAsConsole(t *testing.T) {
	sessionID := uint64(0x1234)
	wire := mustEncodeWireFrame(t, sessionID, protocol.OutputData, 7, protocol.BuildInputDataPayload([]byte("hello\n")))

	d := newTestDaemon()
	d.conn = &fakeTransport{reads: bytesAsReads(wire)}
	d.setSessionState(true, sessionID, 7)

	client := &rpcConn{
		notifyCh: make(chan *rpcNotification, 8),
		done:     make(chan struct{}),
	}
	d.clients[client] = struct{}{}
	d.beginActiveEval(7, client)
	defer d.endActiveEval()

	d.wg.Add(1)
	go d.transportReadLoop()

	n := readNotification(t, client.notifyCh)
	if n.Method != EventConsole {
		t.Fatalf("notification method = %q, want %q", n.Method, EventConsole)
	}
	event, ok := n.Params.(*ConsoleEvent)
	if !ok {
		t.Fatalf("notification params = %#v, want *ConsoleEvent", n.Params)
	}
	if string(event.Data) != "hello\n" {
		t.Fatalf("console data = %q, want %q", string(event.Data), "hello\n")
	}

	close(d.done)
	d.wg.Wait()
	close(client.done)
}

func TestTransportReadLoopDeliversResponseToWaiter(t *testing.T) {
	sessionID := uint64(0x2233)
	wire := mustEncodeWireFrame(t, sessionID, protocol.EvalRes, 7, []byte("ok"))

	d := newTestDaemon()
	d.conn = &fakeTransport{reads: bytesAsReads(wire)}
	d.setSessionState(true, sessionID, 0)

	ch := d.registerWaiter(7, protocol.EvalRes, false)

	d.wg.Add(1)
	go d.transportReadLoop()

	select {
	case resp := <-ch:
		if resp.header == nil {
			t.Fatal("response header is nil")
		}
		if resp.header.MessageType != protocol.EvalRes {
			t.Fatalf("message type = 0x%02x, want EVAL_RES", resp.header.MessageType)
		}
		if resp.header.Seq != 7 {
			t.Fatalf("seq = %d, want 7", resp.header.Seq)
		}
	case <-time.After(200 * time.Millisecond):
		t.Fatal("timed out waiting for waiter delivery")
	}

	close(d.done)
	d.wg.Wait()
}

func TestTransportReadLoopDiscardsMismatchedSessionID(t *testing.T) {
	wire := mustEncodeWireFrame(t, 0x9999, protocol.InfoRes, 3, []byte("bad"))

	d := newTestDaemon()
	d.conn = &fakeTransport{reads: bytesAsReads(wire)}
	d.setSessionState(true, 0x1111, 0)

	ch := d.registerWaiter(3, protocol.InfoRes, false)

	d.wg.Add(1)
	go d.transportReadLoop()

	select {
	case resp := <-ch:
		t.Fatalf("unexpected delivery: %#v", resp)
	case <-time.After(150 * time.Millisecond):
	}

	close(d.done)
	d.wg.Wait()
}

func TestTransportReadLoopRejectsUnexpectedResponseTypeForWaiter(t *testing.T) {
	sessionID := uint64(0x4455)
	wire := mustEncodeWireFrame(t, sessionID, protocol.InfoRes, 7, []byte("bad"))

	d := newTestDaemon()
	d.conn = &fakeTransport{reads: bytesAsReads(wire)}
	d.setSessionState(true, sessionID, 0)

	ch := d.registerWaiter(7, protocol.EvalRes, false)

	d.wg.Add(1)
	go d.transportReadLoop()

	select {
	case resp := <-ch:
		t.Fatalf("unexpected delivery: %#v", resp)
	case <-time.After(150 * time.Millisecond):
	}

	close(d.done)
	d.wg.Wait()
}

func TestInterruptCancelDoesNotLeakAcrossEvals(t *testing.T) {
	oldTimeout := interruptCancelTimeout
	interruptCancelTimeout = 20 * time.Millisecond
	defer func() { interruptCancelTimeout = oldTimeout }()

	d := newTestDaemon()
	d.conn = &fakeTransport{}
	d.setSessionState(true, 0x4455, 0)

	d.beginActiveEval(1, nil)
	if err := d.deviceInterrupt(); err != nil {
		t.Fatalf("deviceInterrupt: %v", err)
	}
	d.endActiveEval()

	d.beginActiveEval(2, nil)
	ch := d.registerWaiter(2, protocol.EvalRes, true)
	defer d.clearWaiter()
	defer d.endActiveEval()

	select {
	case resp := <-ch:
		t.Fatalf("unexpected waiter cancellation: %#v", resp)
	case <-time.After(100 * time.Millisecond):
	}
}

func TestHandleFrameDropsNonFrameBytesInDirectMode(t *testing.T) {
	d := newTestDaemon()
	d.conn = &fakeTransport{reads: bytesAsReads([]byte("froth> hi\n"))}

	client := &rpcConn{
		notifyCh: make(chan *rpcNotification, 8),
		done:     make(chan struct{}),
	}
	d.clients[client] = struct{}{}

	d.wg.Add(1)
	go d.transportReadLoop()

	select {
	case n := <-client.notifyCh:
		t.Fatalf("unexpected notification: %#v", n)
	case <-time.After(150 * time.Millisecond):
	}

	close(d.done)
	d.wg.Wait()
	close(client.done)
}

func TestAttachSendsCorrectFrame(t *testing.T) {
	conn := &fakeTransport{}
	d := newTestDaemon()
	d.conn = conn

	conn.onWrite = func(data []byte) {
		frames := decodeWireFrames(t, data)
		for _, frame := range frames {
			if frame.header.MessageType != protocol.AttachReq {
				continue
			}
			resp := mustEncodeWireFrame(t, frame.header.SessionID, protocol.AttachRes, 0, []byte{protocol.AttachStatusOK})
			conn.queueReadBytes(resp)
		}
	}

	d.wg.Add(1)
	go d.transportReadLoop()

	d.reqMu.Lock()
	err := d.attach()
	d.reqMu.Unlock()
	if err != nil {
		t.Fatalf("attach: %v", err)
	}

	frames := decodeWireFrames(t, conn.writtenBytes())
	if len(frames) == 0 {
		t.Fatal("no frames written during attach")
	}
	frame := frames[0]
	if frame.header.MessageType != protocol.AttachReq {
		t.Fatalf("message type = 0x%02x, want ATTACH_REQ", frame.header.MessageType)
	}
	if frame.header.Seq != 0 {
		t.Fatalf("seq = %d, want 0", frame.header.Seq)
	}
	if frame.header.SessionID == 0 {
		t.Fatal("session ID = 0, want non-zero")
	}
	if len(frame.payload) != 0 {
		t.Fatalf("payload len = %d, want 0", len(frame.payload))
	}

	attached, sessionID, activeSeq := d.sessionSnapshot()
	if !attached {
		t.Fatal("daemon not attached")
	}
	if sessionID != frame.header.SessionID {
		t.Fatalf("session ID = %016x, want %016x", sessionID, frame.header.SessionID)
	}
	if activeSeq != 0 {
		t.Fatalf("activeSeq = %d, want 0", activeSeq)
	}
	if d.nextSeq != 1 {
		t.Fatalf("nextSeq = %d, want 1", d.nextSeq)
	}

	close(d.done)
	d.wg.Wait()
}

func TestAttachIgnoresStaleSessionResponse(t *testing.T) {
	conn := &fakeTransport{}
	d := newTestDaemon()
	d.conn = conn

	conn.onWrite = func(data []byte) {
		frames := decodeWireFrames(t, data)
		for _, frame := range frames {
			if frame.header.MessageType != protocol.AttachReq {
				continue
			}

			stale := mustEncodeWireFrame(t, frame.header.SessionID+1, protocol.AttachRes, 0, []byte{protocol.AttachStatusOK})
			ok := mustEncodeWireFrame(t, frame.header.SessionID, protocol.AttachRes, 0, []byte{protocol.AttachStatusOK})
			conn.queueReadBytes(stale)
			conn.queueReadBytes(ok)
			return
		}
	}

	d.wg.Add(1)
	go d.transportReadLoop()

	d.reqMu.Lock()
	err := d.attach()
	d.reqMu.Unlock()
	if err != nil {
		t.Fatalf("attach: %v", err)
	}

	attached, sessionID, _ := d.sessionSnapshot()
	if !attached {
		t.Fatal("daemon not attached")
	}
	if sessionID == 0 {
		t.Fatal("session ID = 0, want non-zero")
	}

	close(d.done)
	d.wg.Wait()
}

func TestKeepaliveFramesSent(t *testing.T) {
	conn := &fakeTransport{}
	d := newTestDaemon()
	d.conn = conn

	conn.onWrite = func(data []byte) {
		frames := decodeWireFrames(t, data)
		for _, frame := range frames {
			if frame.header.MessageType != protocol.AttachReq {
				continue
			}
			resp := mustEncodeWireFrame(t, frame.header.SessionID, protocol.AttachRes, 0, []byte{protocol.AttachStatusOK})
			conn.queueReadBytes(resp)
		}
	}

	d.wg.Add(1)
	go d.transportReadLoop()

	d.reqMu.Lock()
	err := d.attach()
	d.reqMu.Unlock()
	if err != nil {
		t.Fatalf("attach: %v", err)
	}

	time.Sleep(2200 * time.Millisecond)

	attached, sessionID, _ := d.sessionSnapshot()
	if !attached {
		t.Fatal("daemon detached before keepalive test")
	}

	frames := decodeWireFrames(t, conn.writtenBytes())
	found := false
	for _, frame := range frames {
		if frame.header.MessageType != protocol.Keepalive {
			continue
		}
		if frame.header.SessionID != sessionID {
			t.Fatalf("keepalive session ID = %016x, want %016x", frame.header.SessionID, sessionID)
		}
		if frame.header.Seq != 0 {
			t.Fatalf("keepalive seq = %d, want 0", frame.header.Seq)
		}
		found = true
	}
	if !found {
		t.Fatal("no KEEPALIVE frame observed")
	}

	close(d.done)
	d.wg.Wait()
}

func TestDeviceInterruptSendsFramedInterruptReq(t *testing.T) {
	conn := &fakeTransport{}
	d := newTestDaemon()
	d.conn = conn
	d.setSessionState(true, 0x4455, 5)

	if err := d.deviceInterrupt(); err != nil {
		t.Fatalf("deviceInterrupt: %v", err)
	}

	frames := decodeWireFrames(t, conn.writtenBytes())
	if len(frames) != 1 {
		t.Fatalf("frame count = %d, want 1", len(frames))
	}
	frame := frames[0]
	if frame.header.MessageType != protocol.InterruptReq {
		t.Fatalf("message type = 0x%02x, want INTERRUPT_REQ", frame.header.MessageType)
	}
	if frame.header.SessionID != 0x4455 {
		t.Fatalf("session ID = %016x, want %016x", frame.header.SessionID, uint64(0x4455))
	}
	if frame.header.Seq != 5 {
		t.Fatalf("seq = %d, want 5", frame.header.Seq)
	}

	close(d.done)
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
		"{\"jsonrpc\":\"2.0\",\"result\":{\"pid\":123,\"api_version\":2,\"daemon_version\":\"0.1.0\",\"running\":true,\"connected\":false,\"reconnecting\":false,\"target\":\"serial\"},\"id\":1}\n",
	)
}

func shortSocketPath(t *testing.T) string {
	t.Helper()
	return filepath.Join(os.TempDir(), fmt.Sprintf("froth-%d.sock", time.Now().UnixNano()))
}

type fakeTransport struct {
	mu          sync.Mutex
	reads       [][]byte
	writes      []byte
	readErr     error
	closed      bool
	readTimeout time.Duration
	onWrite     func([]byte)
}

func (f *fakeTransport) Read(buf []byte) (int, error) {
	deadline := time.Time{}

	for {
		f.mu.Lock()
		if len(f.reads) > 0 {
			chunk := f.reads[0]
			f.reads = f.reads[1:]
			f.mu.Unlock()
			copy(buf, chunk)
			return len(chunk), nil
		}
		readErr := f.readErr
		closed := f.closed
		timeout := f.readTimeout
		f.mu.Unlock()

		if readErr != nil {
			return 0, readErr
		}
		if closed {
			return 0, io.EOF
		}
		if timeout > 0 {
			if deadline.IsZero() {
				deadline = time.Now().Add(timeout)
			}
			if time.Now().After(deadline) {
				return 0, nil
			}
		}
		time.Sleep(1 * time.Millisecond)
	}
}

func (f *fakeTransport) Write(data []byte) error {
	f.mu.Lock()
	clone := append([]byte(nil), data...)
	f.writes = append(f.writes, clone...)
	onWrite := f.onWrite
	f.mu.Unlock()

	if onWrite != nil {
		onWrite(clone)
	}
	return nil
}

func (f *fakeTransport) Close() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.closed = true
	return nil
}

func (f *fakeTransport) Path() string { return "fake" }

func (f *fakeTransport) SetReadTimeout(d time.Duration) error {
	f.mu.Lock()
	f.readTimeout = d
	f.mu.Unlock()
	return nil
}

func (f *fakeTransport) ResetInputBuffer() {}

func (f *fakeTransport) Drain(duration time.Duration) {}

func (f *fakeTransport) queueReadBytes(data []byte) {
	f.mu.Lock()
	f.reads = append(f.reads, bytesAsReads(data)...)
	f.mu.Unlock()
}

func (f *fakeTransport) writtenBytes() []byte {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]byte, len(f.writes))
	copy(out, f.writes)
	return out
}

type decodedWireFrame struct {
	header  *protocol.Header
	payload []byte
}

func decodeWireFrames(t *testing.T, data []byte) []decodedWireFrame {
	t.Helper()

	var frames []decodedWireFrame
	start := -1
	for i, b := range data {
		if b != 0x00 {
			continue
		}
		if start >= 0 && i > start {
			decoded, err := protocol.COBSDecode(data[start:i])
			if err != nil {
				t.Fatalf("COBS decode: %v", err)
			}
			header, payload, err := protocol.ParseFrame(decoded)
			if err != nil {
				t.Fatalf("parse frame: %v", err)
			}
			frames = append(frames, decodedWireFrame{header: header, payload: payload})
		}
		start = i + 1
	}
	return frames
}

func mustEncodeWireFrame(t *testing.T, sessionID uint64, msgType byte, seq uint16, payload []byte) []byte {
	t.Helper()
	wire, err := protocol.EncodeWireFrame(sessionID, msgType, seq, payload)
	if err != nil {
		t.Fatalf("encode wire frame: %v", err)
	}
	return wire
}

func newTestDaemon() *Daemon {
	d := New("", false, "")
	d.disconnectCh = make(chan struct{})
	d.clients = make(map[*rpcConn]struct{})
	d.done = make(chan struct{})
	return d
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

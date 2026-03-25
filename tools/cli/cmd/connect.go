package cmd

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
)

var connectLookPath = exec.LookPath

func runConnect(args []string) error {
	local := false

	for _, arg := range args {
		switch arg {
		case "--local":
			local = true
		default:
			return fmt.Errorf("unknown connect flag: %s", arg)
		}
	}

	if !local {
		return runConnectSerial()
	}

	if portFlag != "" {
		return fmt.Errorf("--local cannot be combined with --port")
	}

	return runConnectLocal()
}

func runConnectSerial() error {
	client, err := dialConnectDaemon()
	if err != nil {
		return err
	}
	defer client.Close()

	var outputMu sync.Mutex
	printLocked := func(format string, args ...interface{}) {
		outputMu.Lock()
		defer outputMu.Unlock()
		fmt.Printf(format, args...)
	}
	writeLocked := func(data []byte) {
		outputMu.Lock()
		defer outputMu.Unlock()
		_, _ = os.Stdout.Write(data)
	}

	disconnectCh := make(chan struct{})
	var disconnectOnce sync.Once
	signalDisconnect := func() {
		disconnectOnce.Do(func() {
			printLocked("Disconnected\n")
			close(disconnectCh)
		})
	}

	lineCh := make(chan string)
	scanErrCh := make(chan error, 1)
	go scanConnectInput(lineCh, scanErrCh)

	client.EventHandler = func(method string, params json.RawMessage) {
		switch method {
		case daemon.EventConsole:
			var evt daemon.ConsoleEvent
			if err := json.Unmarshal(params, &evt); err == nil {
				writeLocked(evt.Data)
			}
		case daemon.EventInputWait:
			var evt daemon.InputWaitEvent
			if err := json.Unmarshal(params, &evt); err == nil {
				printLocked("\n[froth] input> ")
				go func(seq int) {
					line, ok := <-lineCh
					if !ok {
						_ = client.Interrupt()
						return
					}
					if line == "\\ quit" {
						_ = client.Interrupt()
						return
					}
					data := []byte(line)
					if line == "" {
						data = []byte{'\n'}
					}
					if err := client.SendInput(data, seq); err != nil {
						printLocked("\ninput: %v\n", err)
					}
				}(evt.Seq)
			}
		case daemon.EventDisconnected:
			signalDisconnect()
		}
	}

	status, err := waitForConnectedStatus(client, 5*time.Second)
	if err != nil {
		return fmt.Errorf("status: %w", err)
	}
	if !status.Connected || status.Device == nil {
		return fmt.Errorf("device not connected")
	}

	printLocked("%s\n", formatConnectedMessage(status.Device.Board, status.Port))

	sigintCh := make(chan os.Signal, 1)
	signal.Notify(sigintCh, os.Interrupt)
	defer signal.Stop(sigintCh)

	for {
		printLocked("froth> ")

		select {
		case <-disconnectCh:
			return nil
		case <-sigintCh:
			// Ctrl-C at the prompt (no eval running). Just re-prompt.
			printLocked("\n")
			continue
		case line, ok := <-lineCh:
			if !ok {
				printLocked("\n")
				if err := <-scanErrCh; err != nil {
					return fmt.Errorf("read input: %w", err)
				}
				return nil
			}

			trimmed := strings.TrimSpace(line)
			if trimmed == "" {
				continue
			}
			if trimmed == "\\ quit" {
				return nil
			}

			// Run eval in a goroutine so we can handle Ctrl-C
			// while the RPC is blocking.
			type evalOutcome struct {
				result *daemon.EvalResult
				err    error
			}
			evalCh := make(chan evalOutcome, 1)
			go func() {
				r, e := client.Eval(line)
				evalCh <- evalOutcome{r, e}
			}()

			var evalDone bool
			for !evalDone {
				select {
				case res := <-evalCh:
					evalDone = true
					if res.err != nil {
						if isConnectDisconnectError(res.err) {
							signalDisconnect()
							return nil
						}
						printLocked("eval: %v\n", res.err)
					} else {
						printConnectEvalResult(res.result, printLocked)
					}
				case <-sigintCh:
					// Interrupt via a fresh connection. The main
					// client is blocked in Eval on its socket.
					ic, dialErr := daemon.Dial()
					if dialErr != nil {
						printLocked("interrupt: %v\n", dialErr)
						continue
					}
					if intErr := ic.Interrupt(); intErr != nil {
						printLocked("interrupt: %v\n", intErr)
					}
					ic.Close()
					// Wait for evalCh — the eval should unwind.
				case <-disconnectCh:
					return nil
				}
			}
		}
	}
}

func dialConnectDaemon() (*daemon.Client, error) {
	client, err := daemon.Dial()
	if err == nil {
		return client, nil
	}

	if startErr := startConnectDaemon(); startErr != nil {
		return nil, fmt.Errorf("connect to daemon: %w; start it with 'froth daemon start --background'", err)
	}

	client, err = daemon.Dial()
	if err != nil {
		return nil, fmt.Errorf("connect to daemon after start: %w", err)
	}

	return client, nil
}

func startConnectDaemon() error {
	exe, err := os.Executable()
	if err != nil {
		return err
	}

	args := make([]string, 0, 5)
	if portFlag != "" {
		args = append(args, "--port", portFlag)
	}
	args = append(args, "daemon", "start", "--background")

	cmd := exec.Command(exe, args...)
	output, err := cmd.CombinedOutput()
	if err == nil {
		return nil
	}

	text := strings.TrimSpace(string(output))
	if text == "" {
		return err
	}

	return fmt.Errorf("%w\n%s", err, text)
}

func waitForConnectedStatus(client *daemon.Client, timeout time.Duration) (*daemon.StatusResult, error) {
	deadline := time.Now().Add(timeout)

	for {
		status, err := client.Status()
		if err != nil {
			return nil, err
		}
		if status.Connected && status.Device != nil {
			return status, nil
		}
		if time.Now().After(deadline) {
			return status, nil
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func scanConnectInput(lineCh chan<- string, errCh chan<- error) {
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Buffer(make([]byte, 0, 1024), 1024*1024)

	for scanner.Scan() {
		lineCh <- scanner.Text()
	}

	errCh <- scanner.Err()
	close(lineCh)
}

func printConnectEvalResult(result *daemon.EvalResult, printLocked func(string, ...interface{})) {
	if result.Status == 0 || result.ErrorCode == frothErrorReset {
		if result.StackRepr != "" {
			printLocked("%s\n", result.StackRepr)
		}
		return
	}

	msg := fmt.Sprintf("error(%d)", result.ErrorCode)
	if result.FaultWord != "" {
		msg += fmt.Sprintf(" in \"%s\"", result.FaultWord)
	}
	printLocked("%s\n", msg)
}

func formatConnectedMessage(board string, port string) string {
	if board == "" && port == "" {
		return "Connected"
	}
	if board == "" {
		return fmt.Sprintf("Connected on %s", port)
	}
	if port == "" {
		return fmt.Sprintf("Connected to %s", board)
	}
	return fmt.Sprintf("Connected to %s on %s", board, port)
}

func isConnectDisconnectError(err error) bool {
	if err == nil {
		return false
	}

	text := err.Error()
	return strings.Contains(text, "connection closed") ||
		strings.Contains(text, "broken pipe") ||
		strings.Contains(text, "connection reset by peer")
}

func runConnectLocal() error {
	kernelRoot, err := findKernelRoot()
	if err != nil {
		return err
	}

	buildDir, binaryPath, err := localConnectPaths()
	if err != nil {
		return err
	}

	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return fmt.Errorf("create local build dir: %w", err)
	}

	needsBuild, err := localBinaryNeedsBuild(binaryPath, kernelRoot)
	if err != nil {
		return err
	}
	if needsBuild {
		if err := buildLocalConnectBinary(buildDir, kernelRoot); err != nil {
			return err
		}
	}

	if _, err := os.Stat(binaryPath); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("local Froth binary missing after build: %s", binaryPath)
		}
		return fmt.Errorf("stat local Froth binary: %w", err)
	}

	return syscall.Exec(binaryPath, []string{binaryPath}, os.Environ())
}

func localConnectPaths() (string, string, error) {
	home, err := sdk.FrothHome()
	if err != nil {
		return "", "", err
	}

	buildDir := filepath.Join(home, "local-build")
	return buildDir, filepath.Join(buildDir, "Froth"), nil
}

func buildLocalConnectBinary(buildDir string, kernelRoot string) error {
	cmakePath, err := connectLookPath("cmake")
	if err != nil {
		return fmt.Errorf("cmake is required for 'froth connect --local'; install CMake and try again")
	}

	makePath, err := connectLookPath("make")
	if err != nil {
		return fmt.Errorf("make is required for 'froth connect --local'; install make (Xcode Command Line Tools on macOS, build-essential on Linux) and try again")
	}

	if err := runQuietBuildCommand(buildDir, cmakePath,
		kernelRoot,
		"-DFROTH_CELL_SIZE_BITS=32",
		"-DFROTH_BOARD=posix",
		"-DFROTH_PLATFORM=posix",
	); err != nil {
		return fmt.Errorf("cmake configure failed: %w", err)
	}

	makeArgs := []string{}
	if jobs := runtime.NumCPU(); jobs > 1 {
		makeArgs = append(makeArgs, fmt.Sprintf("-j%d", jobs))
	}
	if err := runQuietBuildCommand(buildDir, makePath, makeArgs...); err != nil {
		return fmt.Errorf("make failed: %w", err)
	}

	return nil
}

func runQuietBuildCommand(dir string, name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir

	output, err := cmd.CombinedOutput()
	if err == nil {
		return nil
	}

	text := strings.TrimSpace(string(output))
	if text == "" {
		return err
	}

	return fmt.Errorf("%w\n%s", err, text)
}

func localBinaryNeedsBuild(binaryPath string, kernelRoot string) (bool, error) {
	binaryInfo, err := os.Stat(binaryPath)
	if err != nil {
		if os.IsNotExist(err) {
			return true, nil
		}
		return false, fmt.Errorf("stat local Froth binary: %w", err)
	}

	latestInput, err := latestLocalBuildInputModTime(kernelRoot)
	if err != nil {
		return false, err
	}

	return binaryInfo.ModTime().Before(latestInput), nil
}

func latestLocalBuildInputModTime(kernelRoot string) (time.Time, error) {
	paths := []string{
		"CMakeLists.txt",
		"boards",
		"cmake",
		"platforms",
		"src",
	}

	var latest time.Time
	for _, rel := range paths {
		path := filepath.Join(kernelRoot, rel)
		info, err := os.Stat(path)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return time.Time{}, fmt.Errorf("stat %s: %w", path, err)
		}

		if info.IsDir() {
			err = filepath.Walk(path, func(walkPath string, walkInfo os.FileInfo, walkErr error) error {
				if walkErr != nil {
					return walkErr
				}
				if walkInfo.IsDir() {
					return nil
				}
				if !walkInfo.Mode().IsRegular() {
					return nil
				}
				if walkInfo.ModTime().After(latest) {
					latest = walkInfo.ModTime()
				}
				return nil
			})
			if err != nil {
				return time.Time{}, fmt.Errorf("walk %s: %w", path, err)
			}
			continue
		}

		if info.Mode().IsRegular() && info.ModTime().After(latest) {
			latest = info.ModTime()
		}
	}

	if latest.IsZero() {
		return time.Time{}, fmt.Errorf("no local POSIX build inputs found under %s", kernelRoot)
	}

	return latest, nil
}

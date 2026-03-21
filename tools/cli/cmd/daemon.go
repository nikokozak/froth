package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
)

func runDaemon(args []string) error {
	if len(args) == 0 {
		fmt.Println("Usage: froth daemon <start|stop|status>")
		return nil
	}

	switch args[0] {
	case "start":
		return runDaemonStart(args[1:])
	case "stop":
		return runDaemonStop(args[1:])
	case "status":
		return runDaemonStatus()
	default:
		return fmt.Errorf("unknown daemon command: %s", args[0])
	}
}

func runDaemonStart(args []string) error {
	background := false
	local := false
	localRuntimePath := ""

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--background":
			background = true
		case "--local":
			local = true
		case "--local-runtime":
			if i+1 >= len(args) {
				return fmt.Errorf("--local-runtime requires a path")
			}
			localRuntimePath = args[i+1]
			i++
		default:
			return fmt.Errorf("unknown daemon start flag: %s", args[i])
		}
	}

	if local && portFlag != "" {
		return fmt.Errorf("--local cannot be combined with --port")
	}
	if localRuntimePath != "" && !local {
		return fmt.Errorf("--local-runtime requires --local")
	}

	if status, err := healthyDaemonStatus(); err == nil && status.Running {
		fmt.Printf("daemon already running (pid %d)\n", status.PID)
		return nil
	}

	if background {
		return startDaemonInBackground(local, localRuntimePath)
	}

	d := daemon.New(portFlag, local, localRuntimePath)
	return d.Start()
}

func startDaemonInBackground(local bool, localRuntimePath string) error {
	exe, err := os.Executable()
	if err != nil {
		return fmt.Errorf("resolve executable: %w", err)
	}

	home, err := sdk.FrothHome()
	if err != nil {
		return err
	}

	frothDir := home
	if err := os.MkdirAll(frothDir, 0755); err != nil {
		return fmt.Errorf("create %s: %w", frothDir, err)
	}

	logPath := filepath.Join(frothDir, "daemon.log")
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return fmt.Errorf("open log file: %w", err)
	}
	defer logFile.Close()

	devNull, err := os.OpenFile(os.DevNull, os.O_RDONLY, 0)
	if err != nil {
		return fmt.Errorf("open %s: %w", os.DevNull, err)
	}
	defer devNull.Close()

	childArgs := make([]string, 0, 6)
	if portFlag != "" {
		childArgs = append(childArgs, "--port", portFlag)
	}
	childArgs = append(childArgs, "daemon", "start")
	if local {
		childArgs = append(childArgs, "--local")
	}
	if localRuntimePath != "" {
		childArgs = append(childArgs, "--local-runtime", localRuntimePath)
	}

	cmd := exec.Command(exe, childArgs...)
	cmd.Stdin = devNull
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start background daemon: %w", err)
	}

	childPID := cmd.Process.Pid

	if err := waitForDaemonReady(5*time.Second, childPID); err != nil {
		_ = cmd.Process.Kill()
		return err
	}

	if err := cmd.Process.Release(); err != nil {
		return fmt.Errorf("release background daemon: %w", err)
	}

	fmt.Printf("%d\n", childPID)
	return nil
}

func waitForDaemonReady(timeout time.Duration, pid int) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		status, err := healthyDaemonStatus()
		if err == nil && status.Running && status.PID == pid {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("daemon failed to become ready")
}

func healthyDaemonStatus() (*daemon.StatusResult, error) {
	client, err := daemon.Dial()
	if err != nil {
		return nil, err
	}
	defer client.Close()

	return client.Status()
}

func runDaemonStop(args []string) error {
	expectedPID := 0
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--pid":
			if i+1 >= len(args) {
				return fmt.Errorf("--pid requires a value")
			}
			pid, err := strconv.Atoi(args[i+1])
			if err != nil {
				return fmt.Errorf("invalid pid: %s", args[i+1])
			}
			expectedPID = pid
			i++
		default:
			return fmt.Errorf("unknown daemon stop flag: %s", args[i])
		}
	}

	if _, err := healthyDaemonStatus(); err != nil {
		return fmt.Errorf("daemon not running")
	}

	data, err := os.ReadFile(daemon.PIDPath())
	if err != nil {
		return fmt.Errorf("daemon socket exists but no pid file")
	}

	pid, err := strconv.Atoi(strings.TrimSpace(string(data)))
	if err != nil {
		return fmt.Errorf("invalid pid file")
	}
	if expectedPID != 0 && pid != expectedPID {
		fmt.Printf("daemon pid changed (%d != %d); not stopping\n", pid, expectedPID)
		return nil
	}

	proc, err := os.FindProcess(pid)
	if err != nil {
		return fmt.Errorf("process not found: %w", err)
	}

	if err := proc.Signal(syscall.SIGTERM); err != nil {
		return fmt.Errorf("signal: %w", err)
	}

	fmt.Println("daemon stopping")
	return nil
}

func runDaemonStatus() error {
	client, err := daemon.Dial()
	if err != nil {
		fmt.Println("daemon: not running")
		return nil
	}
	defer client.Close()

	status, err := client.Status()
	if err != nil {
		return fmt.Errorf("status: %w", err)
	}

	fmt.Println("daemon: running")
	fmt.Printf("pid: %d\n", status.PID)
	fmt.Printf("daemon: %s (api %d)\n", status.DaemonVersion, status.APIVersion)
	fmt.Printf("target: %s\n", status.Target)
	if status.Reconnecting {
		fmt.Println("device: reconnecting")
		if status.Port != "" {
			fmt.Printf("port: %s\n", status.Port)
		}
	} else if status.Connected && status.Device != nil {
		fmt.Printf(
			"device: %s on %s (%d-bit)\n",
			status.Device.Version,
			status.Device.Board,
			status.Device.CellBits,
		)
		if status.Port != "" {
			fmt.Printf("port: %s\n", status.Port)
		}
	} else {
		fmt.Println("device: not connected")
	}

	return nil
}

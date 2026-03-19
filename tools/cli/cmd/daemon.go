package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
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
		return runDaemonStop()
	case "status":
		return runDaemonStatus()
	default:
		return fmt.Errorf("unknown daemon command: %s", args[0])
	}
}

func runDaemonStart(args []string) error {
	background := false
	local := false

	for _, arg := range args {
		switch arg {
		case "--background":
			background = true
		case "--local":
			local = true
		default:
			return fmt.Errorf("unknown daemon start flag: %s", arg)
		}
	}

	if local && portFlag != "" {
		return fmt.Errorf("--local cannot be combined with --port")
	}

	if background {
		return startDaemonInBackground(local)
	}

	d := daemon.New(portFlag, local)
	return d.Start()
}

func startDaemonInBackground(local bool) error {
	exe, err := os.Executable()
	if err != nil {
		return fmt.Errorf("resolve executable: %w", err)
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("home dir: %w", err)
	}

	frothDir := filepath.Join(home, ".froth")
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

	cmd := exec.Command(exe, childArgs...)
	cmd.Stdin = devNull
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start background daemon: %w", err)
	}

	if err := cmd.Process.Release(); err != nil {
		return fmt.Errorf("release background daemon: %w", err)
	}

	fmt.Printf("daemon started in background (log: %s)\n", logPath)
	return nil
}

func runDaemonStop() error {
	client, err := daemon.Dial()
	if err != nil {
		return fmt.Errorf("daemon not running")
	}
	client.Close()

	data, err := os.ReadFile(daemon.PIDPath())
	if err != nil {
		return fmt.Errorf("daemon socket exists but no pid file")
	}

	pid, err := strconv.Atoi(strings.TrimSpace(string(data)))
	if err != nil {
		return fmt.Errorf("invalid pid file")
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

package cmd

import (
	"fmt"
	"os"
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
		return runDaemonStart()
	case "stop":
		return runDaemonStop()
	case "status":
		return runDaemonStatus()
	default:
		return fmt.Errorf("unknown daemon command: %s", args[0])
	}
}

func runDaemonStart() error {
	d := daemon.New(portFlag)
	return d.Start()
}

func runDaemonStop() error {
	// Verify daemon is actually running by connecting to socket
	client, err := daemon.Dial()
	if err != nil {
		return fmt.Errorf("daemon not running")
	}
	client.Close()

	// Read PID and send SIGTERM
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
	if status.Connected && status.Device != nil {
		fmt.Printf("device: %s on %s (%d-bit)\n", status.Device.Version, status.Device.Board, status.Device.CellBits)
		if status.Port != "" {
			fmt.Printf("port: %s\n", status.Port)
		}
	} else {
		fmt.Println("device: not connected")
	}

	return nil
}

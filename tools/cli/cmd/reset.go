package cmd

import (
	"fmt"
	"os"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runReset() error {
	if !serialFlag {
		err := runResetDaemon()
		if err == nil {
			return nil
		}
		if daemonFlag {
			return fmt.Errorf("daemon: %w", err)
		}
	}
	return runResetSerial()
}

func runResetDaemon() error {
	client, err := daemon.Dial()
	if err != nil {
		return err
	}
	defer client.Close()

	reset, err := client.Reset()
	if err != nil {
		return err
	}

	var status string
	if reset.Status == 0 {
		status = "OK"
	} else {
		status = "ERROR"
	}
	fmt.Printf("Reset result: %s [%d]\n", status, reset.Status)
	fmt.Printf("heap: %d / %d bytes\n", reset.HeapUsed, reset.HeapSize)
	fmt.Printf("slots: %d\n", reset.SlotCount)
	return nil
}

func runResetSerial() error {
	sess, err := session.Connect(portFlag)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer sess.Close()

	sess.OutputHandler = func(data []byte) {
		_, _ = os.Stdout.Write(data)
	}

	reset, err := sess.Reset()
	if err != nil {
		return fmt.Errorf("reset: %w", err)
	}

	var status string
	if reset.Status == 0 {
		status = "OK"
	} else {
		status = "ERROR"
	}
	fmt.Printf("Reset result: %s [%d]\n", status, reset.Status)
	fmt.Printf("heap: %d / %d bytes\n", reset.HeapUsed, reset.HeapSize)
	fmt.Printf("slots: %d\n", reset.SlotCount)

	return nil
}

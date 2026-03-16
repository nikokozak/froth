package cmd

import (
	"fmt"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runInfo() error {
	if !serialFlag {
		err := runInfoDaemon()
		if err == nil {
			return nil
		}
		if daemonFlag {
			return fmt.Errorf("daemon: %w", err)
		}
	}
	return runInfoSerial()
}

func runInfoDaemon() error {
	client, err := daemon.Dial()
	if err != nil {
		return err
	}
	defer client.Close()

	info, err := client.Hello()
	if err != nil {
		return err
	}

	fmt.Printf("Froth %s on %s\n", info.Version, info.Board)
	fmt.Printf("%d-bit cells, max payload %d\n", info.CellBits, info.MaxPayload)
	fmt.Printf("heap: %d / %d bytes\n", info.HeapUsed, info.HeapSize)
	fmt.Printf("slots: %d\n", info.SlotCount)
	return nil
}

func runInfoSerial() error {
	sess, err := session.Connect(portFlag)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer sess.Close()

	info := sess.DeviceInfo()
	fmt.Printf("Froth %s on %s\n", info.Version, info.Board)
	fmt.Printf("%d-bit cells, max payload %d\n", info.CellBits, info.MaxPayload)
	fmt.Printf("heap: %d / %d bytes\n", info.HeapUsed, info.HeapSize)
	fmt.Printf("slots: %d\n", info.SlotCount)
	return nil
}

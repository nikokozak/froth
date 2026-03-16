package cmd

import (
	"fmt"

	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runInfo() error {
	// 1. Open a session (auto-detect or --port).
	//    session.Connect handles discovery and HELLO handshake.
	sess, err := session.Connect(portFlag)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer sess.Close()

	// 2. The session already has the HELLO_RES from the handshake.
	//    Print device info in a human-readable format.
	//
	// Expected output:
	//   Froth v0.1.0 on posix
	//   32-bit cells, max payload 256
	//   heap: 708 / 4096 bytes
	//   slots: 65
	info := sess.DeviceInfo()
	fmt.Printf("Froth %s on %s\n", info.Version, info.Board)
	fmt.Printf("%d-bit cells, max payload %d\n", info.CellBits, info.MaxPayload)
	fmt.Printf("heap: %d / %d bytes\n", info.HeapUsed, info.HeapSize)
	fmt.Printf("slots: %d\n", info.SlotCount)

	return nil
}

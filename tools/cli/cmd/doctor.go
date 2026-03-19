package cmd

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	serialpkg "github.com/nikokozak/froth/tools/cli/internal/serial"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runDoctor() error {
	fmt.Printf("go: %s\n", runtime.Version())

	if path, err := exec.LookPath("cmake"); err == nil {
		fmt.Printf("cmake: %s\n", path)
	} else {
		fmt.Println("cmake: not found")
	}

	if path, err := exec.LookPath("make"); err == nil {
		fmt.Printf("make: %s\n", path)
	} else {
		fmt.Println("make: not found")
	}

	candidates, err := serialpkg.ListCandidates()
	if err != nil {
		fmt.Printf("serial: error: %v\n", err)
	} else if len(candidates) == 0 {
		fmt.Println("serial: no USB-serial ports found")
	} else {
		fmt.Printf("serial: %d port(s)\n", len(candidates))
		for _, p := range candidates {
			fmt.Printf("  %s\n", p)
		}
	}

	home, _ := os.UserHomeDir()
	if home != "" {
		exportPath := filepath.Join(home, ".froth", "sdk", "esp-idf", "export.sh")
		if _, err := os.Stat(exportPath); err == nil {
			fmt.Printf("esp-idf: installed (%s)\n", filepath.Dir(exportPath))
		} else {
			fmt.Println("esp-idf: not found (run tools/setup-esp-idf.sh)")
		}
	}

	sess, err := session.Connect(portFlag)
	if err != nil {
		fmt.Println("device: not reachable")
		var discoverErr *serialpkg.DiscoverError
		if errors.As(err, &discoverErr) && discoverErr.Err != nil {
			fmt.Printf("probe: %s: %v\n", discoverErr.Path, discoverErr.Err)
		} else if portFlag != "" {
			fmt.Printf("probe: %v\n", err)
		}
	} else {
		info := sess.DeviceInfo()
		fmt.Printf("device: %s on %s (%d-bit)\n", info.Version, info.Board, info.CellBits)
		sess.Close()
	}

	return nil
}

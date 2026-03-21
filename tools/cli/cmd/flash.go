package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	serialpkg "github.com/nikokozak/froth/tools/cli/internal/serial"
)

func runFlash() error {
	root, err := findKernelRoot()
	if err != nil {
		return err
	}

	switch targetFlag {
	case "", "posix":
		fmt.Println("no flash step for POSIX target")
		fmt.Printf("binary: %s\n", filepath.Join(root, "build64", "Froth"))
		return nil
	case "esp-idf":
		return flashESPIDF(root)
	default:
		return fmt.Errorf("unknown target: %s", targetFlag)
	}
}

func flashESPIDF(root string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	targetDir := filepath.Join(root, "targets", "esp-idf")
	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("target dir not found: %s", targetDir)
	}

	port := portFlag
	if port == "" {
		candidates, err := serialpkg.ListCandidates()
		if err != nil || len(candidates) == 0 {
			return fmt.Errorf("no serial port found (use --port)")
		}
		port = candidates[0]
		fmt.Printf("detected port: %s\n", port)
	}

	if !strings.HasPrefix(port, "/dev/") {
		return fmt.Errorf("invalid port path: %s", port)
	}

	cmd := exec.Command("bash", "-c", ". \"$IDF_EXPORT\" && idf.py flash -p \"$FLASH_PORT\"")
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(), "IDF_EXPORT="+exportPath, "FLASH_PORT="+port)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py flash: %w", err)
	}

	return nil
}

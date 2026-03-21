package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/project"
	serialpkg "github.com/nikokozak/froth/tools/cli/internal/serial"
)

func runFlash() error {
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err == nil {
		return runFlashManifest(manifest, root)
	}
	if _, rootErr := project.FindProjectRoot(cwd); rootErr == nil {
		return err
	}

	return runFlashLegacy()
}

func runFlashManifest(manifest *project.Manifest, root string) error {
	fmt.Printf("Building for %s...\n", manifest.Target.Board)
	if err := runBuildManifest(manifest, root); err != nil {
		return err
	}

	switch manifest.Target.Platform {
	case "", "posix":
		fmt.Printf("binary: %s\n", filepath.Join(root, ".froth-build", "firmware", "Froth"))
		return nil
	case "esp-idf":
		port, err := resolveFlashPort()
		if err != nil {
			return err
		}
		fmt.Printf("Flashing to %s...\n", port)
		return flashESPIDFDir(filepath.Join(root, ".froth-build", "esp-idf"), port)
	default:
		return fmt.Errorf("unknown target: %s", manifest.Target.Platform)
	}
}

func runFlashLegacy() error {
	root, err := findLocalKernelRoot()
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
	targetDir := filepath.Join(root, "targets", "esp-idf")
	port, err := resolveFlashPort()
	if err != nil {
		return err
	}
	fmt.Printf("Flashing to %s...\n", port)
	return flashESPIDFDir(targetDir, port)
}

func resolveFlashPort() (string, error) {
	port := portFlag
	if port == "" {
		candidates, err := serialpkg.ListCandidates()
		if err != nil || len(candidates) == 0 {
			return "", fmt.Errorf("no serial port found (use --port)")
		}
		port = candidates[0]
		fmt.Printf("detected port: %s\n", port)
	}

	if !strings.HasPrefix(port, "/dev/") {
		return "", fmt.Errorf("invalid port path: %s", port)
	}

	return port, nil
}

func flashESPIDFDir(targetDir string, port string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("target dir not found: %s", targetDir)
	}

	args := []string{
		"-c",
		`. "$IDF_EXPORT" && exec idf.py "$@"`,
		"bash",
		"flash",
		"-p",
		port,
	}

	cmd := exec.Command("bash", args...)
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(), "IDF_EXPORT="+exportPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py flash: %w", err)
	}

	return nil
}

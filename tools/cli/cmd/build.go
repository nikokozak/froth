package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
)

func runBuild() error {
	root, err := findProjectRoot()
	if err != nil {
		return err
	}

	switch targetFlag {
	case "", "posix":
		return buildPosix(root)
	case "esp-idf":
		return buildESPIDF(root)
	default:
		return fmt.Errorf("unknown target: %s", targetFlag)
	}
}

func buildPosix(root string) error {
	buildDir := filepath.Join(root, "build64")
	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return fmt.Errorf("create build dir: %w", err)
	}

	cmake := exec.Command("cmake", "..", "-DFROTH_CELL_SIZE_BITS=32")
	cmake.Dir = buildDir
	cmake.Stdout = os.Stdout
	cmake.Stderr = os.Stderr
	if err := cmake.Run(); err != nil {
		return fmt.Errorf("cmake: %w", err)
	}

	mk := exec.Command("make")
	mk.Dir = buildDir
	mk.Stdout = os.Stdout
	mk.Stderr = os.Stderr
	if err := mk.Run(); err != nil {
		return fmt.Errorf("make: %w", err)
	}

	fmt.Printf("\nbinary: %s\n", filepath.Join(buildDir, "Froth"))
	return nil
}

func buildESPIDF(root string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	targetDir := filepath.Join(root, "targets", "esp-idf")
	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("target dir not found: %s", targetDir)
	}

	cmd := exec.Command("bash", "-c", ". \"$IDF_EXPORT\" && idf.py build")
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(), "IDF_EXPORT="+exportPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py build: %w", err)
	}

	return nil
}

// findProjectRoot walks up from CWD looking for a directory with
// both CMakeLists.txt and src/. Returns the path or an error.
func findProjectRoot() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}

	for {
		cmake := filepath.Join(dir, "CMakeLists.txt")
		srcDir := filepath.Join(dir, "src")
		if _, err := os.Stat(cmake); err == nil {
			if info, err := os.Stat(srcDir); err == nil && info.IsDir() {
				return dir, nil
			}
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("not in a Froth project (no CMakeLists.txt + src/ found)")
		}
		dir = parent
	}
}

// espIDFExportPath returns the path to ESP-IDF's export.sh, or an error
// if ESP-IDF is not installed.
func espIDFExportPath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("home dir: %w", err)
	}
	p := filepath.Join(home, ".froth", "sdk", "esp-idf", "export.sh")
	if _, err := os.Stat(p); err != nil {
		return "", fmt.Errorf("ESP-IDF not found (run tools/setup-esp-idf.sh)")
	}
	return p, nil
}

package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"

	"github.com/nikokozak/froth/tools/cli/internal/project"
)

func runBuild() error {
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	// Try manifest-driven build first
	manifest, root, err := project.Load(cwd)
	if err != nil {
		// Fall back to legacy project detection (kernel repo)
		return runBuildLegacy()
	}

	return runBuildManifest(manifest, root)
}

func runBuildManifest(manifest *project.Manifest, root string) error {
	// Resolve includes
	result, err := project.Resolve(manifest, root)
	if err != nil {
		return err
	}

	for _, w := range result.Warnings {
		fmt.Fprintf(os.Stderr, "warning: %s\n", w)
	}

	if len(result.Files) > 1 {
		fmt.Fprintf(os.Stderr, "Resolved %s (%d dependencies)\n",
			result.Files[len(result.Files)-1], len(result.Files)-1)
	}

	// Write resolved source to .froth-build/
	buildDir := filepath.Join(root, ".froth-build")
	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return fmt.Errorf("create build dir: %w", err)
	}

	resolvedPath := filepath.Join(buildDir, "resolved.froth")
	if err := os.WriteFile(resolvedPath, []byte(result.Source), 0644); err != nil {
		return fmt.Errorf("write resolved source: %w", err)
	}

	// Build based on target platform
	switch manifest.Target.Platform {
	case "posix", "":
		return buildPosixManifest(manifest, root, resolvedPath)
	case "esp-idf":
		return buildESPIDFManifest(manifest, root, resolvedPath)
	default:
		return fmt.Errorf("unknown platform: %s", manifest.Target.Platform)
	}
}

func buildPosixManifest(manifest *project.Manifest, root string, resolvedPath string) error {
	// For manifest builds, the kernel source is the SDK (or local repo).
	// For now, fall back to looking for CMakeLists.txt in the kernel repo.
	kernelRoot, err := findKernelRoot()
	if err != nil {
		return fmt.Errorf("kernel source not found: %w\n  (SDK embedding not yet implemented — run from within the kernel repo)", err)
	}

	firmwareDir := filepath.Join(root, ".froth-build", "firmware")
	if err := os.MkdirAll(firmwareDir, 0755); err != nil {
		return fmt.Errorf("create firmware dir: %w", err)
	}

	absResolved, _ := filepath.Abs(resolvedPath)

	cmakeArgs := []string{
		kernelRoot,
		fmt.Sprintf("-DFROTH_BOARD=%s", manifest.Target.Board),
		fmt.Sprintf("-DFROTH_PLATFORM=%s", manifest.Target.Platform),
		fmt.Sprintf("-DFROTH_USER_PROGRAM=%s", absResolved),
	}
	cmakeArgs = append(cmakeArgs, manifest.Build.CMakeArgs()...)

	cmake := exec.Command("cmake", cmakeArgs...)
	cmake.Dir = firmwareDir
	cmake.Stdout = os.Stdout
	cmake.Stderr = os.Stderr
	if err := cmake.Run(); err != nil {
		return fmt.Errorf("cmake: %w", err)
	}

	mk := exec.Command("make")
	mk.Dir = firmwareDir
	mk.Stdout = os.Stdout
	mk.Stderr = os.Stderr
	if err := mk.Run(); err != nil {
		return fmt.Errorf("make: %w", err)
	}

	fmt.Printf("\nFirmware ready: %s\n", filepath.Join(firmwareDir, "Froth"))
	return nil
}

func buildESPIDFManifest(manifest *project.Manifest, root string, resolvedPath string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	kernelRoot, err := findKernelRoot()
	if err != nil {
		return fmt.Errorf("kernel source not found: %w", err)
	}

	targetDir := filepath.Join(kernelRoot, "targets", "esp-idf")
	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("esp-idf target dir not found: %s", targetDir)
	}

	absResolved, _ := filepath.Abs(resolvedPath)

	// Validate board/platform are safe for shell interpolation (alphanumeric + dash + underscore)
	if !isShellSafe(manifest.Target.Board) {
		return fmt.Errorf("invalid board name: %s", manifest.Target.Board)
	}
	if !isShellSafe(manifest.Target.Platform) {
		return fmt.Errorf("invalid platform name: %s", manifest.Target.Platform)
	}

	// Build CMake defines. Board and platform are validated above.
	// Build overrides from CMakeArgs() are integer-formatted, so shell-safe.
	cmakeDefines := fmt.Sprintf(
		"-DFROTH_USER_PROGRAM=\"$FROTH_RESOLVED\" -DFROTH_BOARD=%s -DFROTH_PLATFORM=%s",
		manifest.Target.Board, manifest.Target.Platform,
	)
	for _, arg := range manifest.Build.CMakeArgs() {
		cmakeDefines += " " + arg
	}

	buildCmd := fmt.Sprintf(". \"$IDF_EXPORT\" && idf.py %s build", cmakeDefines)
	cmd := exec.Command("bash", "-c", buildCmd)
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(),
		"IDF_EXPORT="+exportPath,
		"FROTH_RESOLVED="+absResolved,
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py build: %w", err)
	}

	return nil
}

// runBuildLegacy is the old build path for when there's no froth.toml
// (building the kernel repo directly).
func runBuildLegacy() error {
	root, err := findKernelRoot()
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

// findKernelRoot walks up from CWD looking for a directory with
// both CMakeLists.txt and src/froth_vm.h. Returns the path or an error.
func findKernelRoot() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}

	for {
		cmake := filepath.Join(dir, "CMakeLists.txt")
		vm := filepath.Join(dir, "src", "froth_vm.h")
		if _, err := os.Stat(cmake); err == nil {
			if _, err := os.Stat(vm); err == nil {
				return dir, nil
			}
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("kernel source not found (no CMakeLists.txt + src/froth_vm.h)")
		}
		dir = parent
	}
}

var shellSafePattern = regexp.MustCompile(`^[a-zA-Z0-9_-]+$`)

func isShellSafe(s string) bool {
	return shellSafePattern.MatchString(s)
}

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

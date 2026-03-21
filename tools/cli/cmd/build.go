package cmd

import (
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/project"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
)

var ensureSDKRoot = sdk.EnsureSDK

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
	kernelRoot, err := findKernelRoot()
	if err != nil {
		return fmt.Errorf("kernel source not found: %w", err)
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

	absKernelRoot, err := filepath.Abs(kernelRoot)
	if err != nil {
		return fmt.Errorf("abs kernel root: %w", err)
	}

	targetDir, err := stageESPIDFTarget(kernelRoot, root)
	if err != nil {
		return err
	}

	absResolved, err := filepath.Abs(resolvedPath)
	if err != nil {
		return fmt.Errorf("abs resolved source: %w", err)
	}

	// Validate board/platform are simple identifiers before passing them to CMake.
	if !isShellSafe(manifest.Target.Board) {
		return fmt.Errorf("invalid board name: %s", manifest.Target.Board)
	}
	if !isShellSafe(manifest.Target.Platform) {
		return fmt.Errorf("invalid platform name: %s", manifest.Target.Platform)
	}

	args := []string{
		"-c",
		`. "$IDF_EXPORT" && exec idf.py "$@"`,
		"bash",
		fmt.Sprintf("-DFROTH_USER_PROGRAM=%s", absResolved),
		fmt.Sprintf("-DFROTH_BOARD=%s", manifest.Target.Board),
		fmt.Sprintf("-DFROTH_PLATFORM=%s", manifest.Target.Platform),
		fmt.Sprintf("-DFROTH_KERNEL_ROOT=%s", absKernelRoot),
	}
	args = append(args, manifest.Build.CMakeArgs()...)
	args = append(args, "build")

	cmd := exec.Command("bash", args...)
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(),
		"IDF_EXPORT="+exportPath,
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py build: %w", err)
	}

	return nil
}

func stageESPIDFTarget(kernelRoot string, projectRoot string) (string, error) {
	sourceDir := filepath.Join(kernelRoot, "targets", "esp-idf")
	if _, err := os.Stat(sourceDir); err != nil {
		return "", fmt.Errorf("esp-idf target dir not found: %s", sourceDir)
	}

	stagedDir := filepath.Join(projectRoot, ".froth-build", "esp-idf")
	stagedMainCMake := filepath.Join(stagedDir, "main", "CMakeLists.txt")
	if _, err := os.Stat(stagedMainCMake); err != nil {
		if !os.IsNotExist(err) {
			return "", fmt.Errorf("check staged esp-idf target: %w", err)
		}
		if err := copyESPIDFScaffold(sourceDir, stagedDir); err != nil {
			return "", fmt.Errorf("stage esp-idf target: %w", err)
		}
	}
	if err := patchESPIDFMainCMake(stagedDir); err != nil {
		return "", fmt.Errorf("patch staged esp-idf cmake: %w", err)
	}
	return stagedDir, nil
}

func copyESPIDFScaffold(sourceDir string, destDir string) error {
	if err := os.MkdirAll(destDir, 0755); err != nil {
		return fmt.Errorf("create staged target dir: %w", err)
	}

	return filepath.WalkDir(sourceDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}

		rel, err := filepath.Rel(sourceDir, path)
		if err != nil {
			return err
		}
		if rel == "." {
			return nil
		}

		if shouldSkipESPIDFScaffoldPath(rel) {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}

		destPath := filepath.Join(destDir, rel)
		if d.IsDir() {
			info, err := d.Info()
			if err != nil {
				return err
			}
			mode := info.Mode().Perm()
			if mode == 0 {
				mode = 0755
			}
			return os.MkdirAll(destPath, mode)
		}

		info, err := d.Info()
		if err != nil {
			return err
		}
		if !info.Mode().IsRegular() {
			return nil
		}

		data, err := os.ReadFile(path)
		if err != nil {
			return fmt.Errorf("read %s: %w", path, err)
		}
		if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
			return err
		}
		if err := os.WriteFile(destPath, data, info.Mode().Perm()); err != nil {
			return fmt.Errorf("write %s: %w", destPath, err)
		}
		return nil
	})
}

func shouldSkipESPIDFScaffoldPath(rel string) bool {
	parts := strings.Split(filepath.ToSlash(rel), "/")
	if len(parts) == 0 {
		return false
	}

	switch parts[0] {
	case ".cache", "build", "managed_components":
		return true
	}

	if len(parts) == 1 {
		switch parts[0] {
		case "sdkconfig", "sdkconfig.old", "dependencies.lock":
			return true
		}
	}

	return false
}

func patchESPIDFMainCMake(stagedDir string) error {
	mainCMakePath := filepath.Join(stagedDir, "main", "CMakeLists.txt")
	data, err := os.ReadFile(mainCMakePath)
	if err != nil {
		return err
	}

	original := `set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")`
	replacement := `if(DEFINED FROTH_KERNEL_ROOT)
    set(FROTH_ROOT "${FROTH_KERNEL_ROOT}")
else()
    set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
endif()`

	content := string(data)
	if strings.Contains(content, replacement) {
		return nil
	}
	if !strings.Contains(content, original) {
		return fmt.Errorf("expected FROTH_ROOT definition not found in %s", mainCMakePath)
	}

	updated := strings.Replace(content, original, replacement, 1)
	if updated == content {
		return nil
	}

	return os.WriteFile(mainCMakePath, []byte(updated), 0644)
}

// runBuildLegacy is the old build path for when there's no froth.toml
// (building the kernel repo directly).
func runBuildLegacy() error {
	root, err := findLocalKernelRoot()
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

func findLocalKernelRoot() (string, error) {
	startDir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}
	return findLocalKernelRootFrom(startDir)
}

func findLocalKernelRootFrom(startDir string) (string, error) {
	dir := startDir
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

// findKernelRoot walks up from CWD looking for a local kernel checkout first,
// then falls back to the extracted SDK cache.
func findKernelRoot() (string, error) {
	startDir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}
	return findKernelRootFrom(startDir)
}

func findKernelRootFrom(startDir string) (string, error) {
	if root, err := findLocalKernelRootFrom(startDir); err == nil {
		return root, nil
	}

	sdkRoot, err := ensureSDKRoot()
	if err != nil {
		return "", fmt.Errorf("kernel source not found locally and sdk extraction failed: %w", err)
	}
	return sdkRoot, nil
}

var shellSafePattern = regexp.MustCompile(`^[a-zA-Z0-9_-]+$`)

func isShellSafe(s string) bool {
	return shellSafePattern.MatchString(s)
}

func espIDFExportPath() (string, error) {
	home, err := sdk.FrothHome()
	if err != nil {
		return "", err
	}
	p := filepath.Join(home, "sdk", "esp-idf", "export.sh")
	if _, err := os.Stat(p); err != nil {
		return "", fmt.Errorf("ESP-IDF not found (run tools/setup-esp-idf.sh)")
	}
	return p, nil
}

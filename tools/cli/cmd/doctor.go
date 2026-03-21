package cmd

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/project"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
	serialpkg "github.com/nikokozak/froth/tools/cli/internal/serial"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runDoctor() error {
	fmt.Printf("go: %s\n", runtime.Version())

	if path, err := exec.LookPath("cmake"); err == nil {
		fmt.Printf("cmake: %s\n", path)
	} else {
		doctorFailure("cmake", "not found", "Install CMake and ensure `cmake` is on PATH.")
	}

	if path, err := exec.LookPath("make"); err == nil {
		fmt.Printf("make: %s\n", path)
	} else {
		doctorFailure("make", "not found", "Install `make` and ensure it is on PATH.")
	}

	candidates, err := serialpkg.ListCandidates()
	if err != nil {
		doctorFailure("serial", fmt.Sprintf("error: %v", err), "Check USB permissions and retry `froth doctor`.")
	} else if len(candidates) == 0 {
		doctorFailure("serial", "no USB-serial ports found", "Connect a USB-serial device and retry.")
	} else {
		fmt.Printf("serial: %d port(s)\n", len(candidates))
		for _, p := range candidates {
			fmt.Printf("  %s\n", p)
		}
	}

	if exportPath, ok := doctorESPIDFStatus(); ok {
		fmt.Printf("esp-idf: installed (%s)\n", filepath.Dir(exportPath))
	} else {
		doctorFailure("esp-idf", "not found", "Run `tools/setup-esp-idf.sh`.")
	}

	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err == nil {
		runProjectDoctor(manifest, root)
	} else if !isBareProjectMode(err) {
		doctorFailure("project", fmt.Sprintf("error: %v", err), "Fix `froth.toml` and retry `froth doctor`.")
	}

	sess, err := session.Connect(portFlag)
	if err != nil {
		doctorFailure("device", "not reachable", doctorDeviceRemediation())
		var discoverErr *serialpkg.DiscoverError
		if errors.As(err, &discoverErr) && discoverErr.Err != nil {
			doctorFailure("probe", fmt.Sprintf("%s: %v", discoverErr.Path, discoverErr.Err), doctorDeviceRemediation())
		} else if portFlag != "" {
			doctorFailure("probe", fmt.Sprintf("%v", err), doctorDeviceRemediation())
		}
	} else {
		info := sess.DeviceInfo()
		fmt.Printf("device: %s on %s (%d-bit)\n", info.Version, info.Board, info.CellBits)
		sess.Close()
	}

	return nil
}

func runProjectDoctor(manifest *project.Manifest, root string) {
	fmt.Printf("project: %s\n", manifest.Project.Name)
	fmt.Printf("target: %s (%s)\n", manifest.Target.Board, manifest.Target.Platform)

	entryPath := filepath.Join(root, manifest.Project.Entry)
	if _, err := os.Stat(entryPath); err == nil {
		fmt.Printf("entry: %s\n", manifest.Project.Entry)
	} else {
		doctorFailure("entry", fmt.Sprintf("missing (%s)", manifest.Project.Entry), fmt.Sprintf("Create `%s` or update `[project].entry` in `froth.toml`.", manifest.Project.Entry))
	}

	depNames := make([]string, 0, len(manifest.Dependencies))
	for name := range manifest.Dependencies {
		depNames = append(depNames, name)
	}
	sort.Strings(depNames)

	for _, name := range depNames {
		dep := manifest.Dependencies[name]
		label := fmt.Sprintf("dependency %s", name)
		if dep.Path == "" {
			doctorFailure(label, "empty path", fmt.Sprintf("Set `[dependencies].%s.path` in `froth.toml` or remove `[dependencies].%s`.", name, name))
			continue
		}

		depPath := filepath.Join(root, dep.Path)
		info, err := os.Stat(depPath)
		if err != nil {
			doctorFailure(label, fmt.Sprintf("missing (%s)", dep.Path), fmt.Sprintf("Create `%s` or remove `[dependencies].%s` from `froth.toml`.", dep.Path, name))
			continue
		}

		if info.IsDir() {
			initPath := filepath.Join(depPath, "init.froth")
			if _, err := os.Stat(initPath); err != nil {
				doctorFailure(label, fmt.Sprintf("missing init.froth (%s)", dep.Path), fmt.Sprintf("Create `%s` or point `[dependencies].%s.path` at a file.", filepath.Join(dep.Path, "init.froth"), name))
				continue
			}
		}

		fmt.Printf("%s: %s\n", label, dep.Path)
	}

	kernelRoot, err := findKernelRoot()
	if err != nil {
		doctorFailure("board", "sdk not available", "Run `froth build` to extract the embedded SDK, then retry `froth doctor`.")
		return
	}

	boardDir := filepath.Join(kernelRoot, "boards", manifest.Target.Board)
	if _, err := os.Stat(boardDir); err == nil {
		fmt.Printf("board: %s\n", boardDir)
	} else {
		doctorFailure("board", fmt.Sprintf("missing (%s)", filepath.Join("boards", manifest.Target.Board)), fmt.Sprintf("Set `[target].board` in `froth.toml` to a directory that exists under `%s`.", filepath.Join(kernelRoot, "boards")))
	}
}

func doctorESPIDFStatus() (string, bool) {
	home, err := sdk.FrothHome()
	if err != nil || home == "" {
		return "", false
	}

	exportPath := filepath.Join(home, "sdk", "esp-idf", "export.sh")
	if _, err := os.Stat(exportPath); err != nil {
		return "", false
	}

	return exportPath, true
}

func doctorFailure(label string, status string, remediation string) {
	fmt.Printf("%s: %s\n", label, status)
	fmt.Printf("  fix: %s\n", remediation)
}

func doctorDeviceRemediation() string {
	if portFlag != "" {
		return fmt.Sprintf("Check the device on `%s` and retry `froth doctor --port %s`.", portFlag, portFlag)
	}
	return "Connect a Froth device or retry with `froth doctor --port <path>`."
}

func isBareProjectMode(err error) bool {
	return strings.Contains(err.Error(), "not in a Froth project")
}

package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/nikokozak/froth/tools/cli/internal/project"
)

func TestRunBuildManifestWritesResolvedSourceWithoutAutorun(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[dependencies]
dep = { path = "lib/dep.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "lib", "dep.froth"), ": dep-word 41 ;")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "helper.froth"), ": helper 1 ;")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), `\ #use "dep"
\ #use "./helper.froth"

: autorun
  dep-word helper +
;
`)

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	resolved := mustReadFile(t, filepath.Join(projectRoot, ".froth-build", "resolved.froth"))
	depMarker := `\ --- lib/dep.froth ---`
	helperMarker := `\ --- src/helper.froth ---`
	mainMarker := `\ --- src/main.froth ---`
	depIndex := strings.Index(resolved, depMarker)
	helperIndex := strings.Index(resolved, helperMarker)
	mainIndex := strings.Index(resolved, mainMarker)
	if depIndex < 0 || helperIndex < 0 || mainIndex < 0 {
		t.Fatalf("resolved source missing expected markers:\n%s", resolved)
	}
	if !(depIndex < helperIndex && helperIndex < mainIndex) {
		t.Fatalf("marker order wrong:\n%s", resolved)
	}
	if strings.Contains(resolved, "[ 'autorun call ] catch drop drop") {
		t.Fatalf("resolved source unexpectedly contains autorun invocation:\n%s", resolved)
	}
}

func TestRunBuildManifestPassesBuildOverridesToCMake(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[build]
cell_size = 64
heap_size = 8192
slot_table_size = 256
line_buffer_size = 2048
tbuf_size = 4096
tdesc_max = 16
ffi_max_tables = 12
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), ": autorun ;")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	log := mustReadFile(t, logPath)
	for _, want := range []string{
		"-DFROTH_CELL_SIZE_BITS=64",
		"-DFROTH_HEAP_SIZE=8192",
		"-DFROTH_SLOT_TABLE_SIZE=256",
		"-DFROTH_LINE_BUFFER_SIZE=2048",
		"-DFROTH_TBUF_SIZE=4096",
		"-DFROTH_TDESC_MAX=16",
		"-DFROTH_FFI_MAX_TABLES=12",
	} {
		if !strings.Contains(log, want) {
			t.Fatalf("build log = %q, want %q", log, want)
		}
	}
}

func TestRunBuildManifestCleanDeletesExistingBuildDir(t *testing.T) {
	resetCommandGlobals(t)
	cleanFlag = true

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), ": autorun ;")
	mustWriteFile(t, filepath.Join(projectRoot, ".froth-build", "stale.txt"), "stale")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	if _, err := os.Stat(filepath.Join(projectRoot, ".froth-build", "stale.txt")); !os.IsNotExist(err) {
		t.Fatalf("stale file still exists after clean build: %v", err)
	}
}

func TestRunBuildManifestMissingEntryFileErrorsWithPath(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/missing.froth"

[target]
board = "posix"
platform = "posix"
`)

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	err = runBuildManifest(manifest, root)
	if err == nil {
		t.Fatal("runBuildManifest succeeded, want error")
	}

	wantPath := filepath.Join(projectRoot, "src", "missing.froth")
	if !strings.Contains(err.Error(), wantPath) {
		t.Fatalf("error = %v, want path %s", err, wantPath)
	}
}

func TestRunBuildManifestMissingDependencyErrorsWithFileContext(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[dependencies]
missing = { path = "lib/missing.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), `\ #use "missing"
: autorun ;
`)

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	err = runBuildManifest(manifest, root)
	if err == nil {
		t.Fatal("runBuildManifest succeeded, want error")
	}
	if !strings.Contains(err.Error(), "src/main.froth:1") {
		t.Fatalf("error = %v, want source location", err)
	}
	if !strings.Contains(err.Error(), "lib/missing.froth") {
		t.Fatalf("error = %v, want missing dependency path", err)
	}
}

func makeFakeKernelRoot(t *testing.T) string {
	t.Helper()

	root := t.TempDir()
	mustWriteFile(t, filepath.Join(root, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.23)\nproject(Froth)\n")
	mustWriteFile(t, filepath.Join(root, "boards", "posix", "ffi.c"), "/* board */\n")
	mustWriteFile(t, filepath.Join(root, "boards", "esp32-devkit-v1", "ffi.c"), "/* board */\n")
	mustWriteFile(t, filepath.Join(root, "src", "froth_vm.h"), "/* vm */\n")
	return root
}

func writeManifestProject(t *testing.T, root string, manifest string) {
	t.Helper()
	mustWriteFile(t, filepath.Join(root, "froth.toml"), manifest)
}

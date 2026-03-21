package sdk

import (
	"io/fs"
	"os"
	"path/filepath"
	"testing"
	"testing/fstest"
)

func TestFrothHomeUsesEnvironmentOverride(t *testing.T) {
	t.Setenv("FROTH_HOME", "/tmp/froth-home")

	home, err := FrothHome()
	if err != nil {
		t.Fatalf("FrothHome: %v", err)
	}
	if home != "/tmp/froth-home" {
		t.Fatalf("home = %q, want %q", home, "/tmp/froth-home")
	}
}

func TestEnsureSDKExtractsEmbeddedTree(t *testing.T) {
	fsys := testKernelFS()
	version, err := versionFromFS(fsys)
	if err != nil {
		t.Fatalf("versionFromFS: %v", err)
	}

	frothHome := t.TempDir()
	sdkRoot, err := ensureSDKFromFS(fsys, frothHome, version)
	if err != nil {
		t.Fatalf("ensureSDKFromFS: %v", err)
	}

	wantRoot := filepath.Join(frothHome, "sdk", "froth-0.1.0")
	if sdkRoot != wantRoot {
		t.Fatalf("sdk root = %q, want %q", sdkRoot, wantRoot)
	}

	assertFileContents(t, sdkRoot, "CMakeLists.txt", `cmake_minimum_required(VERSION 3.23)
set(FROTH_VERSION "0.1.0" CACHE STRING "Froth version string")
`)
	assertFileContents(t, sdkRoot, filepath.Join("src", "froth_vm.h"), "/* vm */\n")
	assertFileContents(t, sdkRoot, filepath.Join("src", "lib", "core.froth"), ": dup dup ;\n")
	assertFileContents(t, sdkRoot, filepath.Join("boards", "posix", "ffi.c"), "/* board */\n")
	assertFileContents(t, sdkRoot, filepath.Join("platforms", "posix", "platform.c"), "/* platform */\n")
	assertFileContents(t, sdkRoot, filepath.Join("cmake", "embed_froth.cmake"), "# embed\n")
}

func TestEnsureSDKSkipsExistingVersion(t *testing.T) {
	fsys := testKernelFS()
	version, err := versionFromFS(fsys)
	if err != nil {
		t.Fatalf("versionFromFS: %v", err)
	}

	frothHome := t.TempDir()
	sdkRoot, err := ensureSDKFromFS(fsys, frothHome, version)
	if err != nil {
		t.Fatalf("first ensureSDKFromFS: %v", err)
	}

	markerPath := filepath.Join(sdkRoot, "marker.txt")
	if err := os.WriteFile(markerPath, []byte("keep"), 0644); err != nil {
		t.Fatalf("write marker: %v", err)
	}

	sdkRoot2, err := ensureSDKFromFS(fsys, frothHome, version)
	if err != nil {
		t.Fatalf("second ensureSDKFromFS: %v", err)
	}
	if sdkRoot2 != sdkRoot {
		t.Fatalf("second sdk root = %q, want %q", sdkRoot2, sdkRoot)
	}
	assertFileContents(t, sdkRoot, "marker.txt", "keep")
}

func testKernelFS() fs.FS {
	return fstest.MapFS{
		"CMakeLists.txt":                      {Data: []byte("cmake_minimum_required(VERSION 3.23)\nset(FROTH_VERSION \"0.1.0\" CACHE STRING \"Froth version string\")\n")},
		"src/froth_vm.h":                      {Data: []byte("/* vm */\n")},
		"src/lib/core.froth":                  {Data: []byte(": dup dup ;\n")},
		"boards/posix/ffi.c":                  {Data: []byte("/* board */\n")},
		"boards/posix/ffi.h":                  {Data: []byte("/* board header */\n")},
		"platforms/posix/platform.c":          {Data: []byte("/* platform */\n")},
		"cmake/embed_froth.cmake":             {Data: []byte("# embed\n")},
		"targets/esp-idf/CMakeLists.txt":      {Data: []byte("# target\n")},
		"targets/esp-idf/main/main.c":         {Data: []byte("/* main */\n")},
		"targets/esp-idf/main/CMakeLists.txt": {Data: []byte("# main cmake\n")},
	}
}

func assertFileContents(t *testing.T, root string, relPath string, want string) {
	t.Helper()

	got, err := os.ReadFile(filepath.Join(root, relPath))
	if err != nil {
		t.Fatalf("read %s: %v", relPath, err)
	}
	if string(got) != want {
		t.Fatalf("%s = %q, want %q", relPath, string(got), want)
	}
}

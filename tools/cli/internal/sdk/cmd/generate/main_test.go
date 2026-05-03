package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestCollectPayloadFilesSkipsBuildCacheDirectories(t *testing.T) {
	root := t.TempDir()
	writePayloadFixture(t, root)
	writeFile(t, filepath.Join(root, "targets", "esp-idf", "build-v4-flash", "artifact.o"), "object")
	writeFile(t, filepath.Join(root, "targets", "esp-idf", "build-runtime-uart", "artifact.o"), "object")
	writeFile(t, filepath.Join(root, "targets", "esp-idf", "build", "artifact.o"), "object")
	writeFile(t, filepath.Join(root, "targets", "esp-idf", "main", "main.c"), "int main(void) { return 0; }\n")

	files, err := collectPayloadFiles(root)
	if err != nil {
		t.Fatalf("collectPayloadFiles: %v", err)
	}

	for _, file := range files {
		switch file.relPath {
		case "targets/esp-idf/build-v4-flash/artifact.o",
			"targets/esp-idf/build-runtime-uart/artifact.o",
			"targets/esp-idf/build/artifact.o":
			t.Fatalf("payload included build cache file %s", file.relPath)
		}
	}
	requirePayloadFile(t, files, "targets/esp-idf/main/main.c")
}

func writePayloadFixture(t *testing.T, root string) {
	t.Helper()

	writeFile(t, filepath.Join(root, "VERSION"), "0.1.0\n")
	writeFile(t, filepath.Join(root, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.23)\n")
	writeFile(t, filepath.Join(root, "cmake", "froth_board_assets.cmake"), "# cmake\n")
	writeFile(t, filepath.Join(root, "src", "frothy_main.c"), "int main(void) { return 0; }\n")
	writeFile(t, filepath.Join(root, "boards", "posix", "ffi.c"), "/* board */\n")
	writeFile(t, filepath.Join(root, "platforms", "posix", "platform.c"), "/* platform */\n")
	writeFile(t, filepath.Join(root, "targets", "esp-idf", "CMakeLists.txt"), "# target\n")
}

func writeFile(t *testing.T, path string, content string) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func requirePayloadFile(t *testing.T, files []payloadFile, relPath string) {
	t.Helper()

	for _, file := range files {
		if file.relPath == relPath {
			return
		}
	}
	t.Fatalf("payload missing %s", relPath)
}

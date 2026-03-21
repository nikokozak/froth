package cmd

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestRunSendRawSourceModePassesSourceDirectly(t *testing.T) {
	resetCommandGlobals(t)

	sourceCh, cleanup := startFakeDaemon(t)
	defer cleanup()

	stdout, stderr := captureOutput(t, func() {
		if err := runSend("1 2 +"); err != nil {
			t.Fatalf("runSend: %v", err)
		}
	})
	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	if !strings.Contains(stdout, "[]") {
		t.Fatalf("stdout = %q, want eval result", stdout)
	}

	got := <-sourceCh
	if got != "1 2 +\n[ 'autorun call ] catch drop drop\n" {
		t.Fatalf("sent source = %q, want raw source plus autorun", got)
	}
	if strings.Contains(got, `\ --- `) {
		t.Fatalf("sent source unexpectedly contains boundary markers: %q", got)
	}
}

func TestRunSendFileModeResolvesIncludesAndAppendsAutorun(t *testing.T) {
	resetCommandGlobals(t)

	sourceCh, cleanup := startFakeDaemon(t)
	defer cleanup()

	projectRoot := t.TempDir()
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[dependencies]
dep = { path = "lib/dep.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "lib", "dep.froth"), ": dep-word 1 ;")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), `\ #use "dep"
: autorun dep-word ;
`)

	if err := runSend(filepath.Join(projectRoot, "src", "main.froth")); err != nil {
		t.Fatalf("runSend: %v", err)
	}

	got := <-sourceCh
	if !strings.Contains(got, `\ --- lib/dep.froth ---`) {
		t.Fatalf("sent source = %q, want resolved dependency", got)
	}
	if !strings.HasSuffix(got, "\n[ 'autorun call ] catch drop drop\n") {
		t.Fatalf("sent source = %q, want autorun appended", got)
	}
}

func TestResolveSourceMissingFrothFileErrors(t *testing.T) {
	resetCommandGlobals(t)

	_, err := resolveSource("missing-file.froth")
	if err == nil {
		t.Fatal("resolveSource succeeded, want error")
	}
	if !strings.Contains(err.Error(), "file not found: missing-file.froth") {
		t.Fatalf("resolveSource error = %v, want file-not-found", err)
	}
}

func TestResolveSourceDirectoryArgumentErrors(t *testing.T) {
	resetCommandGlobals(t)

	dir := t.TempDir()
	_, err := resolveSource(dir)
	if err == nil {
		t.Fatal("resolveSource succeeded, want error")
	}
	if !strings.Contains(err.Error(), "is a directory") {
		t.Fatalf("resolveSource error = %v, want directory error", err)
	}
}

func TestRunSendNoArgumentUsesManifestEntry(t *testing.T) {
	resetCommandGlobals(t)

	sourceCh, cleanup := startFakeDaemon(t)
	defer cleanup()

	projectRoot := t.TempDir()
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[dependencies]
helper = { path = "lib/helper.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "lib", "helper.froth"), ": helper 2 ;")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), `\ #use "helper"
: autorun helper ;
`)

	withChdir(t, projectRoot)
	if err := runSend(""); err != nil {
		t.Fatalf("runSend: %v", err)
	}

	got := <-sourceCh
	if !strings.Contains(got, `\ --- src/main.froth ---`) {
		t.Fatalf("sent source = %q, want manifest entry content", got)
	}
	if !strings.Contains(got, `\ --- lib/helper.froth ---`) {
		t.Fatalf("sent source = %q, want dependency content", got)
	}
}

func TestResolveFromFileSearchesManifestFromFileDirectory(t *testing.T) {
	resetCommandGlobals(t)

	workspace := t.TempDir()

	projectA := filepath.Join(workspace, "project-a")
	writeManifestProject(t, projectA, `[project]
name = "a"
entry = "src/main.froth"

[dependencies]
libdep = { path = "lib/wrong.froth" }
`)
	mustWriteFile(t, filepath.Join(projectA, "lib", "wrong.froth"), ": wrong 111 ;")
	mustWriteFile(t, filepath.Join(projectA, "src", "main.froth"), ": autorun wrong ;")

	projectB := filepath.Join(workspace, "project-b")
	writeManifestProject(t, projectB, `[project]
name = "b"
entry = "src/main.froth"

[dependencies]
libdep = { path = "lib/right.froth" }
`)
	mustWriteFile(t, filepath.Join(projectB, "lib", "right.froth"), ": right 222 ;")
	mustWriteFile(t, filepath.Join(projectB, "src", "main.froth"), `\ #use "libdep"
: autorun right ;
`)

	withChdir(t, projectA)
	got, err := resolveFromFile(filepath.Join(projectB, "src", "main.froth"))
	if err != nil {
		t.Fatalf("resolveFromFile: %v", err)
	}
	if !strings.Contains(got, `\ --- lib/right.froth ---`) {
		t.Fatalf("resolved source = %q, want project-b dependency", got)
	}
	if strings.Contains(got, "wrong 111") {
		t.Fatalf("resolved source = %q, unexpectedly used cwd manifest", got)
	}
}

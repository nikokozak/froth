package cmd

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestRunSendRawSourceModePassesSourceDirectly(t *testing.T) {
	resetCommandGlobals(t)

	reqCh, cleanup := startFakeDaemon(t)
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

	req := <-reqCh
	if req.Method != "eval" {
		t.Fatalf("first request = %#v, want eval", req)
	}
	if req.Source != "1 2 +\n[ 'autorun call ] catch drop drop\n" {
		t.Fatalf("sent source = %q, want raw source plus autorun", req.Source)
	}
	if strings.Contains(req.Source, `\ --- `) {
		t.Fatalf("sent source unexpectedly contains boundary markers: %q", req.Source)
	}
}

func TestRunSendFileModeResolvesIncludesAndAppendsAutorun(t *testing.T) {
	resetCommandGlobals(t)

	reqCh, cleanup := startFakeDaemon(t)
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

	resetReq := <-reqCh
	if resetReq.Method != "reset" {
		t.Fatalf("first request = %#v, want reset", resetReq)
	}
	evalReq := <-reqCh
	if evalReq.Method != "eval" {
		t.Fatalf("second request = %#v, want eval", evalReq)
	}
	if !strings.Contains(evalReq.Source, `\ --- lib/dep.froth ---`) {
		t.Fatalf("sent source = %q, want resolved dependency", evalReq.Source)
	}
	if !strings.HasSuffix(evalReq.Source, "\n[ 'autorun call ] catch drop drop\n") {
		t.Fatalf("sent source = %q, want autorun appended", evalReq.Source)
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

	reqCh, cleanup := startFakeDaemon(t)
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

	resetReq := <-reqCh
	if resetReq.Method != "reset" {
		t.Fatalf("first request = %#v, want reset", resetReq)
	}
	evalReq := <-reqCh
	if evalReq.Method != "eval" {
		t.Fatalf("second request = %#v, want eval", evalReq)
	}
	if !strings.Contains(evalReq.Source, `\ --- src/main.froth ---`) {
		t.Fatalf("sent source = %q, want manifest entry content", evalReq.Source)
	}
	if !strings.Contains(evalReq.Source, `\ --- lib/helper.froth ---`) {
		t.Fatalf("sent source = %q, want dependency content", evalReq.Source)
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
	payload, err := resolveFromFile(filepath.Join(projectB, "src", "main.froth"))
	if err != nil {
		t.Fatalf("resolveFromFile: %v", err)
	}
	if !payload.resetBeforeEval {
		t.Fatal("resolveFromFile resetBeforeEval = false, want true")
	}
	if !strings.Contains(payload.source, `\ --- lib/right.froth ---`) {
		t.Fatalf("resolved source = %q, want project-b dependency", payload.source)
	}
	if strings.Contains(payload.source, "wrong 111") {
		t.Fatalf("resolved source = %q, unexpectedly used cwd manifest", payload.source)
	}
}

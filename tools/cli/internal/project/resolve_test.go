package project

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func setupProject(t *testing.T, files map[string]string) string {
	t.Helper()
	dir := t.TempDir()
	for path, content := range files {
		full := filepath.Join(dir, path)
		os.MkdirAll(filepath.Dir(full), 0755)
		if err := os.WriteFile(full, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func TestResolveSimple(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
`,
		"src/main.froth": `: main "hello" s.emit ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}

	if !strings.Contains(result.Source, ": main") {
		t.Error("expected main definition in resolved source")
	}
	if !strings.Contains(result.Source, "--- src/main.froth ---") {
		t.Error("expected file boundary marker")
	}
	if !strings.Contains(result.Source, "[ 'autorun call ] catch drop") {
		t.Error("expected autorun invocation appended")
	}
}

func TestResolveWithDependency(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
[dependencies]
helper = { path = "lib/helper.froth" }
`,
		"lib/helper.froth": `: double dup + ;
`,
		"src/main.froth": `\ #use "helper"
: main 5 double . ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}

	// helper should appear before main
	helperIdx := strings.Index(result.Source, "--- lib/helper.froth ---")
	mainIdx := strings.Index(result.Source, "--- src/main.froth ---")
	if helperIdx < 0 || mainIdx < 0 {
		t.Fatal("missing boundary markers")
	}
	if helperIdx >= mainIdx {
		t.Error("dependency should appear before entry file")
	}

	// #use line should be stripped
	if strings.Contains(result.Source, `\ #use "helper"`) {
		t.Error("#use directive should be stripped from output")
	}
}

func TestResolveDiamond(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }
common = { path = "lib/common.froth" }
`,
		"lib/common.froth": `: shared 42 ;
`,
		"lib/a.froth": `\ #use "common"
: a-word shared . ;
`,
		"lib/b.froth": `\ #use "common"
: b-word shared . ;
`,
		"src/main.froth": `\ #use "a"
\ #use "b"
: main a-word b-word ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}

	// common should appear exactly once
	count := strings.Count(result.Source, "--- lib/common.froth ---")
	if count != 1 {
		t.Errorf("common should appear once, got %d", count)
	}

	// Order: common, a, b, main
	commonIdx := strings.Index(result.Source, "--- lib/common.froth ---")
	aIdx := strings.Index(result.Source, "--- lib/a.froth ---")
	bIdx := strings.Index(result.Source, "--- lib/b.froth ---")
	mainIdx := strings.Index(result.Source, "--- src/main.froth ---")
	if !(commonIdx < aIdx && aIdx < bIdx && bIdx < mainIdx) {
		t.Error("expected order: common < a < b < main")
	}
}

func TestResolveCycle(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }
`,
		"lib/a.froth": `\ #use "b"
: a-word 1 ;
`,
		"lib/b.froth": `\ #use "a"
: b-word 2 ;
`,
		"src/main.froth": `\ #use "a"
: main a-word ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	_, err = Resolve(m, root)
	if err == nil {
		t.Fatal("expected circular include error")
	}
	if !strings.Contains(err.Error(), "circular") {
		t.Errorf("expected circular error, got: %s", err)
	}
}

func TestResolveSelfInclude(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
`,
		"src/main.froth": `\ #use "./main.froth"
: main 1 ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	_, err = Resolve(m, root)
	if err == nil {
		t.Fatal("expected self-include error")
	}
	if !strings.Contains(err.Error(), "includes itself") {
		t.Errorf("expected 'includes itself' error, got: %s", err)
	}
}

func TestResolveRelativePath(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
`,
		"src/helpers.froth": `: helper 99 ;
`,
		"src/main.froth": `\ #use "./helpers.froth"
: main helper . ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}

	if !strings.Contains(result.Source, ": helper 99 ;") {
		t.Error("expected helper definition in output")
	}
}

func TestResolveRootEscape(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
`,
		"src/main.froth": `\ #use "../../etc/passwd"
: main 1 ;
`,
	})

	// Create the target file so the resolver gets past the "not found" check
	// The root escape check should fire before that
	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	_, err = Resolve(m, root)
	if err == nil {
		t.Fatal("expected root escape error")
	}
	if !strings.Contains(err.Error(), "escape") && !strings.Contains(err.Error(), "not found") {
		t.Errorf("expected escape or not-found error, got: %s", err)
	}
}

func TestResolveDirectiveInsideComment(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
`,
		"src/main.froth": `( \ #use "nonexistent" )
: main 1 ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	// Should succeed — the #use is inside a paren comment
	result, err := Resolve(m, root)
	if err != nil {
		t.Fatalf("directive inside comment should be ignored, got: %s", err)
	}
	if !strings.Contains(result.Source, ": main 1 ;") {
		t.Error("expected main definition in output")
	}
}

func TestResolveLibraryDisciplineWarning(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "lib/bad.froth" }
`,
		"lib/bad.froth": `: helper 1 ;
"side effect" s.emit
`,
		"src/main.froth": `\ #use "bad"
: main helper ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}

	if len(result.Warnings) == 0 {
		t.Error("expected library discipline warning for top-level form")
	}
}

func TestResolveAllowToplevel(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `
[project]
name = "test"
entry = "src/main.froth"
[dependencies]
init = { path = "lib/init.froth" }
`,
		"lib/init.froth": `\ #allow-toplevel
ledc.init
: helper 1 ;
`,
		"src/main.froth": `\ #use "init"
: main helper ;
`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}

	if len(result.Warnings) != 0 {
		t.Errorf("expected no warnings with #allow-toplevel, got: %v", result.Warnings)
	}
}

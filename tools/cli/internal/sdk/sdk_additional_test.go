package sdk

import (
	"os"
	"path/filepath"
	"sync"
	"testing"
)

func TestEnsureSDKFromFSConcurrentCallsAreAtomic(t *testing.T) {
	fsys := testKernelFS()
	version, err := versionFromFS(fsys)
	if err != nil {
		t.Fatalf("versionFromFS: %v", err)
	}

	frothHome := t.TempDir()
	const callers = 8

	paths := make([]string, callers)
	errs := make([]error, callers)

	var wg sync.WaitGroup
	wg.Add(callers)
	for i := 0; i < callers; i++ {
		go func(i int) {
			defer wg.Done()
			paths[i], errs[i] = ensureSDKFromFS(fsys, frothHome, version)
		}(i)
	}
	wg.Wait()

	for i, err := range errs {
		if err != nil {
			t.Fatalf("ensureSDKFromFS call %d: %v", i, err)
		}
	}

	wantRoot := filepath.Join(frothHome, "sdk", "froth-0.1.0")
	for i, path := range paths {
		if path != wantRoot {
			t.Fatalf("paths[%d] = %q, want %q", i, path, wantRoot)
		}
	}

	entries, err := os.ReadDir(filepath.Join(frothHome, "sdk"))
	if err != nil {
		t.Fatalf("ReadDir sdk cache: %v", err)
	}
	if len(entries) != 1 || entries[0].Name() != "froth-0.1.0" {
		t.Fatalf("sdk cache entries = %v, want only froth-0.1.0", entryNames(entries))
	}
}

func TestVersionFromFSParsesCMakeListsVersion(t *testing.T) {
	version, err := versionFromFS(testKernelFS())
	if err != nil {
		t.Fatalf("versionFromFS: %v", err)
	}
	if version != "0.1.0" {
		t.Fatalf("version = %q, want %q", version, "0.1.0")
	}
}

func entryNames(entries []os.DirEntry) []string {
	names := make([]string, 0, len(entries))
	for _, entry := range entries {
		names = append(names, entry.Name())
	}
	return names
}

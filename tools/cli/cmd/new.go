package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

func runNew(args []string) error {
	if len(args) == 0 || strings.TrimSpace(args[0]) == "" {
		return fmt.Errorf("new requires a project name")
	}
	dir := args[0]
	name := filepath.Base(dir)
	if name == "" || name == "." || name == "/" {
		return fmt.Errorf("invalid project name: %q", args[0])
	}

	// Reject names that would produce invalid TOML
	if strings.ContainsAny(name, "\"'\n\r\\") {
		return fmt.Errorf("project name %q contains invalid characters", name)
	}

	board := "posix"
	platform := "posix"
	if targetFlag != "" {
		board = targetFlag
		// Infer platform from board name
		switch {
		case board == "posix":
			platform = "posix"
		default:
			platform = "esp-idf"
		}
	}

	if _, err := os.Stat(dir); err == nil {
		return fmt.Errorf("directory %s already exists", dir)
	}

	dirs := []string{
		dir,
		filepath.Join(dir, "src"),
		filepath.Join(dir, "lib"),
	}
	for _, d := range dirs {
		if err := os.MkdirAll(d, 0755); err != nil {
			return fmt.Errorf("create %s: %w", d, err)
		}
	}

	manifest := fmt.Sprintf(`[project]
name = "%s"
version = "0.1.0"
entry = "src/main.froth"

[target]
board = "%s"
platform = "%s"
`, name, board, platform)

	mainFroth := fmt.Sprintf(`\ %s

: autorun
  "Hello from Froth!" s.emit cr
;
`, name)

	gitignore := `.froth-build/
froth_a.snap
froth_b.snap
`

	files := map[string]string{
		filepath.Join(dir, "froth.toml"):      manifest,
		filepath.Join(dir, "src", "main.froth"): mainFroth,
		filepath.Join(dir, "lib", ".gitkeep"):  "",
		filepath.Join(dir, ".gitignore"):       gitignore,
	}

	for path, content := range files {
		if err := os.WriteFile(path, []byte(content), 0644); err != nil {
			return fmt.Errorf("write %s: %w", path, err)
		}
	}

	fmt.Printf("Created project %s\n", name)
	fmt.Printf("  target: %s (%s)\n", board, platform)
	fmt.Printf("  entry:  src/main.froth\n")
	fmt.Println()
	fmt.Printf("Next steps:\n")
	fmt.Printf("  cd %s\n", dir)
	fmt.Printf("  froth send        # send to device\n")
	fmt.Printf("  froth build       # build firmware\n")

	return nil
}

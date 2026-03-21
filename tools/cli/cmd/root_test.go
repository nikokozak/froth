package cmd

import (
	"os"
	"strings"
	"testing"
)

func TestExecutePrintsUsageWithNoCommand(t *testing.T) {
	resetCommandGlobals(t)

	oldArgs := os.Args
	os.Args = []string{"froth"}
	t.Cleanup(func() { os.Args = oldArgs })

	stdout, stderr := captureOutput(t, func() {
		if err := Execute(); err != nil {
			t.Fatalf("Execute: %v", err)
		}
	})

	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	if !strings.Contains(stdout, "Usage: froth [flags] <command>") {
		t.Fatalf("stdout = %q, want usage", stdout)
	}
}

func TestExecuteRejectsUnknownCommand(t *testing.T) {
	resetCommandGlobals(t)

	oldArgs := os.Args
	os.Args = []string{"froth", "definitely-not-a-command"}
	t.Cleanup(func() { os.Args = oldArgs })

	err := Execute()
	if err == nil {
		t.Fatal("Execute succeeded, want error")
	}
	if !strings.Contains(err.Error(), "unknown command: definitely-not-a-command") {
		t.Fatalf("Execute error = %v, want unknown command", err)
	}
}

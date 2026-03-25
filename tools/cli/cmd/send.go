package cmd

import (
	"encoding/json"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
	"github.com/nikokozak/froth/tools/cli/internal/project"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

const frothErrorReset = 20

type sendPayload struct {
	source          string
	resetBeforeEval bool
}

func runSend(fileArg string) error {
	payload, err := resolveSource(fileArg)
	if err != nil {
		return err
	}

	// Append autorun invocation for send path (ADR-044)
	payload.source = project.AppendAutorun(payload.source)

	if !serialFlag {
		err := sendViaDaemon(payload)
		if err == nil {
			return nil
		}
		if daemonFlag {
			return fmt.Errorf("daemon: %w", err)
		}
	}
	return sendViaSerial(payload)
}

// resolveSource resolves includes and produces a merged source string.
// If fileArg is a raw .froth source string (not a file path), it's sent directly.
// If fileArg is a file path, the resolver runs. If no fileArg, uses froth.toml entry.
func resolveSource(fileArg string) (*sendPayload, error) {
	if fileArg != "" {
		info, err := os.Stat(fileArg)
		if err == nil {
			if info.IsDir() {
				return nil, fmt.Errorf("%s is a directory, not a file", fileArg)
			}
			return resolveFromFile(fileArg)
		}
		// If it looks like a file path but doesn't exist, error instead of
		// silently treating it as raw source
		if strings.HasSuffix(fileArg, ".froth") || strings.Contains(fileArg, "/") {
			return nil, fmt.Errorf("file not found: %s", fileArg)
		}
		// Raw source (backward compat with `froth send "1 2 +"`)
		return &sendPayload{source: fileArg}, nil
	}

	// No argument — try to use froth.toml
	cwd, err := os.Getwd()
	if err != nil {
		return nil, fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err != nil {
		return nil, fmt.Errorf("no file specified and %w", err)
	}

	result, err := project.Resolve(manifest, root)
	if err != nil {
		return nil, err
	}
	printWarnings(result.Warnings)
	printResolveSummary(result)

	return &sendPayload{
		source:          result.Source,
		resetBeforeEval: true,
	}, nil
}

func resolveFromFile(filePath string) (*sendPayload, error) {
	absPath, err := filepath.Abs(filePath)
	if err != nil {
		return nil, err
	}

	// Search for manifest starting from the file's directory, not CWD.
	// This way `froth send /other/project/src/main.froth` finds that
	// project's froth.toml, not an unrelated one in CWD.
	fileDir := filepath.Dir(absPath)
	manifest, root, err := project.Load(fileDir)
	if err != nil {
		// No manifest — resolve the single file without includes
		return resolveBareSingleFile(absPath)
	}

	// Override entry to the specified file
	relPath, err := filepath.Rel(root, absPath)
	if err != nil {
		return resolveBareSingleFile(absPath)
	}
	manifest.Project.Entry = relPath

	result, err := project.Resolve(manifest, root)
	if err != nil {
		return nil, err
	}
	printWarnings(result.Warnings)
	printResolveSummary(result)

	return &sendPayload{
		source:          result.Source,
		resetBeforeEval: true,
	}, nil
}

func resolveBareSingleFile(absPath string) (*sendPayload, error) {
	dir := filepath.Dir(absPath)
	result, err := project.ResolveEntry(absPath, dir)
	if err != nil {
		return nil, err
	}
	printWarnings(result.Warnings)
	return &sendPayload{
		source:          result.Source,
		resetBeforeEval: true,
	}, nil
}

func printWarnings(warnings []string) {
	for _, w := range warnings {
		fmt.Fprintf(os.Stderr, "warning: %s\n", w)
	}
}

func printResolveSummary(result *project.ResolveResult) {
	if len(result.Files) > 1 {
		fmt.Fprintf(os.Stderr, "Resolved %s (%d dependencies)\n",
			result.Files[len(result.Files)-1], len(result.Files)-1)
	}
}

func sendViaDaemon(payload *sendPayload) error {
	client, err := daemon.Dial()
	if err != nil {
		return err
	}
	defer client.Close()

	client.EventHandler = func(method string, params json.RawMessage) {
		if method == daemon.EventConsole {
			var evt daemon.ConsoleEvent
			json.Unmarshal(params, &evt)
			os.Stdout.Write(evt.Data)
		}
	}

	if payload.resetBeforeEval {
		if _, err := client.Reset(); err != nil {
			return fmt.Errorf("reset: %w", err)
		}
	}

	sigintCh := make(chan os.Signal, 1)
	signal.Notify(sigintCh, os.Interrupt)
	defer signal.Stop(sigintCh)

	type evalOutcome struct {
		result *daemon.EvalResult
		err    error
	}
	evalCh := make(chan evalOutcome, 1)
	go func() {
		result, err := client.Eval(payload.source)
		evalCh <- evalOutcome{result: result, err: err}
	}()

	for {
		select {
		case outcome := <-evalCh:
			if outcome.err != nil {
				return fmt.Errorf("eval: %w", outcome.err)
			}
			result := outcome.result
			if result.Status == 0 || result.ErrorCode == frothErrorReset {
				if result.StackRepr != "" {
					fmt.Println(result.StackRepr)
				}
			} else {
				msg := fmt.Sprintf("error(%d)", result.ErrorCode)
				if result.FaultWord != "" {
					msg += fmt.Sprintf(" in \"%s\"", result.FaultWord)
				}
				fmt.Println(msg)
			}
			return nil
		case <-sigintCh:
			interruptClient, dialErr := daemon.Dial()
			if dialErr != nil {
				fmt.Fprintf(os.Stderr, "interrupt: %v\n", dialErr)
				continue
			}
			if interruptErr := interruptClient.Interrupt(); interruptErr != nil {
				fmt.Fprintf(os.Stderr, "interrupt: %v\n", interruptErr)
			}
			_ = interruptClient.Close()
		}
	}
}

func sendViaSerial(payload *sendPayload) error {
	sess, err := session.Connect(portFlag)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer sess.Close()

	sess.OutputHandler = func(data []byte) {
		_, _ = os.Stdout.Write(data)
	}

	if payload.resetBeforeEval {
		if _, err := sess.Reset(); err != nil {
			return fmt.Errorf("reset: %w", err)
		}
	}

	sigintCh := make(chan os.Signal, 1)
	signal.Notify(sigintCh, os.Interrupt)
	defer signal.Stop(sigintCh)

	type evalOutcome struct {
		result *sessionEvalResult
		err    error
	}
	evalCh := make(chan evalOutcome, 1)
	go func() {
		result, err := sess.Eval(payload.source)
		outcome := evalOutcome{err: err}
		if result != nil {
			outcome.result = &sessionEvalResult{
				Status:    result.Status,
				ErrorCode: result.ErrorCode,
				FaultWord: result.FaultWord,
				StackRepr: result.StackRepr,
			}
		}
		evalCh <- outcome
	}()

	for {
		select {
		case outcome := <-evalCh:
			if outcome.err != nil {
				return fmt.Errorf("eval: %w", outcome.err)
			}
			result := outcome.result
			if result.Status == 0 || result.ErrorCode == frothErrorReset {
				if result.StackRepr != "" {
					fmt.Println(result.StackRepr)
				}
			} else {
				msg := fmt.Sprintf("error(%d)", result.ErrorCode)
				if result.FaultWord != "" {
					msg += fmt.Sprintf(" in \"%s\"", result.FaultWord)
				}
				fmt.Println(msg)
			}
			return nil
		case <-sigintCh:
			fmt.Fprintln(os.Stderr, "interrupted")
			_ = sess.Abort()
			os.Exit(130)
		}
	}
}

type sessionEvalResult struct {
	Status    uint8
	ErrorCode uint16
	FaultWord string
	StackRepr string
}

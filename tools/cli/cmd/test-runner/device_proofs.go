package main

import (
	"bytes"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/nikokozak/frothy/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/frothy/tools/cli/internal/project"
	"github.com/nikokozak/frothy/tools/cli/internal/sdk"
)

type proofLog struct {
	lines []string
}

type evalProofResult struct {
	Source string
	Value  string
	Output string
}

func commandProofWorkshopV4(args []string) error {
	fs := flag.NewFlagSet("proof-workshop-v4", flag.ContinueOnError)
	port := fs.String("port", "", "serial port for v4 workshop board")
	liveControls := fs.Bool("live-controls", false, "require manual joystick/button transitions")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *port == "" {
		return fmt.Errorf("--port is required")
	}
	if err := requireSerialPort(*port); err != nil {
		return err
	}
	if err := waitForSerialPortReady(*port, 60*time.Second); err != nil {
		return err
	}

	manager, info, err := connectDeviceManager(*port, 15*time.Second)
	if err != nil {
		return err
	}
	defer manager.Disconnect()

	fmt.Printf("Connected to %s on %s\n", info.Board, info.Port)

	for _, source := range []string{
		"matrix.init:",
		"grid.clear:",
		"grid.fill: true",
	} {
		if _, _, err := evalDeviceForm(manager, source); err != nil {
			return err
		}
	}
	if err := expectEvalExact(manager, "row0 after grid.fill true", "tm1629.raw.row@: 0", "4095"); err != nil {
		return err
	}
	if _, _, err := evalDeviceForm(manager, "grid.fill: false"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "row0 after grid.fill false", "tm1629.raw.row@: 0", "0"); err != nil {
		return err
	}
	for _, source := range []string{
		"grid.set: 1, 1, true",
		"grid.show:",
	} {
		if _, _, err := evalDeviceForm(manager, source); err != nil {
			return err
		}
	}
	if err := expectEvalExact(manager, "row1 after single pixel", "tm1629.raw.row@: 1", "2"); err != nil {
		return err
	}
	for _, source := range []string{
		"grid.rect: 2, 2, 4, 3, true",
		"grid.show:",
	} {
		if _, _, err := evalDeviceForm(manager, source); err != nil {
			return err
		}
	}
	if err := expectEvalExact(manager, "row2 after rect", "tm1629.raw.row@: 2", "60"); err != nil {
		return err
	}

	for _, check := range []struct {
		label  string
		source string
		min    int
		max    int
	}{
		{"knob.left.raw", "knob.left.raw:", 0, 4095},
		{"knob.right.raw", "knob.right.raw:", 0, 4095},
		{"knob.left", "knob.left:", 0, 100},
		{"knob.right", "knob.right:", 0, 100},
	} {
		if err := expectEvalRange(manager, check.label, check.source, check.min, check.max); err != nil {
			return err
		}
	}

	for _, check := range []struct {
		label    string
		source   string
		expected string
	}{
		{"joy.up.pin", "joy.up.pin", "17"},
		{"joy.down.pin", "joy.down.pin", "16"},
		{"joy.left.pin", "joy.left.pin", "13"},
		{"joy.right.pin", "joy.right.pin", "14"},
		{"joy.click.pin", "joy.click.pin", "25"},
	} {
		if err := expectEvalExact(manager, check.label, check.source, check.expected); err != nil {
			return err
		}
	}

	fmt.Fprintln(os.Stderr, "release v4 joystick/button controls now")
	for _, check := range []struct {
		label    string
		source   string
		expected string
	}{
		{"joy.up? idle", "joy.up?:", "false"},
		{"joy.down? idle", "joy.down?:", "false"},
		{"joy.left? idle", "joy.left?:", "false"},
		{"joy.right? idle", "joy.right?:", "false"},
		{"joy.click? idle", "joy.click?:", "false"},
	} {
		if err := waitForExact(manager, check.label, check.source, check.expected, 5*time.Second); err != nil {
			return err
		}
	}

	if _, _, err := evalDeviceForm(manager, "joy.up.pin is LED_BUILTIN"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "overlay joy.up.pin", "joy.up.pin", "2"); err != nil {
		return err
	}
	if _, _, err := evalDeviceForm(manager, "dangerous.wipe:"); err != nil {
		return err
	}
	for _, check := range []struct {
		label    string
		source   string
		expected string
	}{
		{"restored joy.up.pin", "joy.up.pin", "17"},
		{"restored joy.down.pin", "joy.down.pin", "16"},
		{"restored joy.left.pin", "joy.left.pin", "13"},
		{"restored joy.right.pin", "joy.right.pin", "14"},
		{"restored joy.click.pin", "joy.click.pin", "25"},
		{"restored knob.left.pin", "knob.left.pin", "33"},
		{"restored knob.right.pin", "knob.right.pin", "32"},
	} {
		if err := expectEvalExact(manager, check.label, check.source, check.expected); err != nil {
			return err
		}
	}
	if err := expectEvalRange(manager, "knob.left after wipe", "knob.left:", 0, 100); err != nil {
		return err
	}
	if err := expectEvalRange(manager, "knob.right after wipe", "knob.right:", 0, 100); err != nil {
		return err
	}

	if *liveControls {
		for _, check := range []struct {
			label  string
			source string
		}{
			{"joy.up?", "joy.up?:"},
			{"joy.down?", "joy.down?:"},
			{"joy.left?", "joy.left?:"},
			{"joy.right?", "joy.right?:"},
			{"joy.click?", "joy.click?:"},
		} {
			if err := expectLiveTransition(manager, check.label, check.source); err != nil {
				return err
			}
		}
	}

	fmt.Printf("ok: v4 workshop surface smoke passed on %s\n", *port)
	return nil
}

func commandProofM10Device(args []string) error {
	fs := flag.NewFlagSet("proof-m10-device", flag.ContinueOnError)
	port := fs.String("port", "", "serial port for ESP32 proof board")
	board := fs.String("board", "esp32-devkit-v1", "board name for maintained flash path")
	cli := fs.String("cli", "", "froth CLI binary")
	assumeBlink := fs.Bool("assume-blink-confirmed", false, "accepted for compatibility; device proof is non-interactive")
	transcriptOut := fs.String("transcript-out", "", "write compact control proof transcript")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *port == "" {
		return fmt.Errorf("--port is required")
	}
	if err := requireSerialPort(*port); err != nil {
		return err
	}
	if err := waitForSerialPortReady(*port, 60*time.Second); err != nil {
		return err
	}
	if *assumeBlink {
		fmt.Fprintln(os.Stderr, "note: --assume-blink-confirmed accepted; Go device proof is non-interactive")
	}

	paths, err := detectPaths()
	if err != nil {
		return err
	}
	cliPath := resolveCLIPath(paths.Root, *cli)
	if !fileExists(cliPath) {
		return fmt.Errorf("missing CLI binary: %s", cliPath)
	}

	log := &proofLog{}
	defer func() {
		if *transcriptOut != "" {
			if err := os.WriteFile(*transcriptOut, []byte(log.String()), 0o644); err != nil {
				fmt.Fprintf(os.Stderr, "warning: write transcript: %v\n", err)
			}
		}
	}()

	log.Step("flash maintained image")
	if err := runM10Flash(paths, cliPath, *board, *port); err != nil {
		return err
	}

	manager, info, err := connectDeviceManager(*port, 15*time.Second)
	if err != nil {
		return err
	}
	log.Step(fmt.Sprintf("connected to %s on %s", info.Board, info.Port))

	if err := runM10BlinkProof(paths.Root, manager, log); err != nil {
		_ = manager.Disconnect()
		return err
	}
	if err := wipeDevice(manager, log, "clear blink proof overlay"); err != nil {
		_ = manager.Disconnect()
		return err
	}
	if err := runM10BootSetup(paths.Root, manager, log); err != nil {
		_ = manager.Disconnect()
		return err
	}
	if err := manager.Disconnect(); err != nil {
		return err
	}

	log.Step("safe boot monitor check")
	if err := runM10SafeBootProof(paths, *port, log); err != nil {
		return err
	}

	log.Step("reflash for boot persistence check")
	if err := runM10Flash(paths, cliPath, *board, *port); err != nil {
		return err
	}

	manager, info, err = connectDeviceManager(*port, 15*time.Second)
	if err != nil {
		return err
	}
	defer manager.Disconnect()
	log.Step(fmt.Sprintf("reconnected to %s on %s", info.Board, info.Port))

	if err := runM10BootVerify(manager, log); err != nil {
		return err
	}
	if err := runM10CellsProof(paths.Root, manager, log); err != nil {
		return err
	}
	if err := wipeDevice(manager, log, "clear cells proof overlay"); err != nil {
		return err
	}
	if err := runM10BoardSurfaceProof(manager, log); err != nil {
		return err
	}

	fmt.Printf("ok: M10 ESP32 proof passed on %s (%s)\n", *port, *board)
	return nil
}

func resolveCLIPath(root string, provided string) string {
	if provided != "" {
		return provided
	}
	if fromEnv := os.Getenv("FROTH_CLI_BINARY"); fromEnv != "" {
		return fromEnv
	}
	if fromEnv := os.Getenv("FROTHY_CLI_BINARY"); fromEnv != "" {
		return fromEnv
	}
	return filepath.Join(root, "tools", "cli", "froth-cli")
}

func runM10Flash(paths pathSet, cliPath string, board string, port string) error {
	const maxAttempts = 3
	var lastErr error
	for attempt := 1; attempt <= maxAttempts; attempt++ {
		if err := waitForSerialPortReady(port, 60*time.Second); err != nil {
			return err
		}
		lastErr = runCommand(
			paths,
			"device:m10:flash",
			baseTestEnv(paths),
			paths.Root,
			cliPath,
			"--target",
			"esp-idf",
			"--board",
			board,
			"--port",
			port,
			"flash",
		)
		if lastErr == nil {
			return nil
		}
		if attempt == maxAttempts || !m10FlashFailureIsRetryable(paths) {
			return lastErr
		}
		fmt.Fprintf(os.Stderr, "retry: device:m10:flash attempt %d/%d hit serial transport failure, retrying\n", attempt, maxAttempts)
		time.Sleep(time.Duration(attempt) * time.Second)
	}
	return lastErr
}

func m10FlashFailureIsRetryable(paths pathSet) bool {
	data, err := os.ReadFile(profileLogPath(paths, "device:m10:flash"))
	if err != nil {
		return false
	}
	text := string(data)
	for _, needle := range []string{
		"The chip stopped responding",
		"Could not exclusively lock port",
		"device disconnected or multiple access on port",
		"Waiting for the device to reconnect",
		"port is busy or doesn't exist",
		"No serial data received",
		"Failed to connect",
		"Timed out waiting for packet",
	} {
		if strings.Contains(text, needle) {
			return true
		}
	}
	return false
}

func runM10BlinkProof(root string, manager *frothycontrol.Manager, log *proofLog) error {
	if err := expectEvalExact(manager, "blink pin output mode", "gpio.mode: LED_BUILTIN, 1", "nil"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink pin high write", "gpio.write: LED_BUILTIN, 1", "nil"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink pin high readback", "gpio.read: LED_BUILTIN", "1"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink pin low write", "gpio.write: LED_BUILTIN, 0", "nil"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink pin low readback", "gpio.read: LED_BUILTIN", "0"); err != nil {
		return err
	}
	if _, err := evalProofFile(manager, filepath.Join(root, "tools", "frothy", "proof_m10_blink.frothy"), log); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink pulses", "pulses", "3"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink period", "period", "50"); err != nil {
		return err
	}
	if err := expectEvalExact(manager, "blink final pin state", "gpio.read: LED_BUILTIN", "0"); err != nil {
		return err
	}
	return nil
}

func runM10SafeBootProof(paths pathSet, port string, log *proofLog) error {
	if err := waitForSerialPortReady(port, 60*time.Second); err != nil {
		return err
	}
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}
	targetDir := filepath.Join(paths.Root, "targets", "esp-idf")
	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("target dir not found: %s", targetDir)
	}

	if _, err := exec.LookPath("script"); err != nil {
		return fmt.Errorf("script command is required for the safe boot TTY monitor check: %w", err)
	}

	monitorCommand := `. "$IDF_EXPORT" >/dev/null 2>&1 && exec idf.py -p "$FROTHY_PORT" monitor`
	var cmd *exec.Cmd
	if runtime.GOOS == "linux" {
		cmd = exec.Command("script", "-q", "-c", monitorCommand, "/dev/null")
	} else {
		cmd = exec.Command("script", "-q", "/dev/null", "bash", "-c", monitorCommand)
	}
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(), "IDF_EXPORT="+exportPath, "FROTHY_PORT="+port)

	session, err := startCommandStream(cmd)
	if err != nil {
		return err
	}
	defer session.closeWith("\x1d")

	var transcript []byte
	_, err = session.waitFor(func(data []byte) bool {
		return bytes.Contains(data, []byte("idf_monitor")) ||
			bytes.Contains(data, []byte("esp-idf-monitor")) ||
			bytes.Contains(data, []byte("boot: CTRL-C for safe boot")) ||
			hasTerminalPrompt(data, frothyPrompt)
	}, 10*time.Second)
	if err != nil {
		return fmt.Errorf("safe boot monitor ready: %w", err)
	}
	if err := session.send("\x14\x12"); err != nil {
		return fmt.Errorf("send safe boot monitor reset: %w", err)
	}
	chunk, err := session.waitFor(func(data []byte) bool {
		text := normalizeNewlines(data)
		return strings.Contains(text, "snapshot: found") &&
			strings.Contains(text, "boot: CTRL-C for safe boot")
	}, 120*time.Second)
	if err != nil {
		return fmt.Errorf("safe boot banner: %w", err)
	}
	transcript = append(transcript, chunk...)

	if err := session.send("\x03"); err != nil {
		return fmt.Errorf("send safe boot interrupt: %w", err)
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 120*time.Second)
	if err != nil {
		return fmt.Errorf("safe boot prompt: %w", err)
	}
	transcript = append(transcript, chunk...)

	if err := session.send("note\n"); err != nil {
		return fmt.Errorf("send safe boot note check: %w", err)
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 10*time.Second)
	if err != nil {
		return fmt.Errorf("safe boot note check: %w", err)
	}
	transcript = append(transcript, chunk...)

	if err := session.send("1 + 1\n"); err != nil {
		return fmt.Errorf("send safe boot arithmetic check: %w", err)
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		if !hasTerminalPrompt(data, frothyPrompt) {
			return false
		}
		return strings.Contains(normalizeNewlines(data), "1 + 1\n2\nfroth>")
	}, 10*time.Second)
	if err != nil {
		return fmt.Errorf("safe boot arithmetic check: %w", err)
	}
	transcript = append(transcript, chunk...)

	text := normalizeNewlines(transcript)
	log.Output("idf.py monitor safe boot", text)
	for _, needle := range []string{
		"snapshot: found",
		"boot: CTRL-C for safe boot",
		"boot: Safe Boot, skipped restore and boot.",
		"eval error (4)",
		"1 + 1\n2",
	} {
		if !strings.Contains(text, needle) {
			return fmt.Errorf("safe boot transcript missing %q", needle)
		}
	}
	if strings.Contains(text, "parse error (") {
		return fmt.Errorf("safe boot transcript contains parse error")
	}
	return nil
}

func espIDFExportPath() (string, error) {
	home, err := sdk.FrothHome()
	if err != nil {
		return "", err
	}
	path := filepath.Join(home, "sdk", "esp-idf", "export.sh")
	if _, err := os.Stat(path); err != nil {
		return "", fmt.Errorf("ESP-IDF not found (run `froth setup esp-idf`)")
	}
	return path, nil
}

func runM10BootSetup(root string, manager *frothycontrol.Manager, log *proofLog) error {
	if _, err := evalProofFile(manager, filepath.Join(root, "tools", "frothy", "proof_m10_boot_persist.frothy"), log); err != nil {
		return err
	}
	// The source file keeps a trailing save for the raw shell preflight. Keep an
	// explicit framed SAVE here so the device proof exercises control persistence.
	value, output, err := evalDeviceBuiltin("save:", manager.Save)
	log.Eval("save:", value, output)
	if err != nil {
		return fmt.Errorf("save boot setup snapshot: %w", err)
	}
	if value != "" && value != "nil" {
		return fmt.Errorf("save boot setup snapshot value = %q, want nil", value)
	}
	return expectEvalExact(manager, "boot setup note", "note", `"armed"`)
}

func runM10BootVerify(manager *frothycontrol.Manager, log *proofLog) error {
	value, output, err := evalDeviceForm(manager, "note")
	log.Eval("note", value, output)
	if err != nil {
		return fmt.Errorf("boot note after reboot: %w", err)
	}
	if value != `"booted"` {
		return fmt.Errorf("boot note after reboot = %q, want %q", value, `"booted"`)
	}

	if err := wipeDevice(manager, log, "wipe boot snapshot"); err != nil {
		return err
	}

	if value, output, err = evalDeviceForm(manager, "note"); err == nil {
		log.Eval("note", value, output)
		return fmt.Errorf("note survived wipe with value %q", value)
	}
	log.Step("boot note absent after wipe")
	return nil
}

func wipeDevice(manager *frothycontrol.Manager, log *proofLog, label string) error {
	value, output, err := evalDeviceBuiltin("dangerous.wipe:", manager.Wipe)
	log.Eval("dangerous.wipe:", value, output)
	if err != nil {
		return fmt.Errorf("%s: %w", label, err)
	}
	if value != "" && value != "nil" {
		return fmt.Errorf("%s value = %q, want nil", label, value)
	}
	return nil
}

func runM10CellsProof(root string, manager *frothycontrol.Manager, log *proofLog) error {
	results, err := evalProofFile(manager, filepath.Join(root, "tools", "frothy", "proof_m10_cells_adc.frothy"), log)
	if err != nil {
		return err
	}
	numericSamples := 0
	for _, result := range results {
		if valueInRange(result.Value, 0, 4095) {
			numericSamples++
		}
	}
	if numericSamples < 4 {
		return fmt.Errorf("cells ADC proof expected at least four numeric samples, got %d", numericSamples)
	}
	return nil
}

func runM10BoardSurfaceProof(manager *frothycontrol.Manager, log *proofLog) error {
	if err := expectEvalExact(manager, "console info", "console.info:", "nil"); err != nil {
		return err
	}
	if err := expectEvalControlError(manager, "console default busy", "console.default!:", 26); err != nil {
		return err
	}
	for _, check := range []struct {
		name    string
		needles []string
	}{
		{"console.info", []string{"owner: target ffi", "effect: ( -- )"}},
		{"console.default!", []string{"owner: target ffi", "effect: ( -- )"}},
		{"console.uart!", []string{"owner: target ffi", "effect: ( port tx rx baud -- )"}},
		{"millis", []string{"owner: board ffi", "effect: ( -- n )"}},
		{"blink", []string{"owner: base image"}},
		{"adc.percent", []string{"owner: base image"}},
	} {
		output, err := slotInfoOutput(manager, check.name)
		log.Output("slotInfo: @"+check.name, output)
		if err != nil {
			return fmt.Errorf("slotInfo %s: %w", check.name, err)
		}
		for _, needle := range check.needles {
			if !strings.Contains(output, needle) {
				return fmt.Errorf("slotInfo %s missing %q", check.name, needle)
			}
		}
	}

	for _, source := range []string{
		"start is millis:",
		"ms: 20",
		"after is millis:",
	} {
		value, output, err := evalDeviceForm(manager, source)
		log.Eval(source, value, output)
		if err != nil {
			return err
		}
	}
	if err := expectEvalExact(manager, "millis increased", "after > start", "true"); err != nil {
		return err
	}

	for _, source := range []string{
		"gpio.output: LED_BUILTIN",
		"gpio.low: LED_BUILTIN",
	} {
		value, output, err := evalDeviceForm(manager, source)
		log.Eval(source, value, output)
		if err != nil {
			return err
		}
	}
	if err := expectEvalExact(manager, "gpio low", "gpio.read: LED_BUILTIN", "0"); err != nil {
		return err
	}
	value, output, err := evalDeviceForm(manager, "gpio.high: LED_BUILTIN")
	log.Eval("gpio.high: LED_BUILTIN", value, output)
	if err != nil {
		return err
	}
	if err := expectEvalExact(manager, "gpio high", "gpio.read: LED_BUILTIN", "1"); err != nil {
		return err
	}
	value, output, err = evalDeviceForm(manager, "gpio.toggle: LED_BUILTIN")
	log.Eval("gpio.toggle: LED_BUILTIN", value, output)
	if err != nil {
		return err
	}
	if err := expectEvalExact(manager, "gpio toggle read", "gpio.read: LED_BUILTIN", "0"); err != nil {
		return err
	}
	if err := expectEvalRange(manager, "adc.percent", "adc.percent: A0", 0, 100); err != nil {
		return err
	}

	value, output, err = evalDeviceForm(manager, "to blink with pin, count, wait [ 99 ]")
	log.Eval("to blink with pin, count, wait [ 99 ]", value, output)
	if err != nil {
		return err
	}
	output, err = slotInfoOutput(manager, "blink")
	log.Output("slotInfo: @blink", output)
	if err != nil {
		return fmt.Errorf("slotInfo blink overlay: %w", err)
	}
	if !strings.Contains(output, "owner: overlay image") {
		return fmt.Errorf("overlay blink missing from slot info")
	}
	if err := expectEvalExact(manager, "overlay blink", "blink: LED_BUILTIN, 1, 1", "99"); err != nil {
		return err
	}
	value, output, err = evalDeviceBuiltin("dangerous.wipe:", manager.Wipe)
	log.Eval("dangerous.wipe:", value, output)
	if err != nil {
		return err
	}
	output, err = slotInfoOutput(manager, "blink")
	log.Output("slotInfo: @blink", output)
	if err != nil {
		return fmt.Errorf("slotInfo blink restored: %w", err)
	}
	if !strings.Contains(output, "owner: base image") {
		return fmt.Errorf("restored blink missing base image slot info")
	}
	return nil
}

func expectEvalControlError(manager *frothycontrol.Manager, label string,
	source string, wantCode uint16) error {
	_, _, err := evalDeviceForm(manager, source)
	if err == nil {
		return fmt.Errorf("%s expected control error %d, got nil", label, wantCode)
	}

	var controlErr *frothycontrol.ControlError
	if !errors.As(err, &controlErr) {
		return fmt.Errorf("%s expected control error %d, got %v", label, wantCode, err)
	}
	if controlErr.Code != wantCode {
		return fmt.Errorf("%s expected control error %d, got %d (%s)",
			label, wantCode, controlErr.Code, controlErr.Detail)
	}
	return nil
}

func evalProofFile(manager *frothycontrol.Manager, path string, log *proofLog) ([]evalProofResult, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	forms, err := project.SplitTopLevelForms(string(data))
	if err != nil {
		return nil, fmt.Errorf("split %s: %w", path, err)
	}
	log.Step("eval " + filepath.Base(path))

	results := make([]evalProofResult, 0, len(forms))
	for _, form := range forms {
		value, output, err := evalDeviceForm(manager, form)
		log.Eval(form, value, output)
		if err != nil {
			return results, fmt.Errorf("eval %q: %w", previewProofForm(form), err)
		}
		results = append(results, evalProofResult{
			Source: form,
			Value:  value,
			Output: output,
		})
	}
	return results, nil
}

func connectDeviceManager(port string, timeout time.Duration) (*frothycontrol.Manager, *frothycontrol.DeviceInfo, error) {
	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		DefaultPort: port,
	})
	deadline := time.Now().Add(timeout)
	var lastErr error
	for {
		info, err := manager.Connect("")
		if err == nil {
			return manager, info, nil
		}
		lastErr = err
		var selectionErr *frothycontrol.ConnectSelectionError
		if errors.As(err, &selectionErr) && selectionErr.Code == "multiple_devices" {
			return nil, nil, err
		}
		if time.Now().After(deadline) {
			_ = manager.Disconnect()
			return nil, nil, lastErr
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func requireSerialPort(port string) error {
	if port == "" {
		return fmt.Errorf("serial port is required")
	}
	if _, err := os.Stat(port); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("serial port is missing: %s", port)
		}
		return err
	}
	return nil
}

func waitForSerialPortReady(port string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	var lastErr error
	for {
		busy, err := serialPortBusy(port)
		if err == nil && !busy {
			return nil
		}
		if err != nil {
			lastErr = err
		} else {
			lastErr = fmt.Errorf("serial port is busy: %s", port)
		}
		if time.Now().After(deadline) {
			return lastErr
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func serialPortBusy(port string) (bool, error) {
	if _, err := os.Stat(port); err != nil {
		if os.IsNotExist(err) {
			return false, fmt.Errorf("serial port is missing: %s", port)
		}
		return false, err
	}

	if _, err := exec.LookPath("lsof"); err != nil {
		return false, nil
	}
	cmd := exec.Command("lsof", "-t", port)
	output, err := cmd.Output()
	if err == nil {
		return strings.TrimSpace(string(output)) != "", nil
	}
	if exitErr, ok := err.(*exec.ExitError); ok && exitErr.ExitCode() == 1 {
		return false, nil
	}
	return false, err
}

func evalDeviceForm(manager *frothycontrol.Manager, source string) (string, string, error) {
	var output bytes.Buffer
	value, err := manager.Eval(source, func(data []byte) {
		_, _ = output.Write(data)
	})
	return value, output.String(), err
}

func evalDeviceBuiltin(source string, run func(func([]byte)) (string, error)) (string, string, error) {
	var output bytes.Buffer
	value, err := run(func(data []byte) {
		_, _ = output.Write(data)
	})
	return value, output.String(), err
}

func expectEvalExact(manager *frothycontrol.Manager, label string, source string, expected string) error {
	actual, _, err := evalDeviceForm(manager, source)
	if err != nil {
		return fmt.Errorf("%s: %w", label, err)
	}
	if actual != expected {
		return fmt.Errorf("%s expected %q, got %q", label, expected, actual)
	}
	return nil
}

func expectEvalRange(manager *frothycontrol.Manager, label string, source string, minimum int, maximum int) error {
	actual, _, err := evalDeviceForm(manager, source)
	if err != nil {
		return fmt.Errorf("%s: %w", label, err)
	}
	if !valueInRange(actual, minimum, maximum) {
		return fmt.Errorf("%s expected range [%d, %d], got %q", label, minimum, maximum, actual)
	}
	return nil
}

func expectLiveTransition(manager *frothycontrol.Manager, label string, source string) error {
	fmt.Fprintf(os.Stderr, "%s: hold the control now\n", label)
	if err := waitForExact(manager, label+" active", source, "true", 30*time.Second); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "%s: release the control now\n", label)
	return waitForExact(manager, label+" released", source, "false", 30*time.Second)
}

func waitForExact(manager *frothycontrol.Manager, label string, source string, expected string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	last := ""
	for time.Now().Before(deadline) {
		value, _, err := evalDeviceForm(manager, source)
		if err != nil {
			return fmt.Errorf("%s: %w", label, err)
		}
		last = value
		if value == expected {
			return nil
		}
		time.Sleep(200 * time.Millisecond)
	}
	return fmt.Errorf("%s expected %q, got %q", label, expected, last)
}

func slotInfoOutput(manager *frothycontrol.Manager, name string) (string, error) {
	var output bytes.Buffer
	value, err := manager.SlotInfo(name, func(data []byte) {
		_, _ = output.Write(data)
	})
	if err != nil {
		return output.String(), err
	}
	if value != "" && value != "nil" {
		return output.String(), fmt.Errorf("slotInfo value = %q, want nil", value)
	}
	return output.String(), nil
}

func valueInRange(value string, minimum int, maximum int) bool {
	actual, err := strconv.Atoi(strings.TrimSpace(value))
	return err == nil && actual >= minimum && actual <= maximum
}

func previewProofForm(source string) string {
	compact := strings.Join(strings.Fields(source), " ")
	if len(compact) <= 80 {
		return compact
	}
	return compact[:77] + "..."
}

func (l *proofLog) Step(text string) {
	l.lines = append(l.lines, "==> "+text)
}

func (l *proofLog) Eval(source string, value string, output string) {
	l.lines = append(l.lines, "$ "+previewProofForm(source))
	if strings.TrimSpace(output) != "" {
		l.lines = append(l.lines, strings.TrimRight(output, "\n"))
	}
	if value != "" && value != "nil" {
		l.lines = append(l.lines, value)
	}
}

func (l *proofLog) Output(label string, output string) {
	l.lines = append(l.lines, "$ "+label)
	if strings.TrimSpace(output) != "" {
		l.lines = append(l.lines, strings.TrimRight(output, "\n"))
	}
}

func (l *proofLog) String() string {
	if len(l.lines) == 0 {
		return ""
	}
	return strings.Join(l.lines, "\n") + "\n"
}

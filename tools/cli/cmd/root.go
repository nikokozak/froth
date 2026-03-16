package cmd

import (
	"fmt"
	"os"
)

// Global flags
var (
	portFlag   string
	targetFlag string
	serialFlag bool
	daemonFlag bool
)

// Execute parses os.Args and dispatches to the right subcommand.
func Execute() error {
	args := os.Args[1:]
	var remaining []string

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--port":
			if i+1 < len(args) {
				portFlag = args[i+1]
				i++
			}
		case "--target":
			if i+1 < len(args) {
				targetFlag = args[i+1]
				i++
			}
		case "--serial":
			serialFlag = true
		case "--daemon":
			daemonFlag = true
		default:
			remaining = append(remaining, args[i])
		}
	}

	if len(remaining) == 0 {
		printUsage()
		return nil
	}

	switch remaining[0] {
	case "info":
		return runInfo()
	case "send":
		if len(remaining) < 2 {
			return fmt.Errorf("send requires a source argument")
		}
		return runSend(remaining[1])
	case "doctor":
		return runDoctor()
	case "build":
		return runBuild()
	case "flash":
		return runFlash()
	case "daemon":
		return runDaemon(remaining[1:])
	default:
		return fmt.Errorf("unknown command: %s", remaining[0])
	}
}

func printUsage() {
	fmt.Println("Usage: froth [flags] <command>")
	fmt.Println()
	fmt.Println("Commands:")
	fmt.Println("  doctor          Check environment and device")
	fmt.Println("  build           Build Froth for a target")
	fmt.Println("  flash           Flash device (ESP-IDF targets)")
	fmt.Println("  info            Show device info")
	fmt.Println("  send <src>      Evaluate Froth source on device")
	fmt.Println("  daemon <cmd>    Manage background daemon (start|stop|status)")
	fmt.Println()
	fmt.Println("Flags:")
	fmt.Println("  --port <path>    Serial port (auto-detect if omitted)")
	fmt.Println("  --target <name>  Build target: posix (default), esp-idf")
	fmt.Println("  --serial         Force direct serial (skip daemon)")
	fmt.Println("  --daemon         Force daemon routing (fail if not running)")
}

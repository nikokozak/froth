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
	cleanFlag  bool
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
		case "--clean":
			cleanFlag = true
		default:
			remaining = append(remaining, args[i])
		}
	}

	if len(remaining) == 0 {
		printUsage()
		return nil
	}

	switch remaining[0] {
	case "new":
		return runNew(remaining[1:])
	case "info":
		return runInfo()
	case "connect":
		return runConnect(remaining[1:])
	case "send":
		fileArg := ""
		if len(remaining) >= 2 {
			fileArg = remaining[1]
		}
		return runSend(fileArg)
	case "doctor":
		return runDoctor()
	case "build":
		return runBuild()
	case "flash":
		return runFlash()
	case "daemon":
		return runDaemon(remaining[1:])
	case "reset":
		return runReset()
	default:
		return fmt.Errorf("unknown command: %s", remaining[0])
	}
}

func printUsage() {
	fmt.Println("Usage: froth [flags] <command>")
	fmt.Println()
	fmt.Println("Commands:")
	fmt.Println("  new <name>      Create a new Froth project")
	fmt.Println("  doctor          Check environment and device")
	fmt.Println("  build           Build Froth firmware")
	fmt.Println("  flash           Flash device (ESP-IDF targets)")
	fmt.Println("  connect         Connect to Froth (local POSIX for now)")
	fmt.Println("  send [file]     Send source to device (resolves includes)")
	fmt.Println("  info            Show device info")
	fmt.Println("  reset           Reset device to stdlib baseline")
	fmt.Println("  daemon <cmd>    Manage background daemon (start|stop|status)")
	fmt.Println()
	fmt.Println("Flags:")
	fmt.Println("  --port <path>    Serial port (auto-detect if omitted)")
	fmt.Println("  --target <name>  Board target (for new/build)")
	fmt.Println("  --serial         Force direct serial (skip daemon)")
	fmt.Println("  --daemon         Force daemon routing")
	fmt.Println("  --clean          Delete the build directory before building")
}

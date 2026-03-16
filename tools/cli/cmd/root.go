package cmd

import (
	"fmt"
	"os"
)

// Global flags
var portFlag string

// Execute parses os.Args and dispatches to the right subcommand.
func Execute() error {
	// Parse global flags and subcommand from os.Args.
	//
	// Supported global flags:
	//   --port <path>   Serial port path (e.g. /dev/tty.usbserial-1234).
	//                   If omitted, auto-detect by probing available ports.
	//
	// Subcommands:
	//   info            Connect, send HELLO, print device info.
	//   send <source>   Connect, send EVAL_REQ with <source>, print result.
	//
	// Algorithm:
	// 1. Walk os.Args[1:] looking for "--port" followed by a value.
	//    Store in portFlag. Remove both from the args slice.
	// 2. The first remaining non-flag arg is the subcommand name.
	// 3. Switch on the subcommand:
	//    - "info"  -> call runInfo()
	//    - "send"  -> next arg is the source string, call runSend(source)
	//    - ""      -> print usage and return nil
	//    - unknown -> return fmt.Errorf("unknown command: %s", name)

	args := os.Args[1:]
	var remaining []string

	for i := 0; i < len(args); i++ {
		if args[i] == "--port" && i+1 < len(args) {
			portFlag = args[i+1]
			i++ // skip the value
		} else {
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
	default:
		return fmt.Errorf("unknown command: %s", remaining[0])
	}
}

func printUsage() {
	fmt.Println("Usage: froth [--port <path>] <command>")
	fmt.Println()
	fmt.Println("Commands:")
	fmt.Println("  info          Show device info (HELLO handshake)")
	fmt.Println("  send <src>    Evaluate Froth source on device")
	fmt.Println()
	fmt.Println("Flags:")
	fmt.Println("  --port <path> Serial port (auto-detect if omitted)")
}

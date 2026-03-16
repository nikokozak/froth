package cmd

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/nikokozak/froth/tools/cli/internal/daemon"
	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runSend(source string) error {
	if !serialFlag {
		err := runSendDaemon(source)
		if err == nil {
			return nil
		}
		if daemonFlag {
			return fmt.Errorf("daemon: %w", err)
		}
	}
	return runSendSerial(source)
}

func runSendDaemon(source string) error {
	client, err := daemon.Dial()
	if err != nil {
		return err
	}
	defer client.Close()

	client.EventHandler = func(method string, params json.RawMessage) {
		if method == daemon.EventConsole {
			var evt daemon.ConsoleEvent
			json.Unmarshal(params, &evt)
			fmt.Print(evt.Text)
		}
	}

	result, err := client.Eval(source)
	if err != nil {
		return fmt.Errorf("eval: %w", err)
	}

	if result.Status == 0 {
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
}

func runSendSerial(source string) error {
	sess, err := session.Connect(portFlag)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer sess.Close()

	sess.SetPassthrough(os.Stdout)

	result, err := sess.Eval(source)
	if err != nil {
		return fmt.Errorf("eval: %w", err)
	}

	if result.Status == 0 {
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
}

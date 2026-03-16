package cmd

import (
	"fmt"
	"os"

	"github.com/nikokozak/froth/tools/cli/internal/session"
)

func runSend(source string) error {
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

	// 3. Print the result.
	//    On success: print stack repr (if non-empty).
	//    On error: print error code, fault word.
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

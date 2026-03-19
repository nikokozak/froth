# Froth Editor Test Battery — 2026-03-18

Covers the VS Code extension, daemon, and their interaction with real hardware and local POSIX targets. Designed to find breaks, not confirm happy paths.

## Prerequisites

- ESP32 DevKit V1 connected via USB
- Latest firmware flashed (with FROTH_HAS_LINK)
- CLI built: `cd tools/cli && go build -o froth-cli .`
- Extension packaged and installed: `code --install-extension tools/vscode/froth-0.0.2.vsix`
- No daemon running at start (`froth-cli daemon stop` or `pkill froth-cli`)
- No minicom or other serial monitor holding the port
- Create a test file `test.froth` with at least 5-10 lines of definitions

## 1. Cold Start — No Daemon

1. Open VS Code. Open a folder containing a `.froth` file.
OK.
2. Check the status bar. Should show "Froth: Idle" (not "No Daemon", not "Connected").
OK.
3. Check the Froth sidebar (activity bar icon). Should show the viewsWelcome content: "Connect Device", "Try Local", "Run Doctor".
OK.
4. Press Cmd+Enter on a line of Froth code. The extension should start the daemon automatically, connect to the ESP32, and evaluate the line. Check: did the daemon start? Did it find the device? Did the result appear in the Froth Console?
First run attempt results in "NO DAEMON", second attempt works. Same timing issues as always, has not been resolved.
5. Check the status bar again. Should show the board name (e.g., "Froth: esp32-devkit-v1").
Yes, shows.
6. Check the sidebar. Should show device metadata: Target, Board, Port, Heap, Slots, Cell Bits.
Yes, shows.

## 2. Cold Start — Local Mode

1. Stop the daemon (`froth-cli daemon stop`). Disconnect the ESP32 (unplug USB).
Immediately after, in editor, this popup appears: Froth daemon started, but the extension could not connect to it. "No Daemon"
2. Click "Try Local" in the sidebar viewsWelcome.
Try local results in the Daemon turning back on, but attempting to find an ESP32. Then gets stuck in reconnect attempt loop. Re-clicking simply re-starts the loop.
3. Check: does the daemon start in local mode? Does the status bar show "Froth: Local POSIX"?\e
No - see above, no matter how much is attempted, doesn't. In fact, quitting Code, starting again (Froth IDLE), then clicking Try Local will immediately result in a "Froth: No Daemon". Clicking the "Try Local" button multiple times results in this popup: A Froth daemon is already running in serial mode. Stop it before switching to local mode. The output console then enters the reconnect loop. "Froth No Device".
4. Select `1 2 +` in the editor. Press Cmd+Enter. Should show `[3]` in the console.
FAIL see above
5. Check the sidebar. Target should say "Local POSIX". Port should say "stdin/stdout".
FAIL see above

## 3. Target Switching

CANNOT TEST UNTIL LOCAL MODE IS FIXED.

1. With local mode running, plug in the ESP32.
2. Click "Connect Device" (or use the command palette).
3. If the extension started the daemon, it should stop it and restart in serial mode. If someone else started it, it should warn.
4. Check: does the status bar switch to the ESP32 board name? Does the sidebar update?
5. Stop the daemon. Click "Try Local" again. Check: does it switch back to local mode cleanly?

## 4. Send Selection

1. Connected to a target (either serial or local).
SERIAL
2. Type `1 2 +` on a line. Place cursor on that line (no selection). Press Cmd+Enter.
PASS
3. Check: sends the current line. Console shows `[3]`.
PASS
4. Select `: foo dup + ;` across a line. Press Cmd+Enter. Console shows `[]`.
PASS
5. Select `5 foo` and Cmd+Enter. Console shows `[10]`.
PASS
6. Select nothing on an empty line. Press Cmd+Enter. Should do nothing (no error, no eval).
PASS
7. Select whitespace only. Press Cmd+Enter. Should do nothing.
PASS

## 5. Send File

1. Open `test.froth` containing several definitions (e.g., `: double dup + ;` and `: triple dup dup + + ;`).
PASS
2. Press Cmd+Shift+Enter (Send File).\e
PASS
3. Check: Console should show "[froth] reset" then "[froth] evaluating test.froth" then the result.\e
PASS (PARTIAL) - tried by adding a bunch of \n into the file (adding essentially 10 empty lines), lines are sent despite them being empty, which messes with console output.
Functionally OK.
4. Verify definitions exist: select `5 double` and Cmd+Enter. Should show `[10]`.
PASS
5. Now edit `test.froth`: change `: double dup + ;` to `: double 3 * ;`.
PASS
6. Press Cmd+Shift+Enter again.
PASS
7. Check: device is reset, new definitions loaded. `5 double` should now return `[15]`, not `[10]`.
PASS
8. Remove a definition from the file entirely. Send File again. The removed word should be gone (error on use).
PASS

## 6. Send File — Large File (Chunking)

1. Create a file with 30+ short definitions (one per line). Should exceed 253 bytes total.
PASS
2. Send File (Cmd+Shift+Enter).\e
PASS
3. Check: all definitions should be available. Test the first, middle, and last word.
PASS
4. Create a file with a multi-line definition that spans more than 5 lines (with `: ... ;`). Send File. The multi-line form should not be split across chunks.
PASS

## 7. Send File — While Loop (Long-Running Program)

1. Create a file with:

   ```
   2 1 gpio.mode
   [ -1 ] [ 2 1 gpio.write 500 ms 2 0 gpio.write 500 ms ] while
   ```

2. Send File (Cmd+Shift+Enter).
PASS
3. Check: the LED should start blinking. The status bar should show "Froth: Running" with a spin icon. The sidebar should show "Program running..." with only an Interrupt option.
PASS
4. Check: the Froth Console should show "[froth] reset" and "[froth] evaluating ...". It should NOT show a timeout error.
PASS
5. Try pressing Cmd+Enter on another line while running. Should show "Target is running a program. Use Interrupt first."
FAIL - no "message". Action is blocked correctly.
6. Try Send File again while running. Same rejection message.
FAIL - no "message". Action is blocked correctly.
7. Try Reset, Save, or Wipe from the overflow menu while running. Same rejection.
FAIL - no "message". Actions are blocked correctly.

## 8. Interrupt

1. While the LED is blinking (from test 7):
2. Click the red "Interrupt" item in the status bar (or use the sidebar/view title button).
PASS
3. Check: the LED should stop. Console should show "[froth] interrupt sent". The status bar should return to "Froth: Connected" (or board name). The sidebar should return to normal with all actions available.
PASS
4. Send a selection (`1 2 +`). Should work normally. Check that the system recovered cleanly.
PASS

## 9. Interrupt — Timing Edge Cases

1. Send a file with a short while loop: `[ -1 ] [ 100 ms ] while`.
PASS
2. Immediately click Interrupt (within 1 second).
PASS
3. Check: should stop. No hang, no COBS corruption.
PASS
4. Send `1 2 +` after. Should work.
PASS
5. Send the while loop file again. Wait 10 seconds. Click Interrupt. Should still work.
PASS
6. Send `1 2 +` after. Should work.
PASS

## 10. Console Output (s.emit)

1. Select `"Hello from Froth" s.emit` and Cmd+Enter.
PASS
2. Check: "Hello from Froth" should appear in the Froth Console output, followed by the stack repr.
FALSE, only stack repr.
3. Send a file that contains:
PASS

   ```
   : greet "Hello " s.emit "World" s.emit ;
   greet
   ```

4. Check: "Hello World" should appear in the console.\e
FALSE, only stack repr.
5. While a while loop is running that does `s.emit`, check that console output streams in real-time (not batched at the end).
FALSE, no "emit" output.

## 11. Reset, Save, Wipe via Sidebar

1. Define a word: select `: keeper 42 ;` and Cmd+Enter.
PASS
2. Click Reset in the sidebar overflow menu. Console should show "[froth] reset".
PASS
3. Select `keeper` and Cmd+Enter. Should error (undefined word after reset).
PASS
4. Define `keeper` again. Click Save. Console should show "[froth] save: ok".
PASS
5. Click Reset. `keeper` should be gone.
PASS
6. Disconnect and reconnect (or restart daemon). `keeper` should be restored from snapshot.
PASS
7. Click Wipe. Console should show "[froth] wipe: ok".
PASS
8. `keeper` should now be gone permanently.
PASS

## 12. Device Disconnect During Operation

1. Connected to ESP32. Send a selection to confirm working.
PASS
2. Unplug the USB cable.\e
PASS
3. Check: status bar should change to "Froth: No Device" or "Froth: Reconnecting". Console should show "[froth] target disconnected" or "[froth] reconnecting...".
PASS
4. Plug the cable back in. Wait a few seconds.
PASS
5. Check: status bar should return to connected state with board name. Console should show "[froth] target connected: ...".\e
PASS
6. Send a selection. Should work.
PASS

## 13. Device Disconnect During While Loop

1. Start a while loop via Send File.
PASS
2. While LED is blinking, unplug the USB cable.\e
PASS
3. Check: the "running" state should transition to "disconnected". No hang. No infinite wait.\e
PASS
4. Plug back in. Wait for reconnect.
PASS
5. Send a selection. Should work.\e
PASS

## 14. Daemon Crash / Stop During Use

1. Connected and working.
PASS
2. In a terminal, run `froth-cli daemon stop`.
PASS
3. Check: status bar should change to "Froth: No Daemon". Console should show "[froth] daemon connection lost".\e
PASS
4. Try Cmd+Enter. The extension should restart the daemon automatically and reconnect.\e
FAIL. On first try: Froth daemon started, but the extension could not connect to it. Works on second try.
5. Check: should work after a few seconds (daemon start + HELLO handshake).
PASS (see above)

## 15. Multiple VS Code Windows

1. Open two VS Code windows, both with `.froth` files.
2. In window 1, send a selection. Should work.
3. In window 2, send a selection. Should work (both are RPC clients to the same daemon).
4. In window 1, start a while loop. Status should show "Running".
5. In window 2, try to send a selection. What happens? (The daemon's `reqMu` will block until the while loop finishes or is interrupted.)
6. From window 2, click Interrupt. The while loop in window 1 should stop.

## 16. CLI and Extension Simultaneously

1. Daemon running, extension connected.
PASS
2. In a terminal, run `froth-cli send "1 2 +"`. Should work (routes through daemon).
PASS
3. In VS Code, send a selection. Should also work.
PASS
4. From CLI, run `froth-cli reset`. Check: extension sidebar should update (if info is refreshed).
PASS
5. From CLI, start a long eval. In VS Code, try to send. Should block or report busy.
Does not block - from VSCode sends eval (i.e. 1 2 +), when through cli infinite loop was started. Upon sending 1 2 +, state in VSCode changes to "busy" state, and shows interrupt button, which in theory isn't terrible, though warning the user might be a nice idea. Potentially I didn't wait long enough for an internal timeout to occur and VSCode to update, but still, executed about 3 seconds afterwards. Not the end of the world.

## 17. Editor Title Buttons

1. Open a `.froth` file. Check: the editor title bar should show Send Selection and Send File buttons (play icon and file-code icon).
PASS
2. Open a `.js` or `.md` file. Check: the buttons should NOT appear (scoped to `.froth`).
PASS
3. Click the Send Selection button in the title bar. Should work the same as Cmd+Enter.
PASS
4. Click the Send File button. Should work the same as Cmd+Shift+Enter.
PASS

## 18. View Title Actions

1. Check the Froth Device sidebar view title. Should show Interrupt and Refresh as icon buttons.
PASS
2. Click the overflow menu (...). Should show Reset, Save, Wipe, Run Doctor.
PASS
3. Click Refresh. Sidebar metadata should update.
PASS
4. Click Run Doctor. Should open a terminal running `froth doctor`.
FAIL - terminal opens for a microsecond, info shows up, and then closes itself immediately. Unusable.

## 19. Status Bar Interaction

1. In "Idle" state, click the status bar item. Should trigger Connect Device.
FAIL. Takes two clicks, first click shows No Daemon, second click connects (similar to errors in previous tests).
2. In "Connected" state, note the board name is shown.
PASS
3. In "Running" state, check that the red Interrupt item appears next to the status item.
PASS
4. In "No Daemon" state, click the status bar item. Should trigger Connect Device (which starts the daemon).
PASS

## 20. Error Recovery Gauntlet

1. Send `notaword`. Should show `error(4)` in console. System stays functional.
PASS
2. Send `1 drop drop`. Should show stack underflow error. System stays functional.
PASS/PARTIAL (shows error(2) in "perm"). This is after sending 1 drop drop *twice*. The first time (on an empty stack), nothing happens, empty stack returned.
3. Send an extremely long line (>500 characters of valid Froth). Should either chunk or error cleanly, not crash.
You need to provide me with this.
4. Send a file with a syntax error (unclosed bracket). Should show reader error, not hang.
PASS (error(101))
5. Send a file that fills the heap. Should show heap overflow error, system stays functional after reset.
Again, I need a file.
6. Rapid-fire: press Cmd+Enter 10 times quickly on `1`. Each should produce a result (stack accumulates). No hangs, no corruption.
PASS

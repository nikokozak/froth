# Froth System Test Battery — 2026-03-18

Run through each section in order. Mark each item pass/fail. Notes column is for anything unexpected.

## Prerequisites

- ESP32 DevKit V1 connected via USB (`/dev/cu.usbserial-0001` or similar)
- Latest firmware flashed (`cd targets/esp-idf && idf.py -p PORT flash`)
- Go CLI built (`cd tools/cli && go build -o froth-cli .`)
- VS Code extension compiled (`cd tools/vscode && npm run compile`)
- No stale daemon running (`froth-cli daemon stop` or check `~/.froth/daemon.sock`)

## 1. Direct REPL (minicom or serial terminal)

Connect with `minicom -D /dev/cu.usbserial-0001 -b 115200` (or `idf.py monitor --no-reset`). These tests verify the core REPL experience is intact.

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 1.1 | Type `1 2 + .` Enter | Prints `3` | pass | |
| 1.2 | Type `0xFF .` Enter | Prints `255` | pass | |
| 1.3 | Type `0b1010 .` Enter | Prints `10` | pass | |
| 1.4 | Type `"Hello" s.emit` Enter | Prints `Hello` | pass | |
| 1.5 | Type `"Hello" s.len .` Enter | Prints `5` | pass | |
| 1.6 | Type `: double dup + ;` Enter, then `5 double .` Enter | Prints `10` | pass | |
| 1.7 | Type `: count dup 0 > [ dup . 1 - count ] [ drop ] if ;` Enter, then `5 count` Enter | Prints `5 4 3 2 1` | pass | |
| 1.8 | Type `10 [ dup 5 > ] [ 1 - ] while .` Enter | Prints `5` | pass | |
| 1.9 | Type `[ 1 ] [ ] while` then Ctrl-C | Prints `error(14): interrupted in "while"`, prompt returns | pass | |
| 1.10 | Type `[ 1 drop drop ] catch .` Enter | Prints `2` (stack underflow code) | pass | |
| 1.11 | Type `5 >r 10 r> + .` Enter | Prints `15` | fails | In what world does '+' add numbers on the RS? This returns stack underflow, correctly. |
| 1.12 | Type `.s` Enter (with values on stack) | Shows stack contents | pass | |
| 1.13 | Type `words` Enter | Lists all defined words | pass | we should add a '\n' after a certain N of words so that they're formatted more nicely (otherwise the print line is huge)|
| 1.14 | Type `'+ see` Enter | Shows `<primitive>` and stack effect | pass | with prims, we should get rid of the empty space where otherwise stack effects go. (stack effect not printing for prims?) |
| 1.15 | Type `info` Enter | Shows version, heap, slots | pass | |
| 1.16 | Multi-line: type `: triple` Enter, see `..` prompt, type `dup dup + +` Enter, type `;` Enter, then `3 triple .` Enter | Prints `9` | pass | |
| 1.17 | Backspace works (type `123`, backspace, type `4`, Enter) | Stack shows `[124]` | pass | |

## 2. Persistence (direct REPL)

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 2.1 | Type `: keeper 42 ;` then `save` | No error | pass | |
| 2.2 | Power cycle ESP32 (unplug/replug or EN reset) | | pass | |
| 2.3 | Reconnect terminal, type `keeper .` | Prints `42` | pass | |
| 2.4 | Type `wipe` | Prints `reset` | pass | |
| 2.5 | Type `keeper` | Error: undefined word | pass | |
| 2.6 | Type `save`, power cycle, reconnect, type `words` | Only base words, no `keeper` | pass | |

## 3. GPIO (direct REPL on ESP32)

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 3.1 | Type `2 1 gpio.mode` Enter | No error (sets pin 2 as output) | pass | |
| 3.2 | Type `2 1 gpio.write` Enter | LED on pin 2 lights up | pass | |
| 3.3 | Type `2 0 gpio.write` Enter | LED turns off | pass | |
| 3.4 | Type `[ -1 ] [ 2 1 gpio.write 500 ms 2 0 gpio.write 500 ms ] while` | LED blinks, Ctrl-C stops it | pass | |

## 4. Reset (direct REPL)

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 4.1 | Type `: temp 99 ;` then `dangerous-reset` | Prints `reset` | pass | |
| 4.2 | Type `temp` | Error: undefined word | pass | |
| 4.3 | Type `info` — check heap/slots back to baseline | Should match fresh boot values | pass | |

## 5. Safe Boot

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 5.1 | Define and save a word, then power cycle. Press Ctrl-C during "boot: CTRL-C for safe boot" | Prompt appears, saved word is NOT restored | pass | add \n after safe boot message |
| 5.2 | Power cycle again without Ctrl-C | Saved word IS restored | pass | |

## 6. CLI — Direct Serial

Close the terminal before running CLI commands (only one process can own the serial port).

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 6.1 | `froth-cli doctor` | Lists Go, cmake, serial ports, ESP-IDF status | pass | |
| 6.2 | `froth-cli --serial --port PORT info` | Shows device version, board, heap, slots | pass | |
| 6.3 | `froth-cli --serial --port PORT send "1 2 +"` | Returns `[3]` | pass | |
| 6.4 | `froth-cli --serial --port PORT send ": foo dup + ;"` | Returns `[]` | pass | |
| 6.5 | `froth-cli --serial --port PORT send "5 foo"` | Returns `[10]` | pass | |
| 6.6 | `froth-cli --serial --port PORT reset` | Returns `Reset result: OK [0]` with baseline heap/slots | pass | |
| 6.7 | `froth-cli --serial --port PORT send "5 foo"` | Returns `error(4)` (foo gone) | pass | |
| 6.8 | `froth-cli --serial --port PORT send '"Hello ESP32" s.emit'` | Console output includes `Hello ESP32`, stack shown | pass | |
| 6.9 | `froth-cli --serial --port PORT send "0xFF"` | Returns `[255]` | pass | |

## 7. CLI — Daemon Path

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 7.1 | `froth-cli daemon start &` (or in separate terminal) | Daemon starts, finds device | pass | |
| 7.2 | `froth-cli daemon status` | Shows running + connected | pass | |
| 7.3 | `froth-cli info` (no --serial flag) | Routes through daemon, shows device info | pass | |
| 7.4 | `froth-cli send "1 2 +"` | Returns `[3]` through daemon | pass | |
| 7.5 | `froth-cli send ": bar 3 * ;"` then `froth-cli send "5 bar"` | Returns `[15]` | pass | |
| 7.6 | `froth-cli reset` | Returns `Reset result: OK [0]` | pass | |
| 7.7 | `froth-cli send "5 bar"` | Returns `error(4)` | pass | |
| 7.8 | Disconnect USB, wait 3s, reconnect USB, wait 3s, `froth-cli info` | Daemon reconnects, info works | pass | |
| 7.9 | `froth-cli daemon stop` | Daemon stops cleanly | pass | |

## 8. VS Code Extension

Start the daemon first (`froth-cli daemon start`). Open VS Code in the Froth project directory. The extension activates on startup.

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 8.1 | Status bar shows connection state | Should show connected (or connecting/disconnected) | pass | |
| 8.2 | Open a `.froth` file, select `1 2 +`, press Cmd+Enter | "Froth Console" output shows `[3]` | pass | |
| 8.3 | Select `: baz 42 ;`, Cmd+Enter, then select `baz`, Cmd+Enter | Console shows `[]` then `[42]` | pass | |
| 8.4 | "Froth: Send File" (Cmd+Shift+P) on a `.froth` file | File content sent, results in console | FAIL | Eval failed: cobs decode: cobs: truncated block (need 90 bytes, have 15) when trying to send file `tests/test.froth` |
| 8.5 | Select `[ 1 ] [ ] while`, Cmd+Enter, then Ctrl-C in terminal | Device interrupts, error in console | FAIL | VSCODE only shows "Console", no terminal into which we can write/execute direct commands (like CTRL-C) |
| 8.6 | Stop daemon while extension is connected | Status bar changes to disconnected | pass | |
| 8.7 | Restart daemon | Extension reconnects, status bar updates | pass | |

## 9. Reset + Eval Workflow (the "Send File" pattern)

This is the critical editor workflow from ADR-037.

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 9.1 | `froth-cli send ": app 1 2 + . ;"` | `[]` | pass | |
| 9.2 | `froth-cli send "app"` | Prints `3`, returns `[]` | FAIL - if VSCODE client connected as well, daemon returns "no device connected" | |
| 9.3 | `froth-cli reset` | OK, baseline state | | |
| 9.4 | `froth-cli send ": app 10 20 + . ;"` (redefined) | `[]` | | |
| 9.5 | `froth-cli send "app"` | Prints `30` (new version, not old) | | |
| 9.6 | `froth-cli reset` then `froth-cli send "app"` | `error(4)` — clean slate | | |

## 10. Edge Cases and Error Recovery

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 10.1 | `froth-cli send ": boom boom ; [ boom ] catch ."` | Returns `18` (call depth) | | |
| 10.2 | `froth-cli send "[ 5 >r ] catch ."` | Returns `15` (RS imbalance) | | |
| 10.3 | `froth-cli send "notaword"` | Returns `error(4)` (undefined) | | |
| 10.4 | `froth-cli send "1 drop drop"` | Returns error (stack underflow) | | |
| 10.5 | Send very long expression (200+ chars) | Should work or return clean error | | |
| 10.6 | Rapid-fire: run 10 `send "1 2 +"` in quick succession | All return `[3]` (stop-and-wait correct) | | |

## 11. POSIX Build Sanity

Quick check that the POSIX build still works independently.

```
cd build64 && cmake .. -DFROTH_CELL_SIZE_BITS=32 && make
```

| # | Test | Expected | Pass? | Notes |
|---|------|----------|-------|-------|
| 11.1 | `(sleep 1; printf '1 2 + .\n\x04') \| timeout 5 ./Froth` | Prints `3` | | |
| 11.2 | `(sleep 1; printf ': foo dup + ;\n5 foo .\n\x04') \| timeout 5 ./Froth` | Prints `10` | | |
| 11.3 | Save/restore cycle (define, save, restart, verify) | Word survives | | |

---

## Summary

| Section | Total | Pass | Fail | Skip |
|---------|-------|------|------|------|
| 1. Direct REPL | 17 | | | |
| 2. Persistence | 6 | | | |
| 3. GPIO | 4 | | | |
| 4. Reset | 3 | | | |
| 5. Safe Boot | 2 | | | |
| 6. CLI Serial | 9 | | | |
| 7. CLI Daemon | 9 | | | |
| 8. VS Code | 7 | | | |
| 9. Send File | 6 | | | |
| 10. Edge Cases | 6 | | | |
| 11. POSIX | 3 | | | |
| **Total** | **72** | | | |

# Diagnosing serial output with raw capture

Raw capture saves exact received serial bytes for diagnosing output, encoding,
and ANSI-color issues. For readable logs, use **Options → Log** instead.

## Start and stop

1. Connect to the device.
2. Choose **Options → Raw Capture → Start...** and select a `.bin` file.
3. Reproduce the output, for example by resetting the board.
4. Choose **Options → Raw Capture On → Stop** when finished.

The menu shows **Raw Capture On** while recording. The byte count and filename
appear above the terminal; hover the filename for its full path. Capture starts
when you confirm the file dialog, so output received before that is not included.

## What the file contains

Raw capture records received bytes as the session drains its RX queue, before
ANSI parsing, UTF-8 replacement, or newline handling. It retains escape sequences,
NUL, CR/LF, invalid UTF-8, and incomplete characters exactly as received, with no
timestamps, line numbers, or transmitted keyboard/response bytes. Normal text
logging is separate: text logs use UTF-8 and native line endings (LF on Linux,
CRLF on Windows). Capture continues through terminal Clear, disconnects,
reconnects, and port changes until stopped or the application exits; stop it
before switching devices if you want separate files. A new capture replaces the
selected file after the save dialog's overwrite confirmation. A write error
stops raw capture and reports the error while the terminal continues receiving.

## Inspecting a capture

From the repository root, inspect the first 4096 bytes without executing their
terminal escape sequences:

```sh
python3 scripts/inspect_raw_capture.py boot.bin
python3 scripts/inspect_raw_capture.py boot.bin --sgr --length 65536
```

The [inspection helper](../scripts/inspect_raw_capture.py) requires Python 3.
Use `--offset 0x1000 --length 4096` to inspect another range. The SGR listing
shows byte offsets and exact color/format parameters: `32` selects normal green,
`0;32` resets formatting and selects green, `0` resets formatting, and `39`
selects the terminal's default foreground. The raw file can be shared unchanged
to reproduce a parsing/color issue; text logs and screenshots cannot retain that
evidence.

## Understanding color differences

imterm uses the ANSI color sequences sent by the device. A log prefix such as
`I` or `W` does not select a color on its own. After an `ESC[0m` reset, text uses
the default foreground until another color sequence arrives. If a capture
contains plain text in that interval, default-colored output is expected.

See [terminal protocol semantics](terminal-semantics.md) for the supported ANSI
commands and UTF-8 display policy.

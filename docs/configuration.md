# Configuration

imterm reads `imterm.toml` from the directory you launch it from. Startup opens
**Setup** with the saved values. Edit the connection settings and click **Save**
to connect. Launch from the same directory to reuse your settings.

## Loading and defaults

A missing file is created with defaults. Partial or malformed files fall back to
default values. Invalid fields use their defaults while valid fields survive;
malformed TOML uses all defaults. Hover **Configuration notice (hover)** in Setup
for the affected keys and recovery instructions. Loading does not overwrite an
existing malformed file. Configuration larger than 1 MiB is rejected.

The reader accepts unversioned files, numeric strings, and display labels such
as `None`, `Hardware`, and `Add CR to LF`.

## File format

Saving writes the version 1 format:

```toml
version = 1

[default.serial]
serial_port = 'COM1'
baud = 115200
data_bits = 8
stop_bits = '2'
parity = 'none'
flow_control = 'none'

[default.input]
new_line_mode = 'strict'

[default.logging]
enabled = false
line_numbers = true
timestamps = true
path = '/home/you/imterm/logs'

[default.display]
zoom_factor = 1.0
```

The default log path is `imterm/logs` beneath the user's home directory, including
on Windows. The example path is illustrative; **Choose Path** selects a directory
for the current machine. Serial defaults are **COM1, 115200 baud, 8 data bits,
2 stop bits, no parity, no flow control**. Match them to your device; most ESP32
consoles use 1 stop bit. Logging is disabled by default.

## Accepted settings

| Setting | Accepted values and effect |
| --- | --- |
| Serial port | A nonempty name/path, at most 4096 bytes, without control characters. A saved unplugged port remains selectable and produces an open error if unavailable. |
| Baud | A whole decimal number from 1 through 4294967295. Signs, trailing text, decimals, and overflow are rejected. The device/driver may reject unsupported rates. |
| Data bits | 5, 6, 7, or 8. |
| Stop bits | `1`, `1.5`, or `2`. |
| Parity | `none`, `odd`, `even`, `mark`, or `space`. |
| Flow control | `none`, `software`, or `hardware`; passed to the native transport. Hardware flow control disables manual RTS. |
| Newline mode | `strict` for normal CR/LF handling, `add_cr_to_lf` for LF-only device output, or `add_lf_to_cr` for CR-only output. Affects received text, not transmitted keyboard or paste bytes. |
| Logging | Boolean `enabled`, `line_numbers`, and `timestamps`; control text logging. Raw RX capture is separate. |
| Log path | A nonempty directory path, at most 4096 bytes, without control characters. Existing regular files and inaccessible paths are rejected; missing directories are created when logging starts. Relative paths resolve from the working directory. |
| Zoom | A positive finite number, normalized to the nearest supported zoom preset. |

## Applying and saving changes

Setup edits take effect when you click **Save**. Save validates the fields,
starts the connection, applies logging/path and newline preferences, and saves
them even if opening the selected device later fails. Invalid baud input leaves
Setup open with an actionable error. Serial parameters take effect on the new
connection.

Newline mode, logging options, and zoom changed through menus/shortcuts take
effect immediately and are saved. Choosing another port through the connection
menu also saves that choice. Logger formatting options survive restart. A file
save failure leaves the selected settings active for the current session and
shows the failure in Setup and above the terminal; fix the reported filesystem
problem and Save again. The logger needs permission to create directories and
write files at the selected log path.

Terminal-view line numbers/timestamps, auto-scroll, auto reconnect, and manual
DTR/RTS requests are session controls; they are not stored in `imterm.toml`.
The line-number/timestamp settings under `default.logging` affect log files.

## Backups and recovery

Saves write and close a private sibling temporary file before replacing the
configuration. The previous file is copied to `imterm.toml.bak` first. Replacement
uses an atomic rename on Unix. Windows uses a recoverable replacement with the
backup retained and attempts to restore it if replacement fails. Failed attempts
clean up their own temporary directory. This does not promise durability through
power loss. If the primary file is missing and a `.bak` file exists, startup
restores that backup and displays a notice. The backup is a rolling previous
copy; copy it elsewhere if it should be kept longer.

Files declaring a newer version load defaults and remain protected from saves,
including zoom/menu saves. They can still be used alongside a live session with
temporary settings. Move the future-version file aside, or use a compatible
release, then restart. Other unsupported/invalid version declarations also load
defaults and can be replaced by an explicit Save.

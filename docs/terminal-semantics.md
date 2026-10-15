# Terminal protocol semantics

imterm supports the ANSI/VT commands listed below. Unsupported commands,
including private (`?`) and screen (`=`) modes, are ignored without changing
terminal state.

## Coordinates

- Screen rows and columns are zero-based positions inside the active viewport.
- ANSI row and column parameters are one-based.
- Buffer rows address the complete scrollback buffer.
- Rendered columns count terminal cells, including expanded tab cells.
- Byte offsets address the UTF-8 byte storage inside one line.

Screen positions refer to the active viewport rather than saved scrollback.
Line-number and timestamp margins do not alter the terminal's row and column
bounds.

## Supported CSI commands

| Sequence | Behavior | Default |
| --- | --- | --- |
| `A`, `B`, `C`, `D` | Move up, down, right, or left | 1 cell |
| `E`, `F` | Move down or up and go to column zero | 1 row |
| `G` | Set absolute column | column 1 |
| `H`, `f` | Set absolute row and column | row 1, column 1 |
| `s`, `u` | Save or restore the cursor | none |
| `J` | Erase display after, before, all, or scrollback | mode 0 |
| `K` | Erase line after, before, or all | mode 0 |
| `m` | Apply supported SGR formatting and normal colors | reset |
| `5n`, `6n` | Device-status or cursor-position report | none |

An omitted or zero cursor-movement parameter means one. Missing `H`, `f`, or
`G` coordinates mean one. Erase-before and erase-after operations include the
cursor cell. Display erasure affects the visible viewport; `3J` removes only
saved scrollback. Cursor-position reports use one-based screen coordinates.

Supported formatting attributes are bold, dim, italic, underline, blinking,
inverse, hidden, and strikethrough, with their standard individual resets.
Supported colors are the normal foreground and background colors 30–37 and
40–47 plus default foreground/background reset.

## Text encoding

Terminal text uses UTF-8. A Unicode scalar occupies one logical rendered
column; tabs expand to tab stops. Byte offsets and columns are distinct.
Overwriting, erasing, selecting, and copying text preserve scalar boundaries.

Serial input accepts modern one-to-four-byte UTF-8: shortest forms only, no
surrogates, and no values above U+10FFFF. Each invalid byte is replaced with
U+FFFD (`EF BF BD`). This same normalized text is rendered, copied, searched,
and logged. [Raw capture](raw-capture.md) preserves the original bytes.
Embedded NUL bytes are ignored in the terminal display. A malformed
UTF-8 prefix never consumes a following ASCII character, control, or escape.
A Unicode scalar or replacement interrupts an unfinished malformed CSI.

A still-valid incomplete UTF-8 prefix is held across serial reads (at most
three bytes) and is not displayed until completed. When the serial stream closes
or the application exits normally, each remaining byte becomes a replacement
character and escape parsing is reset. Clearing the terminal discards any
pending prefix and escape sequence.

Unicode keyboard input is sent as UTF-8, including characters outside the BMP.
Invalid code points become U+FFFD. ASCII keys and control sequences are sent as
their corresponding bytes.

Clipboard paste follows a **pass-through UTF-8** policy: clipboard bytes are
queued unchanged, including malformed UTF-8 and CR/LF, with no second encoding
or newline conversion. Clipboard APIs provide NUL-terminated text, so paste
ends at NUL. Copy uses normalized stored UTF-8 and never truncates a scalar.

Wide-character cell widths, combining marks, grapheme-cluster selection,
bidirectional text, shaping, and font fallback are not supported. Their scalar
values are preserved, but each scalar is counted independently as one column.
The selected font may display a missing-glyph box for unsupported characters;
copy/paste still preserves their UTF-8 bytes.

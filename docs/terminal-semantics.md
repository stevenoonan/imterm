# Terminal protocol semantics

The escape-sequence parser is lexical: it recognizes a bounded CSI sequence
and preserves its identifier, mode, and parameters. `DecodeTerminalCommand`
then converts valid, supported sequences to typed commands. `TerminalState`
only applies those typed commands. Unsupported normal, private (`?`), and
screen (`=`) modes are ignored without changing terminal state.

## Coordinates

- Screen rows and columns are zero-based positions inside the active viewport.
- ANSI row and column parameters are one-based and are converted at decode time.
- Buffer rows address the complete scrollback buffer.
- Rendered columns count terminal cells, including expanded tab cells.
- Byte offsets address the UTF-8 byte storage inside one line.

The conversion from a screen position to a scrollback-buffer position is owned
by `TerminalState`. The renderer supplies viewport row and column counts through
`SetViewportSize`; line-number and timestamp margins do not alter that size.

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

## Text encoding (Milestone 4)

Terminal lines store UTF-8 **bytes**, with color metadata on each byte. A
Unicode scalar occupies one logical rendered column; tabs expand to tab stops.
All conversions between byte offsets and columns decode complete scalars.
Overwriting, erasing, selecting, and copying text preserve scalar boundaries.

Serial input accepts modern one-to-four-byte UTF-8: shortest forms only, no
surrogates, and no values above U+10FFFF. Each invalid byte is replaced with
U+FFFD (`EF BF BD`). This same normalized text is rendered, copied, searched,
and logged; original malformed bytes are not retained. ASCII controls keep
their existing terminal behavior, including ignoring embedded NUL. A malformed
UTF-8 prefix never consumes a following ASCII character, control, or escape.
A Unicode scalar or replacement interrupts an unfinished malformed CSI.

A still-valid incomplete UTF-8 prefix is held across serial reads (at most
three bytes) and is not displayed until completed. `FinishInput`, called when
the serial stream closes and on normal application shutdown, emits a replacement
for each remaining byte and resets escape parsing. Clearing the terminal
discards any pending prefix and escape sequence. Nonstreaming buffer setters
and insertion APIs replace malformed and truncated bytes immediately.

ImGui uses `IMGUI_USE_WCHAR32`, consistently in the library, backends, and
consumers. Each Unicode code point in `InputQueueCharacters` is encoded to UTF-8
exactly once before entering the `uint8_t` serial queue. Invalid code points
passed to the encoder become U+FFFD. ASCII keyboard/control sequences keep their
existing byte values.

Clipboard paste follows a **pass-through UTF-8** policy: clipboard bytes are
queued unchanged, including malformed UTF-8 and CR/LF, with no second encoding
or newline conversion. Clipboard APIs provide NUL-terminated text, so paste
ends at NUL. Copy uses normalized stored UTF-8 and never truncates a scalar.

Wide-character cell widths, combining marks, grapheme-cluster selection,
bidirectional text, shaping, and font fallback remain deferred. Their scalar
values are preserved, but each scalar is counted independently as one column.
The selected font may display a missing-glyph box for unsupported characters;
copy/paste still preserves their UTF-8 bytes.

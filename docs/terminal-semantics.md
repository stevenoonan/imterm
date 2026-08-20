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
40–47 plus default foreground/background reset. Wide characters, combining
marks, grapheme clusters, and full UTF-8 validation remain deferred.

# Terminal view components

`TerminalView` coordinates the view components and the platform clipboard. It
retains the session's shared data/state references as an adapter for Clear,
viewport-size reporting, and querying the device cursor. Parsing, input-stream
lifetime, logging, and serial transport belong to the core/session. The
renderer, selection model, keyboard policy, theme, and viewport calculations
neither parse device input nor own transport behavior.

| Component | Responsibility | Dependencies |
| --- | --- | --- |
| `TerminalSelection` | UI caret, anchored/reversed selection, word/line dragging, Unicode/tab navigation, copy text and per-line selection spans | Read-only `TerminalData`; no ImGui |
| `terminal_viewport` | Visible line bounds and scroll offsets needed to reveal a point | Plain pixel/count values; no ImGui |
| `terminal_input` | Device sequences and modifier/shortcut precedence | Standard C++; no ImGui |
| `terminal_palette` | Dark, light, and retro-blue terminal color tables | Terminal color roles; no ImGui |
| `terminal_keyboard` | Translate ImGui keys/text into input-policy actions | ImGui and `terminal_input` |
| `TerminalRenderer` | Layout, hit testing, and separate margin/selection/cursor/text drawing passes | ImGui; read-only data/selection |
| `TerminalView` | Dispatch actions, clipboard access, pending scroll requests, viewport/cursor bridge, outbound bytes | Components above and session data/state references |

The pure components build into `imterm_core`. `imterm_view` builds the ImGui
adapter and renderer once for the application and headless UI tests. Neither
library depends on GLFW, Vulkan, or serial I/O.

## Layout and drawing

Each frame first obtains a complete `TerminalRenderGeometry` from layout, then
handles interaction, paints, and applies pending reveal requests. Hit testing and
page navigation use the current font, margins, and scroll position.
Zoom anchoring supplies the buffer height and desired scroll before
`BeginChild`, keeping the top visible line stable across zoom changes.

The geometry reports the content origin, first/last visible rows, total rows,
cell metrics, text margin, viewport dimensions, and first visible text position.
Drawing receives this snapshot by const reference. Render statistics report
margin rows, selection rectangles, cursor rectangles, and text runs, enabling
semantic checks without depending on exact screenshot pixels.

Margin drawing handles line numbers/timestamps only when enabled. Selection
drawing uses normalized per-line spans, including selected newlines. Cursor
drawing uses the device cursor, focus state, and monotonic blink time. Text
drawing batches UTF-8/color runs, expands stored tabs, and applies style alpha.
The theme's initial background is defined before the first frame.

## Selection and input

The UI caret is independent of the device cursor. Rendering cannot move a
clicked caret or selection back to the device's current row. Copy uses the
selection, or the UI caret's complete line when there is no selection. Paste
queues clipboard bytes unchanged. Right-click copies and clears an existing
selection, or pastes when no selection exists.

Selection uses rendered columns and complete Unicode scalars. A tab is one
stored character spanning multiple cells, with endpoints snapped to stored
character boundaries. Word selection groups ASCII whitespace/non-whitespace text;
ANSI color changes do not divide a word. Double-click/Ctrl-click selects a word,
triple-click selects a line, and dragging extends in either direction while
retaining the anchor word/line. Buffer shrinkage clamps stored positions safely.
Vertical movement preserves the preferred column through shorter lines.

| Key/action | Result |
| --- | --- |
| Delete, Backspace, Enter, Tab | `ESC[3~`, `7F`, LF (`0A`), and TAB (`09`), respectively |
| Arrow keys | `ESC[A`, `ESC[B`, `ESC[C`, `ESC[D` for up, down, right, left |
| Home, End | `ESC[1~`, `ESC[4~` |
| Page Up/Down | Local UI caret/page navigation; Shift extends selection |
| Ctrl/Cmd+Home/End | Local top/bottom navigation; Shift extends selection |
| Ctrl/Cmd+Insert | Copy |
| Shift+Insert or Ctrl/Cmd+V | Paste clipboard bytes |
| Ctrl/Cmd+A | Select all |
| Ctrl/Cmd+F, zoom shortcuts | Reserved for the capture controller |
| Ctrl/Cmd+C | Device interrupt byte `03` |
| Ctrl/Cmd+Z | Device control byte `1A` |

The ImGui adapter normalizes the platform shortcut modifier before resolving a
key. A physical key produces one action. Alt/alternate combinations do not emit
alphabetic control bytes; ordinary text events still support AltGr input.
Each Unicode scalar occupies one logical cell. See
[terminal semantics](terminal-semantics.md) for the supported protocol commands
and font/layout limitations.

## View API

The view exposes selection/navigation, clipboard actions, theme selection,
optional whitespace visualization, and embedding without an ImGui child.
Mouse and keyboard handling can be enabled independently.

An outbound byte queue carries keyboard and paste input to the session.
Theme tables contain terminal/UI color roles and ANSI colors; palette indices
are internal and are not a persisted format.

## Smoke checklist

Use a real serial device or isolated PTY, and compare 100%, 125%, 150%, and 200%
scale with the same text:

1. Receive multiple lines with ANSI foreground changes, UTF-8, and scrollback.
2. Toggle line numbers/timestamps and inspect margin alignment and text colors.
3. Drag and word/line select in both directions; copy/paste Unicode and use
   right-click copy/paste. Confirm the selected bytes reach the device unchanged.
4. Check cursor focus/blink, Page Up/Down, top/bottom navigation, and zoom anchors.
5. Confirm Ctrl+V/A do not also transmit control bytes, while Ctrl+C/Z do.
6. Clear, resize, reconnect, and close normally.

Headless tests exercise layout/draw state and actual ImGui input events.
Linux PTY tests verify transport byte round trips. Use the graphical application
for visual checks of font quality, margins, colors, and DPI scaling.

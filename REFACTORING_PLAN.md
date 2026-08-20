# imterm Refactoring Plan

This document turns the current code review into an incremental refactoring
roadmap. The work is intentionally divided into milestones that can be merged,
released, and used independently. No milestone should leave the application in
a partially migrated state.

## Goals

- Preserve imterm's current usefulness throughout the refactor.
- Make serial/device input safe even when it is malformed or incomplete.
- Separate terminal behavior from ImGui, serial-port, and Vulkan concerns.
- Make resource ownership and application state explicit.
- Establish automated tests before changing terminal semantics.
- Keep Windows and Linux support working at milestone boundaries.

This plan is not a feature roadmap. Search, hexadecimal display, new transports,
and other product features should normally wait until the foundations below are
stable.

## Stability rule for every milestone

A milestone is complete only when all of the following are true:

- The Debug and Release configurations build from a clean build directory.
- Existing user-visible features that are not explicitly changed by the
  milestone still work.
- New behavior is covered by automated tests where it can be tested without
  hardware.
- A manual smoke test has been completed with a real or loopback serial port.
- No temporary compatibility code, disabled tests, or known crash regressions
  remain undocumented.
- The milestone can be reverted as a unit without requiring later milestones.

If a milestone grows too large to satisfy this rule, split it into smaller
mergeable slices. Prefer adapters and parallel implementations over a long-lived
half-conversion.

## Progress tracker

- [X] Milestone 0: Establish the safety net
- [X] Milestone 1: Harden the existing input pipeline
- [X] Milestone 2: Encapsulate the terminal buffer and logging invariants
- [ ] Milestone 3: Correct and isolate terminal protocol semantics
- [ ] Milestone 4: Define a consistent text-encoding boundary
- [ ] Milestone 5: Introduce an owned capture session
- [ ] Milestone 6: Make configuration typed, validated, and reliable
- [ ] Milestone 7: Split the terminal view into focused components
- [ ] Milestone 8: Encapsulate application/platform startup and finish cleanup

## Milestone 0: Establish the safety net

**Outcome:** The program behaves as it does today, but terminal logic can be
tested independently and regressions can be detected before deeper changes.

**Expected size:** Small to medium.

### Scope

- Create a first-party test target and integrate it with CTest.
- Extract the existing terminal data, parser, state, and logger sources into a
  library target that both the application and tests can link.
- Add characterization tests for current behavior without trying to correct it
  yet:
  - ordinary text, CR, LF, CRLF, and each newline mode;
  - escape sequences split across multiple input buffers;
  - basic colors, cursor movement, erase commands, and status reports;
  - scrolling when the terminal viewport is full;
  - logging complete and incomplete lines;
  - keyboard control-sequence mappings that are currently relied upon.
- Add small binary fixtures or table-driven cases instead of tests coupled to
  ImGui.
- Enable a useful warning baseline for first-party targets. Record existing
  warnings that cannot yet be enabled as errors.

### Deliberately out of scope

- Correcting ANSI behavior discovered by characterization tests.
- Moving serial I/O off the UI thread.
- Large class or ownership changes.

### Completion gate

- `ctest` runs without a display, Vulkan, or serial hardware.
- The application still builds and launches normally.
- Tests demonstrate chunked input, scrollback, logging, and terminal responses.
- The current Debug build and a Release build pass.

## Milestone 1: Harden the existing input pipeline

**Outcome:** Arbitrary serial byte streams cannot read outside their buffers or
terminate the program through expected parser errors. User-visible behavior is
otherwise preserved.

**Expected size:** Medium.

### Scope

- Replace pointer-plus-implicit-length parsing with bounded byte ranges.
- Make UTF-8 processing stop safely on truncated sequences.
- Limit escape-sequence length, numeric argument length, argument count, and
  numeric range.
- Replace parser/state exceptions for unsupported or malformed terminal input
  with explicit parse results or ignored-command results.
- Clamp cursor rows and columns at both lower and upper bounds before indexing.
- Validate erase ranges before constructing iterators.
- Make unsupported escape sequences recover without swallowing unrelated text
  indefinitely.
- Catch unexpected processing failures at the capture boundary, report them,
  and preserve the application session where safe.
- Add regression tests for:
  - every truncated UTF-8 prefix;
  - oversized and overflowing CSI arguments;
  - unknown SGR and CSI commands;
  - large cursor-left/up/down/right values;
  - malformed sequences split at every byte boundary;
  - embedded NUL bytes.
- Add a fuzz target for the parser/state input boundary if the available toolchain
  supports it. It may remain an opt-in developer target.

### Deliberately out of scope

- Full Unicode rendering correctness.
- Broad ANSI conformance changes.
- Redesigning the terminal buffer.

### Completion gate

- The new malformed-input tests pass under AddressSanitizer and
  UndefinedBehaviorSanitizer on at least one supported platform.
- A long fuzz or randomized-input run produces no crash or unbounded allocation.
- Valid ESP32 console output still renders and accepts keyboard input.
- Logging and automatic scrolling still work.

## Milestone 2: Encapsulate the terminal buffer and logging invariants

**Outcome:** Terminal contents have one mutation API, logging cannot retain
invalid vector-element pointers, and line timestamps consistently reflect the
chosen semantics.

**Expected size:** Medium to large.

### Scope

- Make `TerminalData::mLines` private.
- Define a narrow terminal-buffer API for:
  - accessing immutable lines;
  - inserting/appending/removing lines;
  - inserting/replacing bytes or glyphs;
  - erasing validated ranges;
  - querying row, column, and byte-offset information.
- Replace `mUnloggedLine` with a stable index or explicit pending-log record.
- Ensure every mutation preserves the nonempty-buffer invariant.
- Replace public inheritance in `vector_timed` with composition or fold timestamp
  metadata into a dedicated `TerminalLine` value.
- Decide and document when a line timestamp changes: first received byte, most
  recent mutation, or line completion. Implement that rule consistently.
- Replace logger callback identity comparisons with registration tokens or an
  explicit flush protocol.
- Make logger reentrancy and exception handling scope-bound and per instance.
- Add tests that force vector reallocations, clear/reset the buffer, close the
  logger with an incomplete line, and use more than one logger.
- Keep a temporary read-only adapter for `TerminalView` so this milestone does
  not require its redesign.

### Deliberately out of scope

- Replacing the renderer.
- Changing ANSI cursor semantics except where required to prevent invalid access.
- Changing the log-file format unless necessary to fix corruption.

### Completion gate

- No non-buffer class directly mutates the line container.
- Sanitizers pass tests that repeatedly grow, clear, and rebuild the buffer.
- Logging survives reconnection, buffer growth, empty sessions, and shutdown.
- Line numbers and timestamps still render and log correctly in the application.

## Milestone 3: Correct and isolate terminal protocol semantics

**Outcome:** ANSI/VT command behavior lives in a testable terminal core with
explicit coordinate conversions. Common terminal sequences behave predictably.

**Expected size:** Large.

### Scope

- Introduce distinct concepts for:
  - terminal screen row/column;
  - scrollback buffer row;
  - rendered column;
  - UTF-8 byte offset.
- Remove mutable reference accessors for cursor row and column.
- Parse escape sequences into typed commands before applying them.
- Move command execution into small command handlers or a cohesive terminal-core
  state machine.
- Correct and test at least the currently advertised command set:
  - one-based row/column conversion for `H` and `f`;
  - default argument values for cursor movement and erase commands;
  - save/restore cursor;
  - display and line erasure;
  - status and cursor-position reports;
  - supported SGR colors and formatting resets.
- Define behavior for unsupported private/screen modes: ignore safely and record
  diagnostics only when useful.
- Move viewport-size calculation behind one explicit API. Ensure text margins do
  not incorrectly change terminal column bounds.
- Add conformance-style table tests for complete sequences and sequences split
  across every possible input boundary.
- Update characterization tests where this milestone intentionally fixes prior
  behavior, documenting each changed behavior in the commit or release notes.

### Deliberately out of scope

- Supporting every VT/xterm command.
- Full grapheme-cluster or wide-character layout.
- Capture-session ownership changes.

### Completion gate

- All supported commands have table-driven tests.
- Cursor and erase tests include tabs, multibyte input, short lines, scrollback,
  and resize events.
- ESP32 line editing, command history, coloring, and cursor reports work in a
  hardware smoke test.
- Unknown commands remain nonfatal.

## Milestone 4: Define a consistent text-encoding boundary

**Outcome:** Keyboard, paste, serial bytes, terminal storage, and rendering agree
on how UTF-8 is represented. ASCII behavior remains unchanged.

**Expected size:** Medium to large.

### Scope

- Make serial queues byte-oriented rather than `ImWchar`-oriented.
- Encode ImGui Unicode code points to UTF-8 exactly once at the input boundary.
- Preserve paste text as validated or pass-through UTF-8 according to a documented
  policy.
- Replace the current length-only UTF-8 helper with a validating decoder limited
  to modern one-to-four-byte UTF-8.
- Decide how invalid incoming bytes are displayed and copied. A replacement glyph
  or an escaped/raw representation are both acceptable if consistently tested.
- Decide whether terminal storage remains UTF-8 bytes or moves to decoded code
  points. Keep byte offsets and rendered columns distinct either way.
- Test ASCII, common accented characters, non-Latin BMP characters, characters
  outside the BMP, invalid input, copy, paste, selection, and split serial reads.
- Document that wide characters, combining marks, and grapheme clusters are either
  supported here or explicitly deferred.

### Deliberately out of scope

- Font fallback and complete international text shaping unless required for
  correctness on supported fonts.
- Changing transport/session architecture.

### Completion gate

- Non-ASCII keyboard input and paste produce the intended UTF-8 serial bytes.
- Incoming UTF-8 renders and copies without out-of-bounds access or truncation.
- Invalid byte streams follow the documented policy and remain nonfatal.
- ASCII terminal behavior and performance do not regress noticeably.

## Milestone 5: Introduce an owned capture session

**Outcome:** One object owns the serial connection and terminal components. Port
changes, reconnects, failures, and shutdown do not leak resources or depend on
file-level global state.

**Expected size:** Large.

### Scope

- Add a `CaptureSession` or equivalent owner for:
  - the serial transport;
  - connection/reconnection state;
  - terminal core/data;
  - logger;
  - view-facing session state.
- Replace the raw `Serial*` with RAII ownership.
- Define explicit connection transitions and allowable actions for disconnected,
  connecting, connected, reconnecting, reconfiguring, and failed states.
- Introduce a serial transport interface and a fake/loopback implementation for
  tests.
- Move serial polling and terminal response writes out of ImGui rendering code.
- Ensure menus query capability/state rather than dereferencing optional terminal
  objects.
- Decide whether transport I/O remains nonblocking on the UI thread or moves to a
  worker. If a worker is used, define queue ownership, shutdown, and error delivery
  before implementation.
- Add tests for open failure, repeated reconfiguration, disconnect, reconnect,
  cancellation, write failure, shutdown, and preservation/reset of terminal data.

### Deliberately out of scope

- Redesigning the setup dialog.
- Supporting multiple simultaneous sessions unless that falls out naturally from
  the ownership model.
- Replacing the Vulkan application shell.

### Completion gate

- Repeated connect/change-port/disconnect cycles show no leaks under available
  leak-detection tooling.
- The application remains responsive during expected serial timeouts and reconnect
  attempts.
- Auto reconnect, flow-control lines, terminal responses, logging, and shutdown
  work with real hardware.
- UI actions are disabled or safe in every connection state.

## Milestone 6: Make configuration typed, validated, and reliable

**Outcome:** Bad or partial configuration cannot prevent startup, and every saved
setting has a clear effect on the running or next session.

**Expected size:** Medium.

### Scope

- Replace global static configuration loading with explicit startup-time loading.
- Define typed settings with defaults and validation for port, baud rate, data
  bits, stop bits, parity, flow control, newline mode, logging, and log path.
- Recover from missing, partial, malformed, and future-version configuration with
  actionable messages and safe defaults.
- Use atomic/recoverable configuration writes where practical.
- Make combo-box items own their values instead of storing references to external
  vectors.
- Apply the selected flow-control mode when opening a port.
- Apply and persist the selected logging path.
- Bound all text copied into fixed-size UI buffers, or replace those buffers with
  safer string-backed input helpers.
- Test default creation, malformed TOML, missing keys, invalid enum strings,
  extreme baud strings, round trips, and write failures.

### Deliberately out of scope

- A complete visual redesign of the setup dialog.
- Cloud synchronization or multiple named profiles unless separately approved.

### Completion gate

- The program always reaches a usable setup UI when the configuration is absent
  or malformed.
- Every displayed setting is either applied or clearly marked as informational.
- Saved settings survive restart and round-trip through TOML tests.
- Port setup, logging path, and newline mode work in a hardware smoke test.

## Milestone 7: Split the terminal view into focused components

**Outcome:** Rendering, selection/scrolling, and keyboard encoding can evolve
independently. The visible terminal remains functionally equivalent.

**Expected size:** Large.

### Scope

- Separate at least these responsibilities:
  - terminal renderer and geometry calculation;
  - selection, clipboard, and viewport navigation;
  - keyboard/control-sequence mapping;
  - palette/theme data.
- Remove or quarantine copied text-editor features that are not part of imterm,
  including unused editing APIs, identifiers, breakpoints, error markers, queues,
  and incomplete declarations.
- Correct misleading accessors and unreachable/duplicated keyboard branches.
- Replace the single long render method with small rendering passes for margins,
  selection, cursor, and text.
- Make render geometry an output of layout rather than partially initialized
  mutable state.
- Add unit tests for keyboard mapping and selection calculations. Add lightweight
  render-state tests where possible without screenshot brittleness.
- Compare terminal rendering and interaction manually at common DPI scales.

### Deliberately out of scope

- A new visual design.
- Replacing Dear ImGui.
- New terminal features solely because the split makes them easier.

### Completion gate

- No single view component owns protocol parsing or serial transport behavior.
- Keyboard mappings and selection rules are covered independently of ImGui where
  practical.
- Line numbers, timestamps, color, cursor, selection, scrolling, clipboard, and
  DPI behavior pass the smoke checklist.
- Unused editor APIs and dead members are removed or explicitly justified.

## Milestone 8: Encapsulate application/platform startup and finish cleanup

**Outcome:** The application shell has explicit lifetime management, build rules
are target-scoped, and the refactored architecture is documented for future work.

**Expected size:** Medium to large.

### Scope

- Wrap GLFW, Vulkan instance/device/window resources, and ImGui initialization in
  RAII owners with deterministic partial-failure cleanup.
- Check window creation and other currently assumed initialization results.
- Remove remaining copied example code and obsolete comments.
- Replace global compiler flags and include directories with target-scoped CMake
  settings.
- Align the declared minimum CMake version with the features actually used.
- Make warnings-as-errors available for first-party code on supported CI
  toolchains while keeping third-party warnings isolated.
- Add CI or documented automation for supported Debug/Release platform builds and
  tests.
- Document the resulting module boundaries and dependency direction.
- Review public names, const-correctness, signed/unsigned indexing, and remaining
  assertions at trust boundaries.

### Deliberately out of scope

- Changing the renderer backend.
- Removing supported dependency-management paths without a separate decision.
- Major new end-user features.

### Completion gate

- Failure at each startup stage exits cleanly without leaked initialized resources.
- First-party code builds cleanly under the agreed warning policy.
- Debug and Release builds, automated tests, and the full smoke checklist pass on
  supported platforms.
- The README and architecture documentation match the actual build and ownership
  model.

## Manual smoke-test checklist

Run this checklist at every milestone. Add milestone-specific checks rather than
replacing these baseline checks.

- Launch with the normal configuration and reach the setup/terminal UI.
- Select a serial port and connect with representative UART parameters.
- Receive plain ASCII text, CR/LF variants, tabs, and colored ANSI output.
- Use ESP32-style line editing, history, arrows, Home/End, Tab, Backspace, Enter,
  and Ctrl key combinations.
- Copy a selection and paste text to the device.
- Resize the window and verify cursor position, wrapping assumptions, scrolling,
  line numbers, and timestamps.
- Toggle auto-scroll and manually inspect scrollback.
- Toggle DTR and RTS and inspect CTS, DSR, and DCD when hardware supports them.
- Enable logging, receive complete and incomplete lines, then close/reconfigure the
  session and inspect the log.
- Disconnect the device, verify automatic reconnect, and cancel reconnect once.
- Change ports or parameters repeatedly, then exit normally.

## Cross-cutting engineering rules

- Keep third-party code under `deps/` outside this refactor unless an integration
  change is strictly necessary.
- Add a regression test before fixing a discovered crash or protocol bug whenever
  feasible.
- Do not mix broad renaming/formatting with behavioral changes.
- Prefer value types and unique ownership. Use shared ownership only when the
  lifetime is genuinely shared and documented.
- Treat bytes from the serial port, configuration files, and clipboard as external
  input requiring validation at a clear boundary.
- Assertions may protect internal invariants, but they must not be the only defense
  against malformed external input.
- Record intentional behavior changes in the milestone notes so characterization
  tests do not silently redefine expected behavior.

## Issue-to-milestone map

| Review finding | Primary milestone |
| --- | --- |
| Out-of-bounds and throwing serial input path | 1 |
| Public terminal lines and invalidated logging pointer | 2 |
| Unreliable line timestamp and logger callbacks | 2 |
| Mixed coordinates and incorrect ANSI defaults/conversions | 3 |
| Unicode and byte/code-point truncation | 4 |
| Raw serial ownership and capture globals | 5 |
| Blocking/coupled serial work in UI code | 5 |
| Fragile settings, combo references, and ignored settings | 6 |
| Oversized `TerminalView` and copied editor residue | 7 |
| Global Vulkan/GLFW state and build-system cleanup | 8 |
| Missing first-party tests | 0 and every later milestone |

## Decisions to record during implementation

Several choices should be made explicitly rather than emerging accidentally:

- Whether a line timestamp means first input, last mutation, or line completion.
- Whether invalid incoming UTF-8 is replaced, escaped, or shown as raw bytes.
- Whether the terminal buffer stores UTF-8 bytes or decoded code points.
- Which ANSI/VT dialect and command subset imterm promises to support.
- Whether terminal contents survive manual reconfiguration and automatic reconnect.
- Whether serial I/O uses a worker thread or a guaranteed-nonblocking UI-thread
  pump.
- Which Windows and Linux compiler/configuration combinations form the supported
  release matrix.

Record each decision in this document or in a linked architecture decision note
when the corresponding milestone begins.

# imterm Refactoring Plan

This roadmap defines the architecture and completion requirements for imterm's
refactoring work. Milestones 0–7 are complete; Milestone 8 covers the application
shell and build cleanup. The linked guides describe the current implementation.

## Goals

- Keep the terminal usable at every milestone boundary.
- Handle malformed and incomplete serial input safely.
- Separate terminal behavior, transport, UI, and platform startup.
- Give resources explicit owners and deterministic cleanup.
- Cover behavior with automated tests and real-device or loopback smoke checks.
- Support Windows and Linux.

Product features have their own scope; this roadmap concerns architecture,
correctness, and maintainability.

## Completion requirements

A milestone is complete when:

- Debug and Release build from clean directories.
- User-visible behavior matches its documented contract.
- Automated tests cover behavior that does not require hardware.
- A smoke check covers a real or loopback serial session.
- Compatibility code, platform limitations, and known issues are documented.
- The change is independently reviewable and revertible.

Split work into smaller mergeable pieces if needed. Keep each piece usable.

## Progress tracker

- [X] Milestone 0: Establish the safety net
- [X] Milestone 1: Harden the existing input pipeline
- [X] Milestone 2: Encapsulate the terminal buffer and logging invariants
- [X] Milestone 3: Correct and isolate terminal protocol semantics
- [X] Milestone 4: Define a consistent text-encoding boundary
- [X] Milestone 5: Introduce an owned capture session
- [X] Milestone 6: Make configuration typed, validated, and reliable
- [X] Milestone 7: Split the terminal view into focused components
- [ ] Milestone 8: Encapsulate application/platform startup and finish cleanup

## Milestone 0: Establish the safety net

**Status:** Complete. [Testing guide](tests/README.md).

`imterm_core` provides terminal behavior independently of ImGui and serial I/O.
GoogleTest suites run through CTest without a display or serial hardware.
Coverage includes text, CR/LF modes, chunked escape sequences, colors, cursor
movement, erasure, scrolling, logging, and keyboard byte mappings.

First-party core/session/view libraries and test targets enable warnings.
Debug, Release, and opt-in sanitizer configurations support routine validation.

## Milestone 1: Harden the existing input pipeline

**Status:** Complete. [Protocol semantics](docs/terminal-semantics.md).

The input boundary uses bounded byte ranges, limits escape-sequence and numeric
parsing, validates UTF-8, clamps cursor positions, and validates erase ranges.
Unknown or malformed commands recover without throwing expected parser errors.
The session reports unexpected terminal-processing failures.

Tests cover truncated UTF-8, overflowing CSI arguments, unknown commands, large
cursor movements, split sequences, embedded NUL, and deterministic random input.
The opt-in libFuzzer target instruments the parser/state boundary.

## Milestone 2: Encapsulate the terminal buffer and logging invariants

**Status:** Complete.

`TerminalData` owns line mutations and maintains a nonempty buffer. External
readers access immutable lines. `TerminalLine` contains byte glyphs and a
last-content-mutation timestamp. Pending logging uses a stable line index;
watcher tokens identify logger-close callbacks. Logger guards are per instance
and cleanup contains callback exceptions.

Tests cover buffer growth and removal, timestamp updates, incomplete-line
flushes, callback registration, reentrancy, and multiple loggers.

## Milestone 3: Correct and isolate terminal protocol semantics

**Status:** Complete. [Supported commands](docs/terminal-semantics.md#supported-csi-commands).

The core distinguishes screen positions, buffer positions, rendered columns,
and UTF-8 byte offsets. Typed commands express cursor movement, erasure,
graphics changes, and status reports. Viewport dimensions have an explicit API;
line-number/timestamp margins do not change terminal bounds.

Command tests cover defaults, one-based ANSI coordinates, save/restore,
viewport/scrollback erasure, reports, SGR state, split input, and unknown modes.
The reference guide describes rendering limits and the received-tab limitation.

## Milestone 4: Define a consistent text-encoding boundary

**Status:** Complete. [UTF-8 policy](docs/terminal-semantics.md#text-encoding).

Serial queues and terminal storage contain bytes. Keyboard Unicode scalars are
encoded to UTF-8 once; clipboard paste passes bytes through unchanged. Invalid
received bytes normalize to U+FFFD. Incomplete valid prefixes span reads and
finish with replacements at stream end. Clear discards incomplete input.

Selection, navigation, overwrite, and erasure respect scalar boundaries. A
scalar occupies one logical cell; wide-character widths, combining layout,
grapheme selection, shaping, and font fallback are unsupported. ImGui and its
consumers use `IMGUI_USE_WCHAR32` consistently in both dependency build paths.

## Milestone 5: Introduce an owned capture session

**Status:** Complete. [Ownership and I/O](docs/capture-session.md).

`Capture` owns the view/UI state, settings, and a `CaptureSession`. The session
owns terminal data/state, loggers, raw capture, connection state, and a worker.
Only the worker accesses the transport. Bounded RX/TX queues, generation IDs,
20 ms native I/O timeouts, and worker joining define the thread boundary.

Connection states and capabilities govern UI actions. Reconnect and same-port
changes preserve terminal contents; a successful different-port connection
resets protocol state and contents. Stream boundaries flush incomplete text and
logs and discard unsent output. Raw capture continues until stopped or shutdown.

Tests cover failures, cancellation, retries, short/zero writes, queue limits,
modem requests, repeated reconfiguration, data lifetime, and shutdown. Linux PTY
tests cover native transfers, hangup/reconnect, and descriptor cleanup.

## Milestone 6: Make configuration typed, validated, and reliable

**Status:** Complete. [Configuration reference](docs/configuration.md).

`SettingsValues` contains typed serial, newline, logging/path, and zoom settings.
`Capture` explicitly loads configuration at startup. Missing, partial, malformed,
oversized, and unsupported-version files have defined defaults and diagnostics.
Future-version files are protected from replacement.

Save validates bounded text and numeric fields, applies preferences, and writes
through a private temporary file with a rolling backup. Recovery restores a
missing primary from its backup. Setup owns its draft and combo values; menu
changes to newline, logging, and zoom persist immediately.

Tests cover field types/ranges, round trips, restart persistence, backup
recovery, failed saves, Setup Save, and native PTY flow control/newline/log paths.

## Milestone 7: Split the terminal view into focused components

**Status:** Complete. [View components](docs/terminal-view.md).

`TerminalSelection`, viewport calculations, keyboard policy, and palette tables
build into `imterm_core`. `imterm_view` provides the ImGui keyboard adapter,
renderer, and coordinating `TerminalView`. Layout produces a complete geometry
snapshot before interaction. Separate passes draw margins, selection, cursor,
and text. The UI caret and device cursor have independent roles.

A physical key resolves to one action. Local shortcuts, clipboard operations,
AltGr, device control bytes, and independent mouse/keyboard flags have explicit
policies. The outbound byte queue and documented view primitives form the API.

Tests cover selection/navigation, UTF-8/tab boundaries, keyboard precedence,
clipboard bytes, input events, layout, colors, focus/blink, and zoom. Visual
smoke checks use 100%, 125%, 150%, and 200% scaling.

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
- Document intentional behavior changes in the relevant reference guide and
  tests so expected behavior stays explicit.

## Architecture decisions

| Topic | Current policy |
| --- | --- |
| Line timestamps | Most recent content mutation |
| Invalid incoming UTF-8 | One U+FFFD per invalid byte; raw capture preserves originals |
| Terminal storage | UTF-8 bytes with scalar-aware coordinate conversions |
| Terminal dialect | [Documented ANSI/VT subset](docs/terminal-semantics.md) |
| Clipboard paste | Pass-through bytes, without newline conversion |
| Terminal data lifetime | Preserve for same-port reconnect/reconfiguration; reset on successful port change |
| Serial I/O | Dedicated worker with bounded queues |
| Configuration | Versioned TOML, validated settings, temporary-file replacement, rolling backup |
| View responsibilities | [Selection/input/layout/drawing components](docs/terminal-view.md) |

## Open validation and follow-up work

- Define the Windows/Linux compiler and configuration matrix used for release CI
  as part of Milestone 8. Linux automated and PTY tests do not substitute for
  Windows execution or physical UART/modem-line checks.
- Correct received-tab cursor advancement as a separately scoped protocol change,
  with tests for text following a tab. See the
  [tab limitation](docs/terminal-semantics.md#tab-limitation).

# Testing imterm

GoogleTest provides assertions and test discovery; CTest runs the suites.
Core/session tests exercise terminal behavior, configuration, logging, and
transport ownership. UI tests use a headless ImGui context for input, selection,
rendering, zoom, and dialogs. Neither suite needs a display, Vulkan device, or
physical serial hardware. Linux PTY tests open `/dev/ptmx` to check native serial
I/O, hangup/reconnect, and byte round trips.

## Configure and run

Complete the [build setup](../docs/building.md) first. From the repository root,
activate your Conan environment if needed and generate dependencies/presets:

```sh
. .venv/bin/activate
./scripts/bootstrap.sh
```

ImGui must be built with `use_wchar32=True`, as configured in `conanfile.py`.
The submodule build applies `IMGUI_USE_WCHAR32` to ImGui and
its consumers too. Defining it only on the application causes an ABI mismatch.

Skip activation if Conan is installed another way. Re-run bootstrap after
changing `conanfile.py` or removing generated dependencies.

The test wrapper configures, builds, and runs Debug tests by default:

```sh
./scripts/test.sh
```

Select Release, both configurations, or a group using a CTest regular
expression:

```sh
./scripts/test.sh release
./scripts/test.sh all
./scripts/test.sh debug TerminalStateTest
./scripts/test.sh all 'Parser|TerminalInput'
```

Run `./scripts/test.sh --help` for the complete usage summary.

The equivalent commands without the wrapper are shown below.

Configure, build, and test Debug:

```sh
cmake --preset conan-debug -DBUILD_TESTING=ON
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure
```

Repeat for Release:

```sh
cmake --preset conan-release -DBUILD_TESTING=ON
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
```

Tests are enabled through CMake's standard `BUILD_TESTING` option. A build that
does not need tests may configure with `-DBUILD_TESTING=OFF`.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer can be enabled for the
first-party targets with GCC or Clang. This Linux/Bash example uses the Debug
dependencies and compiler from the build setup in a separate build directory:

```sh
cmake -S . -B out/sanitizers \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/out/conan/build/Debug/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DIMTERM_ENABLE_SANITIZERS=ON
cmake --build out/sanitizers --parallel
ctest --test-dir out/sanitizers --output-on-failure
```

On Linux, LeakSanitizer requires running outside a debugger or other ptrace-based
wrapper. When changing compilers, generate matching Conan dependencies and use
a fresh CMake build directory.

## Fuzzing

The opt-in libFuzzer target requires Clang with sanitizer/libFuzzer support.
For Linux/Bash, install Clang and Ninja and create a dedicated Conan profile:

```sh
CC=clang CXX=clang++ conan profile detect --name=imterm-clang
conan profile show -pr:h=imterm-clang
```

Review the detected compiler/version, architecture, and standard library.
Reuse the profile if it already exists. Install its dependencies separately;
the empty `user_presets` setting keeps these from changing your normal presets:

```sh
CC=clang CXX=clang++ conan install . --output-folder=out/fuzz-deps --build=missing \
  -pr:h=imterm-clang -s build_type=Debug \
  -c tools.cmake.cmaketoolchain:generator=Ninja \
  -c 'tools.cmake.cmaketoolchain:user_presets='
cmake -S . -B out/fuzz -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/out/fuzz-deps/build/Debug/generators/conan_toolchain.cmake" \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DIMTERM_BUILD_FUZZER=ON
cmake --build out/fuzz --target imterm_parser_fuzz
./out/fuzz/imterm_parser_fuzz -max_total_time=60
```

The target instruments its own copy of the parser/state core with coverage,
AddressSanitizer, and UndefinedBehaviorSanitizer. Pass a corpus directory to
libFuzzer to retain inputs between runs.

## Test categories

- Parser tests cover byte-by-byte escape-sequence parsing.
- Terminal-state tests cover text input, newline modes, chunked sequences,
  colors, cursor movement, erasure, scrollback, and terminal responses.
- Terminal-data tests cover buffer text, coordinates, tabs, insertion, and
  deletion.
- Terminal-input tests lock down the keyboard sequences sent to the device.
- Selection/view-model tests cover Unicode and tab boundaries, reverse and
  word/line dragging, selection anchors, multiline spans, preferred columns,
  shortened buffers, viewport visibility/reveal calculations, and theme colors
  without ImGui. Keyboard policy tests cover shortcut precedence and modifiers.
- UTF-8 tests cover scalar boundaries, malformed encodings, and truncated prefixes.
- Encoding UI tests exercise ImGui code points, pass-through paste, Unicode
  selection/copy, invalid-byte rendering, and Linux pseudo-terminal loopback
  with single-byte serial reads.
- Logger tests use unique temporary directories and require no user files.
- Configuration tests cover explicit default creation, legacy files, malformed
  and partial TOML, future versions, strict enum/boolean/numeric validation,
  oversized input, every setting's round trip, failed saves, backup recovery,
  and log directory validation.
- Capture-session tests cover open/read/write failures, short writes, retry
  timing, cancellation, repeated reconfiguration, modem controls, log/raw flush,
  and deterministic worker shutdown. Linux PTY tests exercise native hangup and
  reconnection, port cycles, and partial-open descriptor cleanup.
- Capture UI tests render setup, connecting, connected, reconnecting, cancelled,
  and failed states in a headless ImGui context and check modal dismissal and
  error visibility. They exercise Setup's Save button, restart persistence,
  failed saves, zoom persistence, owned combo values, and a Linux native PTY
  smoke for saved flow control, newline mode, and log output location.
- Raw-capture tests verify all 256 byte values, split reads, exact ANSI and
  malformed UTF-8 retention before parsing, start/stop behavior, and disk errors.
- UI zoom tests cover consecutive zoom steps, scrolling near the bottom,
  and connection-dialog measurements after font and style changes.
- Terminal-view UI tests check complete layout snapshots, current-frame
  hit testing at several scales, margin toggles, independent UI/device cursors,
  actual keyboard/mouse events, clipboard actions, selection/color draw passes,
  cursor blink/focus states, and independent mouse/keyboard enable flags.

Warning checks use `/W4` on MSVC and `-Wall -Wextra -Wpedantic` on other compilers
for `imterm_core`, `imterm_session`, `imterm_view`, and their tests. They are
enabled by default and can be controlled with `IMTERM_ENABLE_WARNINGS`.
Warnings are not treated as errors.

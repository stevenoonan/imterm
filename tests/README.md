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
- Logger tests use unique temporary directories and require no user files. Text
  logs are compared in native text mode (LF on Linux, CRLF on Windows); raw
  capture assertions remain byte-exact.
- Configuration tests cover explicit default creation, legacy files, malformed
  and partial TOML, future versions, strict enum/boolean/numeric validation,
  oversized input, every setting's round trip, failed saves, backup recovery,
  and log directory validation. Preservation tests write byte-exact LF and CRLF
  fixtures and compare the original/backup bytes without text translation.
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

Warning checks use `/W4` on MSVC and `-Wall -Wextra -Wpedantic` on GCC/Clang
for every first-party target, including the application. They are enabled by
default through `IMTERM_ENABLE_WARNINGS`. `IMTERM_WARNINGS_AS_ERRORS=ON` enables
the full warning policy plus `/WX` or `-Werror`; CI requires it. Third-party
sources and headers are isolated from that policy.

## Graphics lifecycle tests

`IMTERM_ENABLE_GRAPHICS_TESTS=ON` adds the `ApplicationLifecycle` CTest entry.
It uses the production application shell in a single process, injects exceptions
after each startup resource and during rendering/resize, checks partial
swapchain-image cleanup and restart, and repeatedly launches, renders, resizes,
and shuts down. It also checks that the ImGui context is released and INI state
is saved to a temporary directory. These tests require a display and Vulkan
driver; ordinary core/session/UI tests remain headless.

```sh
cmake --preset conan-debug -DIMTERM_ENABLE_GRAPHICS_TESTS=ON -DIMTERM_WARNINGS_AS_ERRORS=ON
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure -L graphics
```

Linux CI provides Xvfb, Mesa lavapipe, and Khronos validation layers. It sets
`IMTERM_VALIDATE_VULKAN=1`; validation errors, including leaked Vulkan objects at
teardown, fail the tests. No GPU or serial hardware is needed.
To reproduce that environment, install `xvfb`, `mesa-vulkan-drivers`, and optionally
`vulkan-validationlayers`, then run the CI driver with `--graphics --lavapipe`
under `xvfb-run -a`. The driver discovers the installed `lvp_icd.json` or legacy
`lvp_icd.<architecture>.json` and sets both `VK_DRIVER_FILES` and
`VK_ICD_FILENAMES`. A missing manifest fails before dependency installation with
an actionable diagnostic. When running CTest directly, set these variables to
the actual manifest under `/usr/share/vulkan/icd.d`. Clear
`WAYLAND_DISPLAY` and set `XDG_SESSION_TYPE=x11` for an Xvfb run if needed.
Set `IMTERM_VALIDATE_VULKAN=1` to require the validation layer in lifecycle tests.
For manual Vulkan diagnostics, install your platform's validation layers and
set `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` before launch.

## Continuous integration

Every push (all branches and tags), pull request, merge-queue group, and manual
workflow run executes this matrix:

| Platform / compiler | Configurations | GLFW/ImGui dependencies | Tests |
| --- | --- | --- | --- |
| Ubuntu 24.04 / GCC | Debug, Release | Conan, submodules | Core, session, headless UI, Linux PTY, Vulkan lifecycle |
| Ubuntu 24.04 / Clang | Debug, Release | Conan, submodules | Core, session, headless UI, Linux PTY, Vulkan lifecycle |
| Windows 2022 / MSVC x64 | Debug, Release | Conan, submodules | Core, session, headless UI |
| Ubuntu 24.04 / Clang sanitizers | Debug | Submodules | Core, session, UI, PTY under ASan/UBSan; 30-second seeded libFuzzer run |

This is 12 ordinary build/test jobs plus a sanitizer/fuzz job and a stable `CI`
summary check that requires every job to succeed. `CI` can be selected as the
required branch-protection status. Push triggers also
cover the resulting merge commit. `merge_group` covers GitHub's merge queue.
Jobs do not cancel siblings after one failure. CTest timeouts prevent hangs,
empty test discovery is an error, and JUnit/CTest logs and configure diagnostics
are uploaded even on failure. The sanitizer job also uploads its fuzz corpus
and any crash inputs. The workflow uses a read-only repository token and installs
Conan as the runner user; only Linux system-package installation uses sudo.

The cross-platform driver can reproduce one job in a fresh checkout. It needs
Conan 2, CMake 3.24+, Ninja, and initialized submodules for a submodule job. Set
`CC`/`CXX` for GCC or Clang, or run from an MSVC x64 developer shell:

```sh
python scripts/ci.py --configuration Debug --dependencies conan
python scripts/ci.py --configuration Release --dependencies submodules
# Linux: run under Xvfb with the installed Mesa CPU driver.
xvfb-run -a python scripts/ci.py --configuration Debug --graphics --lavapipe
# With Clang selected:
python scripts/ci.py --sanitizers --fuzz --dependencies submodules
```

Each driver invocation first runs the Python CI-driver regression tests, including
current/legacy lavapipe filenames and missing-driver diagnostics. These can also
be run without Conan using `python -m unittest discover -s tests -p test_ci_driver.py`.

The driver generates a separate `imterm-ci` Conan profile and writes only
`out/ci` and `out/ci-deps` plus Conan's normal cache and generated ImGui bindings.
Remove those two build directories before changing compilers. Add
`--install-system-packages` to explicitly allow Conan to install system
prerequisites through sudo on Linux; it is unnecessary when already installed.
The normal developer presets are preserved.

Windows GUI launch/resize/DPI testing, Wayland compositor behavior, and physical
UART/modem-line operations remain manual smoke checks. The automated Linux PTY
checks cover connection, transfer, reconfiguration, hangup/reconnect and logging,
but do not represent physical DTR/RTS/CTS/DSR/DCD behavior. Follow the
[full smoke checklist](../REFACTORING_PLAN.md#manual-smoke-test-checklist) before
release; CI cannot certify those hardware-dependent checks.

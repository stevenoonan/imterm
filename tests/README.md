# Testing imterm

The automated tests exercise `imterm_core` without creating an ImGui context,
Vulkan device, or serial connection. GoogleTest provides assertions and test
discovery; CTest is the project-level runner.

## Configure and run

Install Debug and Release dependencies after changing `conanfile.py`:

```sh
PATH="$PWD/.venv/bin:$PATH" ./scripts/bootstrap.sh
```

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
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure
```

Repeat for Release:

```sh
cmake --preset conan-release
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
```

Tests are enabled through CMake's standard `BUILD_TESTING` option. A build that
does not need tests may configure with `-DBUILD_TESTING=OFF`.

## Sanitizers and fuzzing

AddressSanitizer and UndefinedBehaviorSanitizer can be enabled for the
first-party targets in a separate GCC or Clang build:

```sh
cmake --preset conan-debug -DIMTERM_ENABLE_SANITIZERS=ON
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure
```

Clang users can also build the opt-in libFuzzer boundary target:

```sh
cmake --preset conan-debug -DCMAKE_CXX_COMPILER=clang++ -DIMTERM_BUILD_FUZZER=ON
cmake --build --preset conan-debug --target imterm_parser_fuzz
./out/conan/build/Debug/imterm_parser_fuzz -max_total_time=60
```

## Test categories

- Parser tests characterize byte-by-byte escape-sequence parsing.
- Terminal-state tests cover text input, newline modes, chunked sequences,
  colors, cursor movement, erasure, scrollback, and terminal responses.
- Terminal-data tests cover buffer text, coordinates, tabs, insertion, and
  deletion.
- Terminal-input tests lock down the keyboard sequences sent to the device.
- Logger tests use unique temporary directories and require no user files.

The baseline warning policy is `/W4` on MSVC and `-Wall -Wextra -Wpedantic` on
other compilers for `imterm_core` and its tests. Warnings are not errors yet:
the terminal core still has known signed/unsigned and API warnings that later
milestones will remove. The application target also compiles third-party ImGui
backend sources directly, so its warning isolation is deferred until those
sources have their own target.

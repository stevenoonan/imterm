# Building imterm

Run the commands below from the repository root. The normal build uses Conan
packages for GLFW and Dear ImGui; initializing their Git submodules is optional.
Other bundled libraries under `deps/` are built directly by CMake.

## Requirements

- A C++20 compiler and a build tool supported by CMake.
- CMake 3.24 or newer for the repository's version-5 preset file.
- [Conan 2](https://docs.conan.io/2/installation.html) on `PATH`.
- The platform development packages requested by Conan. Linux builds enable
  both X11 and Wayland support and use ALSA for the terminal bell.

Running the GUI requires a working Vulkan loader and graphics driver. Conan
provides library dependencies, but does not remove the need for platform runtime
libraries and drivers. Keep `JetBrainsMono-Medium.ttf`, copied by the build, next
to the executable to use the bundled font.

## Linux / Bash

If needed, install Conan in a Python virtual environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install conan
```

Install dependencies, configure, and build:

```sh
./scripts/bootstrap.sh
cmake --preset conan-debug
cmake --build --preset conan-debug
./out/conan/build/Debug/imterm
```

The bootstrap script preserves an existing default Conan profile, detecting one
only if it is missing. It installs both Debug and Release dependencies into
`out/conan`, generates `CMakeUserPresets.json`, and does not invoke `sudo` or
install system packages. Re-run it after changing dependencies or removing
generated build files.

For Release, use `conan-release` in both CMake commands. With the default
single-configuration generator, the executable is under
`out/conan/build/Release/`.

## Windows and custom Conan profiles

Use a Visual Studio developer shell when building with MSVC so Conan and CMake
can find the intended compiler. Install Conan 2 and Ninja for the example below.
If no default profile exists, create one with `conan profile detect`.

Run the dependency-install commands directly without the Bash wrapper. Selecting
Ninja explicitly gives the same per-configuration preset names as the Bash
example:

```sh
conan install . --output-folder=out/conan --build=missing -s build_type=Release -c tools.cmake.cmaketoolchain:generator=Ninja
conan install . --output-folder=out/conan --build=missing -s build_type=Debug -c tools.cmake.cmaketoolchain:generator=Ninja
cmake --preset conan-debug
cmake --build --preset conan-debug
```

Conan prints the generated preset names and build directories. If you choose a
different generator, use the preset names it prints; multi-configuration
generators can use different names and executable subdirectories.
Visual Studio can also open the repository as a CMake folder and use the
generated presets to build and run `imterm`.

To use a custom profile, add `--profile=PROFILE_NAME` to each `conan install`
command. Use `conan profile path default` to locate the default profile and
`conan profile show` to inspect the selected settings. Ensure the profile matches
the compiler and architecture in your shell.

Use a fresh build directory when changing generators or compilers.

## Missing system packages

If Conan reports that `tools.system.package_manager:mode` is `check`, install
the listed packages using your operating system's package manager. To explicitly
allow Conan to install them on a system using `sudo`, repeat the install with:

```sh
conan install . --output-folder=out/conan --build=missing -s build_type=Debug \
  -c tools.system.package_manager:mode=install \
  -c tools.system.package_manager:sudo=True
```

Repeat for Release if needed.

## Optional GLFW and ImGui submodules

Initialize the submodules before enabling their CMake options:

```sh
git submodule update --init --recursive
cmake --preset conan-debug -DGLFW_USE_SUBMODULE=ON -DIMGUI_USE_SUBMODULE=ON
cmake --build --preset conan-debug
```

Either option can be enabled independently. Other Conan dependencies are still
required. The build enables `IMGUI_USE_WCHAR32` consistently for the submodule
ImGui library and its consumers; the Conan recipe enables the corresponding
`use_wchar32` option for the package build.

See [Testing imterm](../tests/README.md) for CTest, sanitizer, and fuzzing commands.

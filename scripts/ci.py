#!/usr/bin/env python3
"""Reproduce one GitHub CI build locally (requires Conan 2, CMake and Ninja)."""

import argparse
import os
import platform
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def find_lavapipe(directory=Path("/usr/share/vulkan/icd.d"), architecture=None):
    """Find Mesa's current or architecture-qualified legacy driver manifest."""
    architecture = architecture or platform.machine()
    for name in ("lvp_icd.json", f"lvp_icd.{architecture}.json"):
        manifest = directory / name
        if manifest.is_file():
            return manifest.resolve()
    raise RuntimeError(
        f"No lavapipe Vulkan manifest in {directory}; install mesa-vulkan-drivers. "
        f"Expected lvp_icd.json or lvp_icd.{architecture}.json."
    )


def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--dependencies", choices=("conan", "submodules"), default="conan")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--graphics", action="store_true")
    parser.add_argument("--lavapipe", action="store_true",
                        help="Select the installed Mesa CPU driver for Linux graphics tests")
    parser.add_argument("--fuzz", action="store_true")
    parser.add_argument("--install-system-packages", action="store_true")
    parser.add_argument("--parallel", type=int, default=2)
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    if args.lavapipe:
        if not args.graphics or sys.platform != "linux":
            parser.error("--lavapipe requires --graphics on Linux")
        manifest = find_lavapipe()
        print("Vulkan software driver:", manifest, flush=True)
        # Set both names so modern and older loaders select the same driver,
        # even if the caller already configured one of these overrides.
        os.environ["VK_DRIVER_FILES"] = str(manifest)
        os.environ["VK_ICD_FILENAMES"] = str(manifest)
    if args.dependencies == "submodules":
        for file in ("deps/glfw/CMakeLists.txt", "deps/imgui/imgui.cpp"):
            if not (ROOT / file).is_file():
                parser.error("Initialize submodules with git submodule update --init --recursive")

    run(sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_ci_driver.py")

    # Keep every compiler/configuration invocation in a separate checkout/build.
    # Conan's environment selects the compiler (CC/CXX or MSVC developer shell).
    build = ROOT / "out/ci"
    deps = ROOT / "out/ci-deps"
    run("conan", "profile", "detect", "--name=imterm-ci", "--force")
    install = ["conan", "install", ".", "--output-folder=" + str(deps), "--build=missing",
               "-pr:h=imterm-ci", "-pr:b=imterm-ci", "-s:h", "compiler.cppstd=20",
               "-s:h", "build_type=" + args.configuration,
               "-c", "tools.cmake.cmaketoolchain:generator=Ninja",
               "-c", "tools.cmake.cmaketoolchain:user_presets="]
    if args.install_system_packages:
        install += ["-c", "tools.system.package_manager:mode=install",
                    "-c", "tools.system.package_manager:sudo=True"]
    run(*install)
    toolchain = deps / "build" / args.configuration / "generators/conan_toolchain.cmake"
    if not toolchain.is_file():
        raise RuntimeError("Expected Conan's single-configuration Ninja toolchain at " + str(toolchain))
    submodules = "ON" if args.dependencies == "submodules" else "OFF"
    run("cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain),
        "-DCMAKE_BUILD_TYPE=" + args.configuration,
        "-DBUILD_TESTING=ON", "-DIMTERM_WARNINGS_AS_ERRORS=ON",
        "-DGLFW_USE_SUBMODULE=" + submodules, "-DIMGUI_USE_SUBMODULE=" + submodules,
        "-DIMTERM_ENABLE_SANITIZERS=" + ("ON" if args.sanitizers else "OFF"),
        "-DIMTERM_ENABLE_GRAPHICS_TESTS=" + ("ON" if args.graphics else "OFF"),
        "-DIMTERM_BUILD_FUZZER=" + ("ON" if args.fuzz else "OFF"))
    run("cmake", "--build", build, "--parallel", args.parallel)
    run("ctest", "--test-dir", build, "--output-on-failure", "--no-tests=error",
        "--parallel", args.parallel, "--output-junit", build / "test-results.xml")
    if args.fuzz:
        corpus = build / "fuzz-corpus"
        corpus.mkdir(exist_ok=True)
        # Seed both valid and malformed protocol/UTF-8 boundaries.
        for name, data in {"sgr": b"\x1b[31mred\x1b[0m\r\n", "utf8": "a😀é".encode(),
                           "truncated": b"\x1b[999999999999999999;\xff\xe2\x82"}.items():
            (corpus / name).write_bytes(data)
        run(build / ("imterm_parser_fuzz.exe" if os.name == "nt" else "imterm_parser_fuzz"),
            corpus, "-max_total_time=30", "-timeout=5", "-max_len=4096",
            "-artifact_prefix=" + str(build) + os.sep)


if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)

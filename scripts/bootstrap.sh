#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v conan >/dev/null 2>&1; then
  echo "Conan 2 is required but was not found in PATH." >&2
  echo "Install it with 'pipx install conan' and run this script again." >&2
  exit 1
fi

if ! conan profile path default >/dev/null 2>&1; then
  conan profile detect
fi

COMMON_CONAN_ARGS=(
  --output-folder=out/conan
  --build=missing
)

conan install . "${COMMON_CONAN_ARGS[@]}" -s build_type=Release
conan install . "${COMMON_CONAN_ARGS[@]}" -s build_type=Debug

echo "Bootstrap complete."
echo "Use one of: cmake --preset conan-debug  |  cmake --preset conan-release"

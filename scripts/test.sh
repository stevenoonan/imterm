#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

usage() {
  cat <<'EOF'
Usage: ./scripts/test.sh [debug|release|all] [TEST_REGEX]

Configure, build, and run imterm's automated tests.

Arguments:
  debug       Run the Debug tests (default).
  release     Run the Release tests.
  all         Run both Debug and Release tests.
  TEST_REGEX  Optional CTest regular expression used to select a test group.

Examples:
  ./scripts/test.sh
  ./scripts/test.sh release
  ./scripts/test.sh all
  ./scripts/test.sh debug TerminalStateTest
  ./scripts/test.sh all 'Parser|TerminalInput'
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if (( $# > 2 )); then
  usage >&2
  exit 2
fi

configuration="${1:-debug}"
test_regex="${2:-}"

case "$configuration" in
  debug)
    configurations=(debug)
    ;;
  release)
    configurations=(release)
    ;;
  all)
    configurations=(debug release)
    ;;
  *)
    echo "Unknown configuration: $configuration" >&2
    usage >&2
    exit 2
    ;;
esac

for command_name in cmake ctest; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "$command_name is required but was not found in PATH." >&2
    exit 1
  fi
done

if [[ ! -f CMakeUserPresets.json ]]; then
  echo "CMakeUserPresets.json was not found." >&2
  echo "Run this first: PATH=\"\$PWD/.venv/bin:\$PATH\" ./scripts/bootstrap.sh" >&2
  exit 1
fi

for current_configuration in "${configurations[@]}"; do
  preset="conan-$current_configuration"

  echo
  echo "==> Configuring $current_configuration tests"
  cmake --preset "$preset" -DBUILD_TESTING=ON

  echo
  echo "==> Building $current_configuration"
  cmake --build --preset "$preset"

  ctest_args=(--preset "$preset" --output-on-failure)
  if [[ -n "$test_regex" ]]; then
    ctest_args+=(-R "$test_regex")
  fi

  echo
  if [[ -n "$test_regex" ]]; then
    echo "==> Running $current_configuration tests matching: $test_regex"
  else
    echo "==> Running all $current_configuration tests"
  fi
  ctest "${ctest_args[@]}"
done

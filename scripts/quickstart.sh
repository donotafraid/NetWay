#!/usr/bin/env bash
# One-command build + test for a fresh checkout.
#
#   ./scripts/quickstart.sh
#
# On Ubuntu/Debian this installs the C++ toolchain and Conan when they are
# missing, builds the pinned open62541 v1.4.14 tree, resolves the Conan
# dependencies, then configures, builds and runs ctest.  Existing trees can be
# reused to avoid network access:
#
#   OPEN62541_ROOT=/path/to/open62541-v1.4.14 \
#   CONAN_DIR=/path/to/conan_build \
#   BUILD_DIR=build ./scripts/quickstart.sh
#
# The script is intentionally boring: no hidden environment, no global install
# outside apt/pip, and every command is idempotent.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
ROOT=$(pwd)

BUILD_DIR=${BUILD_DIR:-build}
OPEN62541_ROOT=${OPEN62541_ROOT:-$ROOT/third_party/open62541-v1.4.14}
CONAN_DIR=${CONAN_DIR:-$ROOT/conan_build}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
CC=${CC:-clang-17}
CXX=${CXX:-clang++-17}

SANITIZE=0
for arg in "$@"; do
  case "$arg" in
    --sanitize) SANITIZE=1 ;;
  esac
done


log() { printf '\n=== %s ===\n' "$*"; }
as_root() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  else
    sudo "$@"
  fi
}

export PATH="$HOME/.local/bin:$PATH"

# ---------------------------------------------------------------------------
# 1. Toolchain + Conan.  Ubuntu 22.04 ships clang-14; the project pins the
#    Clang 17/libc++ toolchain.  Add the LLVM apt repository when needed.
# ---------------------------------------------------------------------------
missing=0
for tool in cmake git python3 wget "$CC" "$CXX"; do
  command -v "$tool" >/dev/null 2>&1 || missing=1
done
# clang++ alone is not enough: the project compiles against libc++ headers.
[ -d /usr/lib/llvm-17/include/c++/v1 ] || missing=1
command -v conan >/dev/null 2>&1 || missing=1

if [ "$missing" -eq 1 ]; then
  if ! command -v apt-get >/dev/null 2>&1; then
    echo "error: missing build tools and apt-get is unavailable." >&2
    echo "Install cmake, git, python3, clang-17/libc++-17 and Conan 2, then rerun." >&2
    exit 2
  fi

  log "Installing build dependencies (Ubuntu/Debian)"
  as_root apt-get update
  as_root apt-get install -y --no-install-recommends \
    build-essential cmake git python3 python3-pip wget ca-certificates

  if ! apt-cache policy clang-17 2>/dev/null | grep -q 'Candidate: [0-9]'; then
    tmp_llvm=$(mktemp)
    wget -q -O "$tmp_llvm" https://apt.llvm.org/llvm.sh
    chmod +x "$tmp_llvm"
    as_root "$tmp_llvm" 17
    rm -f "$tmp_llvm"
  fi
  as_root apt-get install -y --no-install-recommends \
    clang-17 libc++-17-dev libc++abi-17-dev

  if ! command -v conan >/dev/null 2>&1; then
    python3 -m pip install --user "conan>=2,<3" ||
      python3 -m pip install --user --break-system-packages "conan>=2,<3"
  fi
  export PATH="$HOME/.local/bin:$PATH"
fi

# ---------------------------------------------------------------------------
# 2. Pinned open62541 v1.4.14 with UA_MULTITHREADING=100.
# ---------------------------------------------------------------------------
if [ ! -f "$OPEN62541_ROOT/build/bin/libopen62541.so" ]; then
  log "Building open62541 v1.4.14 into $OPEN62541_ROOT"
  CC="$CC" CXX="$CXX" bash scripts/build_open62541.sh "$OPEN62541_ROOT"
else
  log "Reusing open62541 at $OPEN62541_ROOT"
fi

# ---------------------------------------------------------------------------
# 3. Conan dependencies (fmt/spdlog/Backward/GTest/CURL/...).
# ---------------------------------------------------------------------------
if [ ! -f "$CONAN_DIR/conan_toolchain.cmake" ]; then
  log "Installing Conan dependencies into $CONAN_DIR"
  conan profile detect --force >/dev/null
  conan install . -of "$CONAN_DIR" -s build_type=Debug \
    -s compiler=clang -s compiler.version=17 \
    -s compiler.libcxx=libc++ -s compiler.cppstd=gnu17 \
    --build=missing
else
  log "Reusing Conan toolchain at $CONAN_DIR"
fi

# ---------------------------------------------------------------------------
# 4. Configure, build, test.
# ---------------------------------------------------------------------------
log "Configuring $BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
  -DOPEN62541_ROOT="$OPEN62541_ROOT" \
  -DCMAKE_TOOLCHAIN_FILE="$CONAN_DIR/conan_toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$CONAN_DIR" \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"

log "Building"
cmake --build "$BUILD_DIR" -j "$JOBS"

if [ "$SANITIZE" -eq 1 ]; then
  log "Running sanitizer suites"
  bash "$ROOT/scripts/sanitizers.sh"
else
  log "Running tests"
  ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

log "Done"

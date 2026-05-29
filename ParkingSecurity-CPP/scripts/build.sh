#!/usr/bin/env bash
# Build parking-security. By default: Release, with Qt UI and DeepStream.
# Usage:  ./scripts/build.sh [Debug|Release]  [extra cmake args...]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-Release}"
shift || true
BUILD_DIR="${ROOT_DIR}/build/${BUILD_TYPE,,}"

# Ensure CUDA is in PATH (JetPack installs nvcc to /usr/local/cuda/bin)
if [ -x /usr/local/cuda/bin/nvcc ]; then
    export PATH="/usr/local/cuda/bin:${PATH}"
    export CUDACXX="/usr/local/cuda/bin/nvcc"
fi

echo "→ Configuring build in ${BUILD_DIR} (${BUILD_TYPE})..."
mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$@"

echo "→ Building with $(nproc) parallel jobs..."
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

# Symlink compile_commands.json to project root (clangd picks it up)
ln -sf "${BUILD_DIR}/compile_commands.json" "${ROOT_DIR}/compile_commands.json" || true

echo
echo "✓ Built: ${BUILD_DIR}/parking-security"

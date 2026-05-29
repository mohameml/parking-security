#!/usr/bin/env bash
# Install build dependencies for parking-security on a Jetson Orin Nano (JetPack 6.x).
# Requires sudo. Idempotent — safe to re-run.
set -euo pipefail

echo "═══════════════════════════════════════════════════════"
echo "  Parking Security — Jetson Setup"
echo "═══════════════════════════════════════════════════════"

# ─── System info ─────────────────────────────────────────────
echo
echo "→ System info:"
uname -a
cat /etc/nv_tegra_release 2>/dev/null | head -1 || true

# ─── Base build tools ────────────────────────────────────────
echo
echo "→ Installing base build tools..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    ca-certificates \
    curl

# ─── GStreamer ───────────────────────────────────────────────
echo
echo "→ Installing GStreamer..."
sudo apt-get install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-tools

# ─── PostgreSQL client + libpqxx ─────────────────────────────
echo
echo "→ Installing PostgreSQL client + libpqxx..."
sudo apt-get install -y \
    libpq-dev \
    libpqxx-dev

# ─── OpenCV (CPU, minimal) ───────────────────────────────────
echo
echo "→ Installing OpenCV dev..."
sudo apt-get install -y libopencv-dev

# ─── Qt 6 ────────────────────────────────────────────────────
echo
echo "→ Installing Qt 6..."
sudo apt-get install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-quickcontrols2 \
    qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts \
    qml6-module-qtquick-window

# ─── Logging / JSON (optional: FetchContent will grab them otherwise) ─
echo
echo "→ Installing spdlog / nlohmann-json (optional, speeds up first build)..."
sudo apt-get install -y libspdlog-dev nlohmann-json3-dev || true

# ─── CUDA / TensorRT (verify — not installing; JetPack ships them) ──
echo
echo "→ Verifying CUDA / TensorRT..."
if [ -x /usr/local/cuda/bin/nvcc ]; then
    /usr/local/cuda/bin/nvcc --version | tail -1
else
    echo "  WARNING: /usr/local/cuda/bin/nvcc not found. Is JetPack installed?"
fi
dpkg -l | grep -E '^ii  libnvinfer10 ' | awk '{print "  TensorRT:", $3}' || \
    echo "  WARNING: libnvinfer not found. TensorRT must come from JetPack."

# ─── DeepStream (optional but recommended) ───────────────────
echo
if [ -d /opt/nvidia/deepstream/deepstream ]; then
    echo "→ DeepStream found at /opt/nvidia/deepstream/deepstream"
    ls -1 /opt/nvidia/deepstream/deepstream/lib/ | head -5
else
    echo "→ DeepStream NOT found. Install from:"
    echo "    https://developer.nvidia.com/deepstream-download"
    echo "  Build will proceed without it (PARKING_USE_DEEPSTREAM=OFF)."
fi

echo
echo "═══════════════════════════════════════════════════════"
echo "  Setup complete. Next:"
echo "    ./scripts/convert_models.sh"
echo "    ./scripts/build.sh"
echo "═══════════════════════════════════════════════════════"

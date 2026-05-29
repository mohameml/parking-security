#!/usr/bin/env bash
# Convert InsightFace buffalo_l ONNX models to TensorRT engines, tuned for the Jetson.
#
# The engines produced here are LOCKED to:
#   - The exact GPU architecture (Jetson Orin Nano = sm_87)
#   - The TensorRT version installed
#   - The build (FP16, INT8, etc.)
# So they must be regenerated on the target device, not checked into git.
#
# Inputs:  buffalo_l ONNX files in ~/.insightface/models/buffalo_l/
#          (copied from the Python project via `rsync` during deploy)
# Outputs: models/*.engine
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="${ROOT_DIR}/models"
SOURCE_DIR="${1:-${HOME}/.insightface/models/buffalo_l}"

PRECISION="${PRECISION:-fp16}"
PRECISION_FLAG=""
[ "${PRECISION}" = "fp16" ] && PRECISION_FLAG="--fp16"
[ "${PRECISION}" = "int8" ] && PRECISION_FLAG="--int8"

echo "═══════════════════════════════════════════════════════"
echo "  Converting buffalo_l ONNX → TensorRT engines"
echo "  Precision: ${PRECISION}"
echo "  Source:    ${SOURCE_DIR}"
echo "  Output:    ${MODELS_DIR}"
echo "═══════════════════════════════════════════════════════"

# ─── Locate or build trtexec ─────────────────────────────────────
# On JetPack 6.x trtexec is shipped as source in /usr/src/tensorrt/samples/trtexec
# and the binary only appears under /usr/src/tensorrt/bin/ after you compile it.
TRTEXEC=""
for candidate in \
    "$(command -v trtexec 2>/dev/null || true)" \
    "/usr/src/tensorrt/bin/trtexec" \
    "/usr/lib/aarch64-linux-gnu/tensorrt/bin/trtexec"; do
    if [ -n "${candidate}" ] && [ -x "${candidate}" ]; then
        TRTEXEC="${candidate}"
        break
    fi
done

if [ -z "${TRTEXEC}" ]; then
    if [ -d /usr/src/tensorrt/samples/trtexec ]; then
        echo "trtexec binary not found; building from /usr/src/tensorrt/samples/trtexec"
        (cd /usr/src/tensorrt/samples/trtexec && sudo make -j"$(nproc)") 1>&2
        if [ -x /usr/src/tensorrt/bin/trtexec ]; then
            TRTEXEC=/usr/src/tensorrt/bin/trtexec
        fi
    fi
fi

if [ -z "${TRTEXEC}" ]; then
    echo "ERROR: trtexec not found. Install JetPack's TensorRT samples:" >&2
    echo "  sudo apt install tensorrt" >&2
    echo "  cd /usr/src/tensorrt/samples/trtexec && sudo make -j" >&2
    exit 1
fi

echo "Using trtexec: ${TRTEXEC}"

if [ ! -d "${SOURCE_DIR}" ]; then
    echo "ERROR: ONNX source directory not found: ${SOURCE_DIR}" >&2
    echo "  Copy buffalo_l from the Python project (~/.insightface/models/buffalo_l/)." >&2
    exit 1
fi

mkdir -p "${MODELS_DIR}"

# ─── Per-model converter ─────────────────────────────────────────
# $1 = base name (no extension)
# $2 = extra trtexec args (e.g. dynamic-shape flags)
convert() {
    local name="$1"
    local extra="${2:-}"
    local onnx="${SOURCE_DIR}/${name}.onnx"
    local engine="${MODELS_DIR}/${name}.engine"

    if [ ! -f "${onnx}" ]; then
        echo "  SKIP (missing): ${onnx}"
        return
    fi

    if [ -f "${engine}" ] && [ "${engine}" -nt "${onnx}" ]; then
        echo "  up-to-date: ${engine}"
        return
    fi

    echo "→ Converting ${name}..."
    # shellcheck disable=SC2086
    "${TRTEXEC}" \
        --onnx="${onnx}" \
        --saveEngine="${engine}" \
        ${PRECISION_FLAG} \
        --memPoolSize=workspace:4096 \
        --avgRuns=10 \
        --warmUp=200 \
        ${extra} \
        2>&1 | grep -E '(throughput|latency|Total Host Walltime|error|ERROR)' || true

    if [ ! -f "${engine}" ]; then
        echo "ERROR: ${engine} was not produced" >&2
        return 1
    fi
}

# SCRFD det_10g — the ONNX has DYNAMIC input dims (?x3x?x?), so trtexec needs
# explicit shapes or it builds a broken 1x3x1x1 engine. Input tensor is "input.1".
convert det_10g "--minShapes=input.1:1x3x640x640 \
    --optShapes=input.1:1x3x640x640 \
    --maxShapes=input.1:1x3x640x640"

# ArcFace w600k_r50 — 112×112 input, static in buffalo_l exports; if dynamic,
# the same trick applies. Input tensor is typically "input.1".
convert w600k_r50 "--minShapes=input.1:1x3x112x112 \
    --optShapes=input.1:1x3x112x112 \
    --maxShapes=input.1:1x3x112x112"

# Optional landmark models (fixed shapes in the buffalo_l exports).
convert 1k3d68  || true
convert 2d106det || true

echo
echo "✓ Done. Engines in ${MODELS_DIR}:"
ls -lh "${MODELS_DIR}"/*.engine 2>/dev/null || echo "  (none generated)"

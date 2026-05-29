#!/usr/bin/env bash
# Deploy parking-security to a Jetson over SSH.
#
# Usage:
#   ./scripts/deploy_jetson.sh user@host [/install/prefix]
#
# This rsyncs the source, builds on the target, and installs the binary +
# config files. For a one-shot dev-loop, it's faster to just run build.sh
# directly on the Jetson.
set -euo pipefail

TARGET="${1:?Usage: deploy_jetson.sh user@host [/install/prefix]}"
PREFIX="${2:-/opt/parking-security}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "→ Syncing source to ${TARGET}:~/parking-security-cpp..."
rsync -az --delete \
    --exclude build/ --exclude .git/ --exclude models/*.engine \
    "${ROOT_DIR}/" "${TARGET}:~/parking-security-cpp/"

echo "→ Building on target..."
ssh "${TARGET}" 'bash -l -c "cd ~/parking-security-cpp && ./scripts/build.sh Release"'

echo "→ Installing to ${PREFIX}..."
ssh "${TARGET}" "sudo cmake --install ~/parking-security-cpp/build/release --prefix ${PREFIX}"

echo "✓ Deployed to ${TARGET}:${PREFIX}"
echo
echo "To run:  ssh ${TARGET} ${PREFIX}/bin/parking-security"

#!/usr/bin/env bash
# One-shot installer for parking-security on the Jetson.
#
# Does:
#   1. Copies build artifacts, configs, models, and the SCRFD parser .so into
#      /opt/parking-security/.
#   2. Writes logrotate config.
#   3. Installs systemd units: parking-security.service (the daemon) and
#      parking-snapshots-cleanup.{service,timer} (nightly cleanup).
#   4. Creates an env file /etc/parking-security/parking-security.env from
#      the example template if it doesn't already exist (so repeated installs
#      don't clobber your secrets).
#   5. Reloads systemd.
#
# After running this: stop the /tmp/run_daemon.sh wrapper and start the
# service:
#     sudo pkill -f parking-security
#     sudo systemctl enable --now parking-security
#     sudo systemctl enable --now parking-snapshots-cleanup.timer
#     journalctl -u parking-security -f
#
# Idempotent — safe to re-run after a rebuild to refresh binaries.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/release"
TARGET_DIR="${INSTALL_PREFIX:-/opt/parking-security}"
ENV_DIR="/etc/parking-security"
ENV_FILE="${ENV_DIR}/parking-security.env"
UNIT_DIR="/etc/systemd/system"
LOGROTATE_FILE="/etc/logrotate.d/parking-security"

if [ ! -x "${BUILD_DIR}/parking-security" ]; then
    echo "ERROR: ${BUILD_DIR}/parking-security not found. Run ./scripts/build.sh first." >&2
    exit 1
fi

# When the script is invoked via `sudo`, $(id -un) returns 'root'. We want
# the install to end up owned by whoever actually ran sudo — otherwise the
# daemon (running as the systemd unit's User=) can't write logs.
TARGET_USER="${SUDO_USER:-$(id -un)}"
TARGET_GROUP="$(id -gn "${TARGET_USER}")"

echo "→ /opt/parking-security install layout"
sudo mkdir -p "${TARGET_DIR}"/{bin,config,models,lib,data}
sudo mkdir -p /var/log/parking-security

sudo install -m 755 "${BUILD_DIR}/parking-security"          "${TARGET_DIR}/bin/"
sudo install -m 755 "${BUILD_DIR}/tools/test_pipeline"       "${TARGET_DIR}/bin/" 2>/dev/null || true

if [ -f "${ROOT_DIR}/lib/libnvds_parsebbox_scrfd.so" ]; then
    sudo install -m 644 "${ROOT_DIR}/lib/libnvds_parsebbox_scrfd.so" "${TARGET_DIR}/lib/"
fi

sudo cp -r "${ROOT_DIR}/config/." "${TARGET_DIR}/config/"
if [ -d "${ROOT_DIR}/models" ]; then
    sudo cp -r "${ROOT_DIR}/models/." "${TARGET_DIR}/models/"
fi
# Embeddings json — copy once, don't clobber a re-exported one.
if [ -f "${ROOT_DIR}/data/embeddings.json" ] && \
   [ ! -f "${TARGET_DIR}/data/embeddings.json" ]; then
    sudo cp "${ROOT_DIR}/data/embeddings.json" "${TARGET_DIR}/data/embeddings.json"
fi
sudo chown -R "${TARGET_USER}:${TARGET_GROUP}" "${TARGET_DIR}" /var/log/parking-security

echo "→ /etc/parking-security/parking-security.env"
sudo mkdir -p "${ENV_DIR}"
if [ ! -f "${ENV_FILE}" ]; then
    sudo install -m 600 "${ROOT_DIR}/config/parking-security.env.example" "${ENV_FILE}"
    sudo chown root:root "${ENV_FILE}"
    echo "   ↳ created from template — edit ${ENV_FILE} to set secrets!"
else
    echo "   ↳ already exists, left alone (edit it directly if you want to change secrets)"
fi

echo "→ /etc/logrotate.d/parking-security"
sudo install -m 644 "${ROOT_DIR}/config/parking-security.logrotate" "${LOGROTATE_FILE}"

echo "→ systemd units"
sudo install -m 644 "${ROOT_DIR}/scripts/parking-security.service"           "${UNIT_DIR}/"
sudo install -m 644 "${ROOT_DIR}/scripts/parking-snapshots-cleanup.service"  "${UNIT_DIR}/"
sudo install -m 644 "${ROOT_DIR}/scripts/parking-snapshots-cleanup.timer"    "${UNIT_DIR}/"
sudo systemctl daemon-reload

cat <<EOF

✓ Installed.

Enable + start:
    sudo systemctl enable --now parking-security
    sudo systemctl enable --now parking-snapshots-cleanup.timer

Follow logs:
    journalctl -u parking-security -f

Env file (edit secrets here):
    sudo nano ${ENV_FILE}
EOF

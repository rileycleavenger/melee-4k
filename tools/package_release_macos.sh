#!/bin/bash
# Build the player-facing macOS release: dist/Melee4K-macOS-<arch>.zip holding
#
#   Melee4K/
#     Melee4K.app           the game (tools/package_macos.sh)
#     PUT-YOUR-ROM-HERE/    the player drops their disc image here
#     Setup.command         one-time setup (platforms/release/Setup.command)
#     .setup/melee4k-setup  tools/setup/setup.py frozen with PyInstaller
#     README.txt            platforms/release/README.txt
#
# Setup downloads the newest texture pack release by default (setup.py's
# DEFAULT_MANIFEST). Needs the repo .venv with pyinstaller
# (`.venv/bin/pip install pyinstaller`).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARCH="$(uname -m)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos-release}"
DIST_DIR="${ROOT_DIR}/dist"
STAGE="${DIST_DIR}/Melee4K"
PY="${ROOT_DIR}/.venv/bin/python"

BUILD_DIR="${BUILD_DIR}" "${SCRIPT_DIR}/package_macos.sh"

echo "=== Freezing Setup ==="
"${PY}" -m pip install -q truststore certifi
SETUP_WORK="${BUILD_DIR}/setup-freeze"
rm -rf "${SETUP_WORK}"
SETUP_SRC="${ROOT_DIR}/tools/setup/setup.py"
"${PY}" -m PyInstaller --onefile --console --collect-data certifi --name melee4k-setup \
    --distpath "${SETUP_WORK}/dist" --workpath "${SETUP_WORK}/build" \
    --specpath "${SETUP_WORK}" "${SETUP_SRC}"

echo "=== Assembling ${STAGE} ==="
rm -rf "${STAGE}"
mkdir -p "${STAGE}/PUT-YOUR-ROM-HERE" "${STAGE}/.setup"
ditto "${BUILD_DIR}/Melee.app" "${STAGE}/Melee4K.app"
cp "${SETUP_WORK}/dist/melee4k-setup" "${STAGE}/.setup/"
cp "${ROOT_DIR}/platforms/release/Setup.command" "${STAGE}/"
chmod +x "${STAGE}/Setup.command" "${STAGE}/.setup/melee4k-setup"
cp "${ROOT_DIR}/platforms/release/README.txt" "${STAGE}/"
printf 'Put your Super Smash Bros. Melee (USA 1.02) disc image in this folder.\n' \
    > "${STAGE}/PUT-YOUR-ROM-HERE/put your .iso here.txt"
codesign --force --sign - --timestamp=none "${STAGE}/.setup/melee4k-setup"

echo "=== Zipping ==="
rm -f "${DIST_DIR}/Melee4K-macOS-${ARCH}.zip"
ditto -c -k --keepParent "${STAGE}" "${DIST_DIR}/Melee4K-macOS-${ARCH}.zip"
ls -lh "${DIST_DIR}/Melee4K-macOS-${ARCH}.zip"

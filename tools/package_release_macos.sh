#!/bin/bash
# Build the player-facing macOS release: dist/<FILE>-macOS-<arch>.zip holding
# (<FILE> and <slug> from product.env: Melee4K, melee4k)
#
#   <FILE>/
#     <FILE>.app            the game (tools/package_macos.sh)
#     PUT-YOUR-ROM-HERE/    the player drops their disc image here
#     Setup.command         one-time setup (platforms/release/Setup.command)
#     .setup/<slug>-setup   tools/setup/setup.py frozen with PyInstaller
#     README.txt            platforms/release/README.txt
#     CREDITS.txt           the custom fighters' model credits (with FIGHTERS)
#
# FIGHTERS=<folder of packs> ships those custom fighters inside the app
# (Contents/Resources/fighters); the product must load packs
# (PRODUCT_FIGHTER_PACKS=1: Melee++, not Melee 4K).
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
source "${ROOT_DIR}/product.env"
STAGE="${DIST_DIR}/${PRODUCT_FILE}"
ZIP="${DIST_DIR}/${PRODUCT_FILE}-macOS-${ARCH}.zip"
if [[ -n "${FIGHTERS:-}" && "${PRODUCT_FIGHTER_PACKS}" != 1 ]]; then
    echo "FIGHTERS given, but product.env has PRODUCT_FIGHTER_PACKS=${PRODUCT_FIGHTER_PACKS}" >&2
    exit 1
fi
PY="${ROOT_DIR}/.venv/bin/python"

BUILD_DIR="${BUILD_DIR}" "${SCRIPT_DIR}/package_macos.sh"

echo "=== Freezing Setup ==="
"${PY}" -m pip install -q truststore certifi
SETUP_WORK="${BUILD_DIR}/setup-freeze"
rm -rf "${SETUP_WORK}"
SETUP_SRC="${ROOT_DIR}/tools/setup/setup.py"
"${PY}" -m PyInstaller --onefile --console --collect-data certifi --name "${PRODUCT_SLUG}-setup" \
    --add-data "${ROOT_DIR}/product.env:." \
    --distpath "${SETUP_WORK}/dist" --workpath "${SETUP_WORK}/build" \
    --specpath "${SETUP_WORK}" "${SETUP_SRC}"

echo "=== Assembling ${STAGE} ==="
rm -rf "${STAGE}"
mkdir -p "${STAGE}/PUT-YOUR-ROM-HERE" "${STAGE}/.setup"
ditto "${BUILD_DIR}/Melee.app" "${STAGE}/${PRODUCT_FILE}.app"
if [[ -n "${FIGHTERS:-}" ]]; then
    mkdir -p "${STAGE}/${PRODUCT_FILE}.app/Contents/Resources/fighters"
    # -X: no extended attributes (the zip would carry them as ._ files)
    cp -RX "${FIGHTERS}"/. "${STAGE}/${PRODUCT_FILE}.app/Contents/Resources/fighters/"
    codesign --force --deep --sign - --timestamp=none "${STAGE}/${PRODUCT_FILE}.app"
    codesign --verify --deep --strict "${STAGE}/${PRODUCT_FILE}.app"
    # Every pack's model credits, where players see them.
    for credits in "${FIGHTERS}"/*/CREDITS.txt; do
        [[ -f "${credits}" ]] && { cat "${credits}"; printf '\n\n'; }
    done > "${STAGE}/CREDITS.txt"
fi
cp "${SETUP_WORK}/dist/${PRODUCT_SLUG}-setup" "${STAGE}/.setup/"
cp "${ROOT_DIR}/platforms/release/Setup.command" "${STAGE}/"
chmod +x "${STAGE}/Setup.command" "${STAGE}/.setup/${PRODUCT_SLUG}-setup"
cp "${ROOT_DIR}/platforms/release/README.txt" "${STAGE}/"
printf 'Put your Super Smash Bros. Melee (USA 1.02) disc image in this folder.\n' \
    > "${STAGE}/PUT-YOUR-ROM-HERE/put your .iso here.txt"
codesign --force --sign - --timestamp=none "${STAGE}/.setup/${PRODUCT_SLUG}-setup"

echo "=== Zipping ==="
rm -f "${ZIP}"
# No extended attributes (macOS tags files with com.apple.provenance): the
# zip would carry them as ._ files.
ditto -c -k --norsrc --noextattr --keepParent "${STAGE}" "${ZIP}"
ls -lh "${ZIP}"

#!/bin/bash
# Melee 4K installer for macOS (Apple Silicon). Paste into Terminal:
#
#   curl -fsSL https://raw.githubusercontent.com/rileycleavenger/melee-4k/v1-pc/install.sh | bash
#
# Files fetched by curl are not marked as downloaded from the internet, so
# macOS never shows its "could not verify" dialog for them. The script
# downloads the newest release, opens the PUT-YOUR-ROM-HERE folder, waits for
# your Melee disc image, runs Setup (which checks the disc and installs the HD
# textures) and adds Melee 4K to Applications and the Desktop. Running it
# again updates an existing install and keeps your disc, saves, settings and
# textures.
set -euo pipefail

REPO="rileycleavenger/melee-4k"
DEST="${MELEE4K_DIR:-$HOME/Melee4K}"
ASSET="Melee4K-macOS-arm64.zip"

say() { printf '%s\n' "$*"; }
fail() { say ""; say "$*"; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || fail "This installer is for macOS; on Windows use Setup.exe from the release zip."
[[ "$(uname -m)" == "arm64" ]] || fail "Melee 4K needs a Mac with Apple Silicon (M1 or newer)."

say "Melee 4K installer"
say ""
say "[1/4] Finding the newest release..."
# The latest release (not pre-releases), or the one MELEE4K_VERSION names
# (a tag, e.g. v1.1.0-rc1, for testing a build before it is released).
# Plain grep/sed, since a fresh Mac has no python3 without developer tools.
if [[ -n "${MELEE4K_VERSION:-}" ]]; then
    API="https://api.github.com/repos/${REPO}/releases/tags/${MELEE4K_VERSION}"
else
    API="https://api.github.com/repos/${REPO}/releases/latest"
fi
URL="$(curl -fsSL "$API" |
    grep -o "\"browser_download_url\": *\"[^\"]*/${ASSET}\"" | head -n 1 |
    sed -E 's/.*"(https[^"]+)"$/\1/' || true)"
[[ -n "$URL" ]] || fail "Could not reach GitHub to find the download; check your internet connection and run the command again."

say "[2/4] Downloading Melee 4K..."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fL --progress-bar -o "$TMP/$ASSET" "$URL" ||
    fail "The download stopped; check your internet connection and run the command again."
ditto -x -k "$TMP/$ASSET" "$TMP/unzipped"

# Keep the player's disc image across updates; replace everything else.
mkdir -p "$DEST/PUT-YOUR-ROM-HERE"
rm -rf "$DEST/Melee4K.app" "$DEST/.setup"
ditto "$TMP/unzipped/Melee4K/Melee4K.app" "$DEST/Melee4K.app"
ditto "$TMP/unzipped/Melee4K/.setup" "$DEST/.setup"
cp "$TMP/unzipped/Melee4K/README.txt" "$DEST/README.txt"
cp "$TMP/unzipped/Melee4K/Setup.command" "$DEST/Setup.command"
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

has_disc() {
    [[ -n "$(find "$DEST/PUT-YOUR-ROM-HERE" -maxdepth 1 \( -type f -o -type l \) \
        \( -iname '*.iso' -o -iname '*.gcm' -o -iname '*.ciso' -o -iname '*.rvz' -o -iname '*.gcz' \) \
        -print -quit)" ]]
}

say "[3/4] Your Melee disc image..."
if ! has_disc; then
    open "$DEST/PUT-YOUR-ROM-HERE"
    say "    A Finder window opened: drag your Melee (USA 1.02) disc image into it."
    say "    Waiting..."
    while ! has_disc; do sleep 2; done
    sleep 2 # let a large copy finish before Setup reads it
    while lsof +D "$DEST/PUT-YOUR-ROM-HERE" >/dev/null 2>&1; do sleep 2; done
fi

say "[4/4] Running Setup..."
"$DEST/.setup/melee4k-setup" --root "$DEST" --no-launch </dev/null || exit 1

# The app itself lives in ~/Applications, where Launchpad and Spotlight look
# (neither lists symlinks); the install folder keeps a link for Setup.
APPS="$HOME/Applications"
mkdir -p "$APPS"
rm -rf "$APPS/Melee 4K.app"
mv "$DEST/Melee4K.app" "$APPS/Melee 4K.app"
ln -s "$APPS/Melee 4K.app" "$DEST/Melee4K.app"

# A Desktop shortcut, made again on every update (a link, so Finder shows the
# game's icon and opens the app; one the player deleted comes back only when
# they run the installer again). Never replaces anything that is not ours.
DESK="$HOME/Desktop/Melee 4K.app"
if [[ -L "$DESK" || ! -e "$DESK" ]]; then
    ln -sfn "$APPS/Melee 4K.app" "$DESK"
fi

say ""
say "Installed. Next time, open Melee 4K from the Desktop, Launchpad or Spotlight (\"Melee 4K\")."
[[ -n "${MELEE4K_NO_LAUNCH:-}" ]] || open "$APPS/Melee 4K.app"

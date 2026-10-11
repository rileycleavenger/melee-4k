#!/bin/bash
# Melee 4K Setup (macOS). Right-click > Open the first time: the release is
# not signed by an Apple developer ID, so macOS asks once.
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR" || exit 1
# Opening this file was the approval; clear the download quarantine from the
# rest of the folder so the app and the Setup helper are allowed to run too.
xattr -dr com.apple.quarantine "$DIR" 2>/dev/null
"$DIR/.setup/melee4k-setup" --root "$DIR" "$@"
status=$?
echo
read -r -p "Press Return to close this window." _
exit $status

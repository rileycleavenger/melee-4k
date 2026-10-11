#!/bin/bash
# Melee 4K cross-platform check (macOS). Put this file and the Windows
# win-*.mrc recordings in your Melee4K folder, then run it from Terminal:
#   bash ~/Melee4K/crossplay-mac.command
# Replays each recording into results.txt; with --record it records mac-*.mrc.
cd "$(dirname "$0")" || exit 1
GAME="Melee4K.app/Contents/MacOS/melee"
DISC="$(find PUT-YOUR-ROM-HERE -maxdepth 1 \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.gcm' -o -iname '*.rvz' \) | head -n 1)"
[[ -n "$DISC" ]] || { echo "Put your disc image in PUT-YOUR-ROM-HERE first."; exit 1; }
export MELEE_BOOT_SCENE=vs MELEE_DEBUG_VS=cpu4 MELEE_NET_CHECKSUM_WIDE=1 MELEE_FRAME_INTERP=0
if [[ "${1:-}" == "--record" ]]; then
    for s in 1 2 3; do for t in 0 3 24; do
        echo "Recording mac-seed$s-stage$t ..."
        MELEE_SEED=$s MELEE_DEBUG_VS_STAGE=$t MELEE_NET_RECORD="raw-seed$s-stage$t.mrc" \
            MELEE_EXIT_AFTER_FRAMES=2400 MELEE_LOG_FILE="mac-seed$s-stage$t-record.log" \
            "$GAME" "$DISC" >/dev/null 2>&1
        # Replay and re-record: share the canonical run (interp_determinism.py).
        MELEE_SEED=$s MELEE_DEBUG_VS_STAGE=$t MELEE_NET_REPLAY="raw-seed$s-stage$t.mrc" \
            MELEE_NET_RECORD="mac-seed$s-stage$t.mrc" MELEE_EXIT_AFTER_FRAMES=2400 \
            MELEE_LOG_FILE="mac-seed$s-stage$t-canon.log" "$GAME" "$DISC" >/dev/null 2>&1
        rm -f "raw-seed$s-stage$t.mrc"
    done; done
    exit 0
fi
echo "Melee 4K cross-platform check" > results.txt
for r in win-*.mrc; do
    [[ -f "$r" ]] || continue
    n="${r%.mrc}"; s="${n#*seed}"; s="${s%%-*}"; t="${n##*stage}"
    echo "Replaying $r ..."
    MELEE_SEED=$s MELEE_DEBUG_VS_STAGE=$t MELEE_NET_REPLAY="$r" MELEE_EXIT_AFTER_FRAMES=2520 \
        MELEE_LOG_FILE="$n.log" "$GAME" "$DISC" >/dev/null 2>&1
    if grep -q "no divergence" "$n.log"; then echo "PASS $r" >> results.txt
    else echo "FAIL $r $(grep -o 'DIVERGED.*' "$n.log" | head -1)" >> results.txt; fi
done
cat results.txt

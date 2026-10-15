#!/bin/bash
# Rebuild <NAME>'s pack from scratch: overlays, model, baked textures per
# costume, menu images, costume files, fighter.cfg, validation (copy to
# ~/Melee4K-dev/fighters/<ID>/make_pack.sh and set the variables below).
#   REPO=~/Desktop/GitRepos/melee-4k ./make_pack.sh [--quick]
# --quick: 1024 px textures, light occlusion (~25 s); without it 2048 px (~80 s).
set -euo pipefail
ID=Xx                      # two-letter id
NAME="Name"                # menus and results (build_ui.py --name)
BASE_CODE=Mr               # base fighter
SYMBOL=PlyName5K           # fighter.cfg symbol stem
BUILD=build_name.py        # this character's Blender script
# Costumes: suffix -> palette changes (keys of the build's PALETTE); the
# first is the default. Python dict syntax.
COSTUMES_PY="{
    'Nr': {},
    'Re': {'cloth': (206, 40, 38)},
    'Gr': {'cloth': (40, 150, 64)},
}"
TEAMS="Re Nr Gr"           # red, blue, green team costumes
POSE="{'J08': (0, 0, -70), 'J31': (0, 0, 70)}"   # arms down (Mario's bones): stock, icon
CSP_POSE="$POSE"           # the portrait's pose
# Export meshes: their texture size (BIG ones at full size), occlusion rules (tools.md §5b).
BIG="('Xx_Body', 'Xx_Head', 'Xx_Cape')"
AO_FLOORS="{'Xx_Eyes': 1.0, 'Xx_Joints': 1.0}"
AO_NO_CAST="['Xx_Arms', 'Xx_Joints']"

G="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$HOME/Desktop/GitRepos/melee-4k}"
BASE="${BASE:-$HOME/Melee4K-dev/fighters/base/$BASE_CODE}"
PACK="${PACK:-$HOME/Melee4K-dev/fighters/packs/$ID}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
PY="$REPO/.venv/bin/python"
SIZE=2048; AO=96; [ "${1:-}" = --quick ] && { SIZE=1024; AO=24; }

"$PY" "$G/make_textures.py" "$G/tex"

mkdir -p "$G/ui" "$G/baked"
cat > "$G/.blender_job.py" <<PYEOF
DIR = '$BASE'; SKIP_REF = True; exec(open('$REPO/tools/fighter/blender_import_base.py').read())
REPO = '$REPO'; TEX = '$G/tex'; exec(open('$G/$BUILD').read())
COSTUMES = $COSTUMES_PY
BAKE = {n: ($SIZE if n in $BIG else $SIZE // 2) for n in OBS}
AO_FLOORS = $AO_FLOORS; AO_NO_CAST = $AO_NO_CAST; AO_SAMPLES = $AO
OUT = '$G/baked'; bake_all()
OUT = '$G/mesh.json'; exec(open('$REPO/tools/fighter/blender_export_fighter.py').read())
import json
_b = json.load(open('$G/baked/bake.json'))
COSTUMES = [{m: c[k] for m, c in _b.items()} for k in COSTUMES]
POSE = $POSE
CSP_POSE = $CSP_POSE
OUT = '$G/ui'; exec(open('$REPO/tools/fighter/blender_render_ui.py').read())
PYEOF
"$BLENDER" -b --factory-startup --python "$G/.blender_job.py" 2>&1 | grep -E "built|baked |exported|rendered|Error|Traceback|line [0-9]" || true

# Costume files: the mesh with each costume's baked textures.
COSTUMES=($("$PY" -c "import json;print(' '.join(next(iter(json.load(open('$G/baked/bake.json')).values()))))"))
mkdir -p "$PACK"
rm -rf "$PACK/textures" "$PACK"/Pl$ID??.dat
for c in "${COSTUMES[@]}"; do
  args=()
  for kv in $("$PY" -c "import json;[print(f'{m}={v[\"$c\"]}') for m,v in json.load(open('$G/baked/bake.json')).items()]"); do
    args+=(--texture "$kv")
  done
  sym="$SYMBOL"; [ "$c" = Nr ] || sym="$SYMBOL$c"     # the game's symbol rule (tools.md §3)
  "$PY" "$REPO/tools/fighter/build_dat.py" --base-dir "$BASE" --mesh "$G/mesh.json" --symbol "$sym" \
    --out "$PACK/Pl$ID$c.dat" --textures "$PACK/textures" "${args[@]}" | tail -1 | cut -c1-120
done
csp=(); stock=(); for i in "${!COSTUMES[@]}"; do csp+=("$G/ui/csp$i.png"); stock+=("$G/ui/stock$i.png"); done
"$PY" "$REPO/tools/fighter/build_ui.py" --icon "$G/ui/icon.png" --csp "${csp[@]}" --stock "${stock[@]}" \
  --name "$NAME" --textures "$PACK/textures" --out "$PACK/Pl${ID}Ui.dat"
cat > "$PACK/fighter.cfg" <<CFG
id $ID
name "$(echo "$NAME" | tr a-z A-Z)"
base $BASE_CODE
symbol $SYMBOL
costumes ${COSTUMES[*]}
teams $TEAMS
# moves Name     # compiled specials (moves.md)
# voice none     # none of the base's voice clips
# weight 1.0     # attribute multipliers: weight walk dash jump gravity fall air
CFG
"$PY" "$REPO/tools/fighter/validate.py" "$PACK" --base-dir "$BASE"

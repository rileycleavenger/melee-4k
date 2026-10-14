#!/bin/bash
# Rebuild <NAME>'s pack from scratch: textures, model, menu images, costume
# files, validation (copy to ~/Melee4K-dev/fighters/<ID>/make_pack.sh and
# set the variables below).
#   REPO=~/Desktop/GitRepos/melee-4k ./make_pack.sh
set -euo pipefail
ID=Xx                      # two-letter id
NAME="Name"                # results-screen name (build_ui.py --name)
BASE_CODE=Mr               # base fighter
SYMBOL=PlyName5K           # fighter.cfg symbol stem
BUILD=build_name.py        # this character's Blender script
COSTUMES=(Nr Re Gr)        # first is the default
POSE="{'J08': (0, 0, -70), 'J31': (0, 0, 70)}"   # portrait pose (bones of the base)

G="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$HOME/Desktop/GitRepos/melee-4k}"
BASE="${BASE:-$HOME/Melee4K-dev/fighters/base/$BASE_CODE}"
PACK="${PACK:-$HOME/Melee4K-dev/fighters/packs/$ID}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
PY="$REPO/.venv/bin/python"

# Each costume's material overrides, MATERIAL=PNG (empty for the default).
over() {
  local s; [ "$1" = "${COSTUMES[0]}" ] && s="" || s="_$(echo "$1" | tr A-Z a-z)"
  echo "xx_cloth=$G/tex/cloth$s.png xx_accent=$G/tex/accent$s.png"
}

REPO="$REPO" "$PY" "$G/make_textures.py" "$G/tex"

mkdir -p "$G/ui"
{
  echo "DIR = '$BASE'; exec(open('$REPO/tools/fighter/blender_import_base.py').read())"
  echo "REPO = '$REPO'; TEX = '$G/tex'; exec(open('$G/$BUILD').read())"
  echo "OUT = '$G/mesh.json'; exec(open('$REPO/tools/fighter/blender_export_fighter.py').read())"
  echo "COSTUMES = ["
  for c in "${COSTUMES[@]}"; do
    echo "  {$(for kv in $(over $c); do echo -n "'${kv%%=*}': '${kv#*=}', "; done)},"
  done
  echo "]"
  echo "POSE = $POSE"
  echo "OUT = '$G/ui'; exec(open('$REPO/tools/fighter/blender_render_ui.py').read())"
} > "$G/.blender_job.py"
"$BLENDER" -b --factory-startup --python "$G/.blender_job.py" 2>&1 | grep -E "built|exported|rendered|Error|Traceback" || true

mkdir -p "$PACK"
rm -rf "$PACK/textures"
for c in "${COSTUMES[@]}"; do
  args=(); for kv in $(over $c); do args+=(--texture "$kv"); done
  sym="$SYMBOL"; [ "$c" = Nr ] || sym="$SYMBOL$c"     # the game's symbol rule (tools.md §3)
  "$PY" "$REPO/tools/fighter/build_dat.py" --base-dir "$BASE" --mesh "$G/mesh.json" --symbol "$sym" \
    --out "$PACK/Pl$ID$c.dat" --textures "$PACK/textures" "${args[@]}" | tail -1
done
csp=(); stock=(); for i in "${!COSTUMES[@]}"; do csp+=("$G/ui/csp$i.png"); stock+=("$G/ui/stock$i.png"); done
"$PY" "$REPO/tools/fighter/build_ui.py" --icon "$G/ui/icon.png" --csp "${csp[@]}" --stock "${stock[@]}" \
  --name "$NAME" --out "$PACK/Pl${ID}Ui.dat"
[ -f "$PACK/fighter.cfg" ] || printf 'id %s\nname "%s"\nbase %s\nsymbol %s\ncostumes %s\n' \
  "$ID" "$(echo "$NAME" | tr a-z A-Z)" "$BASE_CODE" "$SYMBOL" "${COSTUMES[*]}" > "$PACK/fighter.cfg"
"$PY" "$REPO/tools/fighter/validate.py" "$PACK" --base-dir "$BASE"

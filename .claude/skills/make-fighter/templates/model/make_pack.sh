#!/bin/bash
# Rebuild <NAME>'s pack from the user's model: the model on the base's
# skeleton with its own textures (fit.py; one costume, the model as given),
# menu images, its own moves (moves.py), the stock icon (stock_icon.py),
# fighter.cfg, validation. Copy to ~/Melee4K-dev/fighters/<ID>/ and set the
# variables.
#   ./make_pack.sh            everything
#   ./make_pack.sh --inspect  the model's bones, skin and textures (step 2)
#   ./make_pack.sh --check    pose renders only: ui/check_<pose>.png (step 4)
set -euo pipefail
ID=Xx                      # two-letter id
ORDER=100                  # place among the packs on character select
NAME="Name"                # title case, as on the character select door
MOVES=""                   # its specials' name in the C (FtCustomMoves) when NAME has spaces
BASE_CODE=Mr               # base fighter
SYMBOL=PlyName5K           # fighter.cfg symbol stem
MODEL_SRC=model/Model.dae  # the user's file; converted to model/model.glb
SIZE=1.0                   # fighter.cfg size: the model's scale in the game (hitboxes too)
HIDE_JOINTS=""             # build_dat --hide-joints: parts shown only where moves scale them ("J11")
FORMS=()                   # build_dat --form PART:STATE=MATERIAL: another form's mesh (a vehicle)
ICON_JOINTS=""            # the head's joints ("J60"): the icon framed on them (base_joints.py)
ICON_POSE=idle            # the icon's pose: poses/<ICON_POSE>.json; "bind" for the bind pose (a base whose idle turns away)
CSP=jab                    # portrait pose: poses/<CSP>.json (a game pose dump)
RENDER_PY=""               # more blender_render_ui.py settings, Python lines (CSP_JOINTS, ICON_DIR...)
ATTRS="weight 1.0
walk 1.0
dash 1.0
jump 1.0
gravity 1.0
fall 1.0
air 1.0"                   # fighter.cfg attribute multipliers (moves.md §Attributes)

G="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$HOME/Desktop/GitRepos/melee-4k}"
BASE="${BASE:-$HOME/Melee4K-dev/fighters/base/$BASE_CODE}"
PACK="${PACK:-$HOME/Melee4K-dev/fighters/packs/$ID}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
PY="$REPO/.venv/bin/python"
MODE="${1:-}"

# The model as glTF (Blender 5 reads no .dae; assimp reads .dae/.fbx/.obj/...).
case "$MODEL_SRC" in
  *.glb|*.gltf) [ -f "$G/model/model.glb" ] || cp "$G/$MODEL_SRC" "$G/model/model.glb" ;;
  *) [ -f "$G/model/model.glb" ] || assimp export "$G/$MODEL_SRC" "$G/model/model.glb" -f glb2 >/dev/null ;;
esac

if [ "$MODE" = --inspect ]; then
  cat > "$G/.blender_job.py" <<PYEOF
import bpy, collections
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath='$G/model/model.glb')
for o in bpy.data.objects:
    print('OBJECT', o.type, o.name, 'parent', o.parent.name if o.parent else '-', [m.type for m in o.modifiers])
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
skin = collections.Counter()
for o in bpy.data.objects:
    if o.type == 'MESH':
        print('MESH', o.name, len(o.data.polygons), 'faces, materials', [m.name for m in o.data.materials],
              'uv', [u.name for u in o.data.uv_layers])
        for v in o.data.vertices:
            for g in v.groups:
                if g.weight > 0.01:
                    skin[o.vertex_groups[g.group].name] += 1
for b in arm.data.bones:
    h = arm.matrix_world @ b.head_local
    print('BONE', b.name, 'parent', b.parent.name if b.parent else '-', 'head', tuple(round(x, 3) for x in h), 'skin', skin.get(b.name, 0))
for m in bpy.data.materials:
    print('MATERIAL', m.name, [n.image.filepath or ('packed ' + n.image.name) for n in (m.node_tree.nodes if m.use_nodes else []) if n.type == 'TEX_IMAGE' and n.image])
PYEOF
  "$BLENDER" -b --factory-startup --python "$G/.blender_job.py" 2>&1 | grep -E '^(OBJECT|MESH|BONE|MATERIAL)|Error'
  exit 0
fi

mkdir -p "$G/ui" "$G/tex"
cat > "$G/.blender_job.py" <<PYEOF
DIR = '$BASE'; SKIP_REF = True; exec(open('$REPO/tools/fighter/blender_import_base.py').read())
G = '$G'; REPO = '$REPO'; exec(open('$G/fit.py').read())
import bpy, glob, json, os
# The model's own textures, as PNGs.
tex = save_textures(OBS, '$G/tex/original', search=[G + '/model'] + list(globals().get('TEX_SEARCH', [])),
                    colors=globals().get('TEX_COLORS', {}),
                    assign={k: G + '/model/' + v for k, v in globals().get('TEX_ASSIGN', {}).items()})
use_textures(OBS, tex)   # each image on its material's base colour, nothing else lit or see-through
CHECK_POSES = sorted(glob.glob('$G/poses/*.json'))
COSTUMES = [tex]
if '$MODE' == '--check':
    CHECK_ONLY = True
    OUT = '$G/ui'; exec(open('$REPO/tools/fighter/blender_render_ui.py').read())
    raise SystemExit
OUT = '$G/mesh.json'; exec(open('$REPO/tools/fighter/blender_export_fighter.py').read())
POSE = {}
ICON_JOINTS = '${ICON_JOINTS:-}'.split()
ICON_POSE = '$G/poses/${ICON_POSE:-idle}.json' if os.path.exists('$G/poses/${ICON_POSE:-idle}.json') else {}
CSP_POSE = '$G/poses/$CSP.json' if os.path.exists('$G/poses/$CSP.json') else {}
$RENDER_PY
OUT = '$G/ui'; exec(open('$REPO/tools/fighter/blender_render_ui.py').read())
PYEOF
"$BLENDER" -b --factory-startup --python "$G/.blender_job.py" 2>&1 | grep -E "retargeted|prop |exported|rendered|Error|Traceback|line [0-9]" || true
[ "$MODE" = --check ] && exit 0

mkdir -p "$PACK"
rm -rf "$PACK/textures" "$PACK"/Pl$ID??.dat
forms=(); for f in ${FORMS[@]+"${FORMS[@]}"}; do forms+=(--form "$f"); done
"$PY" "$REPO/tools/fighter/build_dat.py" --base-dir "$BASE" --mesh "$G/mesh.json" --symbol "$SYMBOL" \
  --out "$PACK/Pl${ID}Nr.dat" --textures "$PACK/textures" --hide-joints "$HIDE_JOINTS" \
  ${forms[@]+"${forms[@]}"} | tail -1 | cut -c1-120
# Its own attacks, taunt and special animations.
"$PY" "$REPO/tools/fighter/build_moves.py" "$G/moves.py" --base-dir "$BASE" --base "$BASE_CODE" --out "$PACK/motions.bin" | tail -1
# The stock icon: the head render made like Melee's own (stock_icon.py).
"$PY" "$REPO/tools/fighter/stock_icon.py" "$G/ui/stock0.png" "$G/ui/stock_px0.png"
"$PY" "$REPO/tools/fighter/build_ui.py" --icon "$G/ui/icon.png" --csp "$G/ui/csp0.png" \
  --stock "$G/ui/stock_px0.png" --name "$NAME" --textures "$PACK/textures" --out "$PACK/Pl${ID}Ui.dat"
cat > "$PACK/fighter.cfg" <<CFG
id $ID
name "$NAME"
base $BASE_CODE
symbol $SYMBOL
costumes Nr
moves ${MOVES:-$NAME}  # its specials' C (src/melee/ft/kinds/ft${MOVES:-$NAME}/), compiled in
order $ORDER  # place on character select among the packs, lowest first
voice none   # its own voice is not in the game: none of the base's
size $SIZE
${HIDE_JOINTS:+hidden $HIDE_JOINTS}
# Attribute multipliers on the base's (moves.md §Attributes): from how the
# character moves in its games.
$ATTRS
CFG
# Its own effects (beams, glows, trails...: docs/fighters.md).
if [ -f "$G/vfx.cfg" ]; then cp "$G/vfx.cfg" "$PACK/vfx.cfg"; else rm -f "$PACK/vfx.cfg"; fi
"$PY" "$REPO/tools/fighter/validate.py" "$PACK" --base-dir "$BASE"

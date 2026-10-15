#!/bin/bash
# Rebuild <NAME>'s pack from the user's model: the model on the base's
# skeleton (fit.py), the two costumes (Nr: Melee-ified; the second: the
# model as given), menu images, its own moves (moves.py), the pixel stock
# icon (make_stock.py), fighter.cfg, validation. Copy to
# ~/Melee4K-dev/fighters/<ID>/ and set the variables.
#   ./make_pack.sh            everything
#   ./make_pack.sh --inspect  the model's bones, skin and textures (step 2)
#   ./make_pack.sh --check    pose renders only: ui/check_<pose>.png (step 4)
set -euo pipefail
ID=Xx                      # two-letter id
NAME="Name"                # title case, as on the character select door
BASE_CODE=Mr               # base fighter
SYMBOL=PlyName5K           # fighter.cfg symbol stem
MODEL_SRC=model/Model.dae  # the user's file; converted to model/model.glb
SECOND=Bu                  # the second costume's suffix (the model as given)
LO_RATIO=0.6               # Melee-ified mesh: share of the triangles kept
MELEE_STRENGTH=1.0         # Melee-ified texture: shading strength
ICON_JOINTS=""            # the head's joints ("J60"): the icon framed on them (base_joints.py)
CSP=jab                    # portrait pose: poses/<CSP>.json (a game pose dump)

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
# The model's textures as PNGs; ambient occlusion baked for any the model
# brings no AO map for (its own maps, if any: tex/<texture>_ao.png and
# _nrm.png, put there by hand from the model's files).
tex = save_textures(OBS, '$G/tex/original', search=[G + '/model'] + list(globals().get('TEX_SEARCH', [])), colors=globals().get('TEX_COLORS', {}), assign={k: G + '/model/' + v for k, v in globals().get('TEX_ASSIGN', {}).items()})
need = [t for t in set(tex.values()) if not os.path.exists(t[:-4] + '_ao.png')]
if need and '$MODE' != '--check':
    bake_ao({n: o for n, o in OBS.items() if any(m['hsd_texture'] in need for m in o.data.materials)}, '$G/tex/original')
json.dump(tex, open('$G/tex/materials.json', 'w'))
PYEOF
if true; then
  "$BLENDER" -b --factory-startup --python "$G/.blender_job.py" 2>&1 | grep -E "retargeted|removed|Error|Traceback|line [0-9]" || true
  # The Melee-ified textures.
  mkdir -p "$G/tex/melee"
  for t in "$G"/tex/original/*.png; do
    case "$t" in *_ao.png|*_nrm.png) continue ;; esac
    b="${t%.png}"; args=()
    [ -f "${b}_ao.png" ] && args+=(--ao "${b}_ao.png")
    [ -f "${b}_nrm.png" ] && args+=(--normal "${b}_nrm.png")
    "$PY" "$REPO/tools/fighter/melee_texture.py" "$t" "$G/tex/melee/$(basename "$t")" --strength "$MELEE_STRENGTH" ${args[@]+"${args[@]}"}
  done
fi

cat > "$G/.blender_job.py" <<PYEOF
DIR = '$BASE'; SKIP_REF = True; exec(open('$REPO/tools/fighter/blender_import_base.py').read())
G = '$G'; REPO = '$REPO'; exec(open('$G/fit.py').read())
import bpy, glob, json, os
tex = json.load(open('$G/tex/materials.json'))
melee = {m: '$G/tex/melee/' + os.path.basename(p) for m, p in tex.items()}
use_textures(OBS, melee)
CHECK_POSES = sorted(glob.glob('$G/poses/*.json'))
if '$MODE' == '--check':
    COSTUMES = [melee]; CHECK_ONLY = True
    OUT = '$G/ui'; exec(open('$REPO/tools/fighter/blender_render_ui.py').read())
    raise SystemExit
OUT = '$G/mesh_original.json'; exec(open('$REPO/tools/fighter/blender_export_fighter.py').read())
COSTUMES = [melee, tex]
POSE = {}
ICON_JOINTS = '${ICON_JOINTS:-}'.split()
ICON_POSE = '$G/poses/idle.json' if os.path.exists('$G/poses/idle.json') else {}
CSP_POSE = '$G/poses/$CSP.json' if os.path.exists('$G/poses/$CSP.json') else {}
OUT = '$G/ui'; exec(open('$REPO/tools/fighter/blender_render_ui.py').read())
melee_mesh(OBS, $LO_RATIO)
OUT = '$G/mesh_melee.json'; exec(open('$REPO/tools/fighter/blender_export_fighter.py').read())
PYEOF
"$BLENDER" -b --factory-startup --python "$G/.blender_job.py" 2>&1 | grep -E "retargeted|exported|rendered|melee mesh|Error|Traceback|line [0-9]" || true
[ "$MODE" = --check ] && exit 0

mkdir -p "$PACK"
rm -rf "$PACK/textures" "$PACK"/Pl$ID??.dat
"$PY" "$REPO/tools/fighter/build_dat.py" --base-dir "$BASE" --mesh "$G/mesh_melee.json" --symbol "$SYMBOL" \
  --out "$PACK/Pl${ID}Nr.dat" --textures "$PACK/textures" | tail -1 | cut -c1-120
orig=(); while IFS= read -r kv; do orig+=(--texture "$kv"); done < <("$PY" -c "import json;[print(f'{m}={p}') for m,p in json.load(open('$G/tex/materials.json')).items()]")  # names may have spaces
"$PY" "$REPO/tools/fighter/build_dat.py" --base-dir "$BASE" --mesh "$G/mesh_original.json" --symbol "$SYMBOL$SECOND" \
  --out "$PACK/Pl${ID}$SECOND.dat" --textures "$PACK/textures" ${orig[@]+"${orig[@]}"} | tail -1 | cut -c1-120
# Its own attacks, taunt and special animations.
"$PY" "$REPO/tools/fighter/build_moves.py" "$G/moves.py" --base-dir "$BASE" --base "$BASE_CODE" --out "$PACK/motions.bin" | tail -1
# The stock icon: hand-drawn pixel art at 24x24.
"$PY" "$G/make_stock.py" "$G/ui/stock_px.png"
"$PY" "$REPO/tools/fighter/build_ui.py" --icon "$G/ui/icon.png" --csp "$G/ui/csp0.png" "$G/ui/csp1.png" \
  --stock "$G/ui/stock_px.png" "$G/ui/stock_px.png" --name "$NAME" --textures "$PACK/textures" --out "$PACK/Pl${ID}Ui.dat"
cat > "$PACK/fighter.cfg" <<CFG
id $ID
name "$NAME"
base $BASE_CODE
symbol $SYMBOL
costumes Nr $SECOND
moves ${MOVES:-$NAME}  # its specials' C (src/melee/ft/kinds/ft${MOVES:-$NAME}/), compiled in
order ${ORDER:-100}  # place on character select among the packs, lowest first
voice none   # its own voice is not in the game: none of the base's
# Attribute multipliers on the base's (moves.md §Attributes): from how the
# character moves in its games.
weight 1.0
walk 1.0
dash 1.0
jump 1.0
gravity 1.0
fall 1.0
air 1.0
CFG
"$PY" "$REPO/tools/fighter/validate.py" "$PACK" --base-dir "$BASE"

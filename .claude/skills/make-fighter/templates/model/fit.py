"""<NAME>'s model (model/<file>.glb, the user's) on the base's skeleton:
blender_retarget.py's settings. exec'ed by make_pack.sh inside Blender with
G (this folder) and REPO set, after blender_import_base.py.

Fill in from three looks (tools.md §6b):
  1. the model's bones and which carry skin (make_pack.sh --inspect),
  2. the base's joints (tools/fighter/base_joints.py BASE),
  3. renders of the result in the base's poses (make_pack.sh --check).

The example map is a humanoid model (Mixamo-like names) on Mario (Mr).
"""
SRC = G + '/model/model.glb'
SRC_MESH_ROT_X = 0      # 90 if assimp's .dae left the meshes lying down (render them to see)
NAME = '<ID>'
# Model units -> game units: the base's height / the model's (Mario's
# head top is J24 at y 12; a 1.15-unit-tall model: ~10-11). Larger makes
# the character bigger than the base on screen (hitboxes stay the base's).
SCALE = globals().get('SCALE', 11.0)
# Bones whose skin is a spare part kept out of sight (alternate hands,
# faces, weapons parked far from the body): their faces are dropped.
DROP = [r'(Arm|ForeArm|Hand)[12]$']
# model bone -> base joint, or (base joint, model child bone): the bone's
# segment to that child is turned and stretched onto the base's segment
# to the child's joint. Hips to chest to head; each arm's upper arm,
# forearm, hand; fingers onto the base's finger joints (so the base's hand
# poses curl them); each leg's thigh, shin, foot.
MAP = {
    'Hips': ('J04', 'Spine2'), 'Spine2': ('J05', 'Head'), 'Head': 'J22',
    'LeftShoulder': ('J06', 'LeftArm'), 'LeftArm': ('J08', 'LeftForeArm'),
    'LeftForeArm': ('J09', 'LeftHand'), 'LeftHand': 'J10',
    'RightShoulder': ('J29', 'RightArm'), 'RightArm': ('J31', 'RightForeArm'),
    'RightForeArm': ('J32', 'RightHand'), 'RightHand': 'J33',
    'LeftUpLeg': ('J48', 'LeftLeg'), 'LeftLeg': ('J49', 'LeftFoot'), 'LeftFoot': 'J51',
    'RightUpLeg': ('J54', 'RightLeg'), 'RightLeg': ('J55', 'RightFoot'), 'RightFoot': 'J57',
}
# Weighted to another joint than placed at: the head is placed at the
# neck and turns with the base's head joint; a joint the game never
# animates (a zero matrix in pose dumps, Mario's J44) gets none.
WEIGHT_AS = {'Head': 'J23'}
# Bones that move with another: a spine bone under the root, a loose
# accessory bone (hair, a ribbon) that belongs to the head.
FOLLOW = {'Spine1': 'Hips'}
# Feet keep their own height, so the soles stay on the ground.
KEEP_Y = ['LeftFoot', 'RightFoot']
exec(open(REPO + '/tools/fighter/blender_retarget.py').read()); OBS = retarget()

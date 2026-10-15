"""<NAME>'s own moves (tools/fighter/build_moves.py): every attack, the
taunt and the special moves' animations, new; the base's timing kept.

Design first (notes.md "Moveset"): each move from the character's own
games and media. Then pose it here. Model space: Y up, +Z front, +X the
fighter's left. Joint numbers below are Mario's (base_joints.py prints
any base's; on another base, change JOINTS first).

Every slot must be designed: one left as todo() stops the build.
"""
J = dict(trans=1, xrot=2, yrot=3, hips=4, waist=5, neck=22, head=23,
         l_arm=8, l_fore=9, l_hand=10, r_arm=31, r_fore=32, r_hand=33,
         l_thigh=48, l_shin=49, l_foot=51, l_toe=52, r_thigh=54, r_shin=55, r_foot=57, r_toe=58)
FWD, UP, DOWN, BACK = (0, 0, 1), (0, 1, 0), (0, -1, 0), (0, 0, -1)
PITCH, YAW, ROLL = (1, 0, 0), (0, 1, 0), (0, 0, 1)   # +PITCH leans forward, +YAW turns to its left
EL = "electric"


def todo(motion):
    raise SystemExit(f"moves.py: {motion} is not designed yet (every move is the character's own)")


def moves(ctx):
    # ---- helpers ---------------------------------------------------------
    def stance():
        """Its standing pose: the base's idle, adjusted to the character
        (arms, lean). Every ground move starts and ends here."""
        p = ctx.pose("Wait1", 0)
        p.aim(J["l_arm"], J["l_fore"], (0.35, -1, 0.15)).aim(J["l_fore"], J["l_hand"], (0.25, -1, 0.35))
        p.aim(J["r_arm"], J["r_fore"], (-0.35, -1, 0.15)).aim(J["r_fore"], J["r_hand"], (-0.25, -1, 0.35))
        return p

    def air():
        """Its falling pose; aerials start and end here."""
        return ctx.pose("Fall", 0)

    def arms(p, l=None, r=None, lf=None, rf=None):
        """Point the upper arms (l, r) and forearms (lf, rf; default: same)."""
        if l:
            p.aim(J["l_arm"], J["l_fore"], l).aim(J["l_fore"], J["l_hand"], lf or l)
        if r:
            p.aim(J["r_arm"], J["r_fore"], r).aim(J["r_fore"], J["r_hand"], rf or r)
        return p

    def legs(p, l=None, r=None, ls=None, rs=None):
        """Point the thighs (l, r) and shins (ls, rs; default: same)."""
        if l:
            p.aim(J["l_thigh"], J["l_shin"], l).aim(J["l_shin"], J["l_foot"], ls or l)
        if r:
            p.aim(J["r_thigh"], J["r_shin"], r).aim(J["r_shin"], J["r_foot"], rs or r)
        return p

    def hit(bone, damage, size, **kw):
        """A hitbox's new fields for ctx.rescript (bone, damage, size,
        offset, angle, kbg, bkb, wkb, element); the base's others kept.
        On joint 0 the offset is (forward, up, side)."""
        h = dict(bone=bone, damage=damage, size=size)
        h.update(kw)
        return h

    out = []
    # ---- a worked example: jab 1, a straight right punch ------------------
    # Base timing (frame_data.py Attack11): hit frames 2-4 of 16, 3
    # hitboxes. Keys: stance, the punch out by the hit frame, held over
    # the hit, back to the stance by the end.
    def punch():
        p = stance().turn(J["waist"], YAW, 30).turn(J["hips"], PITCH, 8)
        return arms(p, r=(-0.05, 0.08, 1))
    out.append(Move("Attack11", 16, [(0, stance()), (2, punch()), (5, punch()), (16, stance())],
                    ctx.rescript("Attack11", [hit(J["r_hand"], 3, 3.8, angle=80), hit(J["r_fore"], 3, 3.0, angle=80),
                                              hit(J["r_arm"], 3, 2.5, angle=80)])))

    # ---- every other slot: design each ------------------------------------
    for motion in ("Attack12", "Attack13",            # jab 2, 3 (Attack100: a rapid jab, if the base has one)
                   "AttackDash",                      # dash attack (root=True or translate=[1]: it travels)
                   "AttackS3S", "AttackS3Hi", "AttackS3Lw",  # forward tilt, and angled up/down
                   "AttackHi3", "AttackLw3",          # up tilt, down tilt (crouched)
                   "AttackS4S", "AttackS4Hi", "AttackS4Lw",  # forward smash, and angled
                   "AttackHi4", "AttackLw4",          # up smash, down smash
                   "AttackAirN", "AttackAirF", "AttackAirB", "AttackAirHi", "AttackAirLw",  # aerials
                   "Appeal",                          # taunt (drop 0x44 sounds, 0xB8)
                   # Special moves' animations (their effects and projectiles
                   # are C, moves.md): keep the base's script (throw frame,
                   # root=True for recoveries that rise).
                   "SpecialN", "SpecialAirN", "SpecialS", "SpecialSAir",
                   "SpecialHi", "SpecialAirHi", "SpecialLw", "SpecialAirLw",
                   # Grab game and get-ups (optional but part of a complete set):
                   # "CatchAttack", "ThrowF", "ThrowB", "ThrowHi", "ThrowLw",
                   # "DownAttackU", "DownAttackD", "CliffAttackQuick", "CliffAttackSlow",
                   ):
        todo(motion)
    return out

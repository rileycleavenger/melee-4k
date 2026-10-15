# SPDX-License-Identifier: GPL-3.0-or-later
"""Helpers for a fighter's moves spec (build_moves.py): the posing and
hitbox shorthand every spec repeats, over the base's own joint names.

    import sys; sys.path.insert(0, REPO + "/tools/fighter")   # build_moves does it
    from moveskit import Kit, FWD, UP, DOWN, BACK, PITCH, YAW, ROLL

    def moves(ctx):
        k = Kit(ctx, Move, J=dict(hips=4, chest=22, head=41, l_arm=25, l_fore=26,
                                  l_hand=27, r_arm=55, ...))
        k.attack("Attack11", 18, [(0, k.idle()), (3, jab), (18, k.idle())],
                 [k.hit("r_hand", 3, 3.4, angle=75)])
        k.special(["SpecialN", "SpecialAirN"], 40, keys, script)
        return k.out

J names: hips, waist, chest, neck, head; l_arm > l_fore > l_hand and r_...
(shoulder to elbow to wrist); l_thigh > l_shin > l_foot (> l_toe) and r_...;
any extra (tail, ears, cape...) by name. Joints a base lacks are left out
of J: arms() and legs() skip a missing side.
"""
import anim

FWD, UP, DOWN, BACK = (0, 0, 1), (0, 1, 0), (0, -1, 0), (0, 0, -1)
PITCH, YAW, ROLL = (1, 0, 0), (0, 1, 0), (0, 0, 1)
EFFECTS = (0x28, 0xA0)                 # rescript drops: graphics, model swaps
QUIET = (0x28, 0xA0, 0x44, 0xA4)       # ... and sounds, voice (taunts)


class Kit:
    def __init__(self, ctx, Move, J, idle="Wait1", fall="Fall", squat="SquatWait"):
        self.ctx, self.Move, self.J = ctx, Move, J
        self._idle, self._fall, self._squat = idle, fall, squat
        self.out = []

    # ---- poses ----------------------------------------------------------------
    def pose(self, motion, frame=0):
        return self.ctx.pose(motion, frame)

    def idle(self):
        return self.ctx.pose(self._idle, 0)

    def air(self):
        return self.ctx.pose(self._fall, 0)

    def squat(self):
        return self.ctx.pose(self._squat, 0)

    def j(self, name):
        return self.J[name] if isinstance(name, str) else name

    def arms(self, p, l=None, r=None, lf=None, rf=None):
        """Upper arm towards l / r, forearm towards lf / rf (default the same)."""
        J = self.J
        if l and "l_arm" in J:
            p.aim(J["l_arm"], J["l_fore"], l)
            if "l_hand" in J:
                p.aim(J["l_fore"], J["l_hand"], lf or l)
        if r and "r_arm" in J:
            p.aim(J["r_arm"], J["r_fore"], r)
            if "r_hand" in J:
                p.aim(J["r_fore"], J["r_hand"], rf or r)
        return p

    def legs(self, p, l=None, r=None, ls=None, rs=None):
        """Thigh towards l / r, shin towards ls / rs (default the same)."""
        J = self.J
        if l and "l_thigh" in J:
            p.aim(J["l_thigh"], J["l_shin"], l).aim(J["l_shin"], J["l_foot"], ls or l)
        if r and "r_thigh" in J:
            p.aim(J["r_thigh"], J["r_shin"], r).aim(J["r_shin"], J["r_foot"], rs or r)
        return p

    def kick(self, p, side, direction, knee=None):
        if side == "R":
            return self.legs(p, r=knee or direction, rs=direction)
        return self.legs(p, l=knee or direction, ls=direction)

    def punch(self, p, side, direction, elbow=None):
        if side == "R":
            return self.arms(p, r=elbow or direction, rf=direction)
        return self.arms(p, l=elbow or direction, lf=direction)

    def turn(self, p, joint, axis, deg):
        return p.turn(self.j(joint), axis, deg)

    # ---- hitboxes and scripts -----------------------------------------------------
    def hit(self, bone, damage, size, **kw):
        """A hitbox's fields for rescript. The offset defaults to none: the
        base's (along its sword, say) would carry over; so would its element
        (Pikachu's electricity on every hit): it defaults to normal, and the
        hit sound to the element's (a kick's from a leg). A script with more
        hitboxes than given repeats the last."""
        h = dict(bone=self.j(bone), damage=damage, size=size, offset=(0, 0, 0), element="normal")
        h.update(kw)
        if "sfx" not in kw:
            leg = any(self.J.get(n) == h["bone"] for n in self.J if n.startswith(("l_thigh", "l_shin", "l_foot",
                                                                                   "r_thigh", "r_shin", "r_foot")))
            h["sfx"] = anim.hit_sfx(h["element"], damage, kick=leg)
        return h

    def blade(self, damage, **kw):
        """A hitbox's fields for a weapon the base also holds there (a sword
        joint): the base's bone, offset and size kept, along its blade."""
        h = dict(damage=damage)
        h.update(kw)
        return h

    def script(self):
        return self.ctx.script()

    def has(self, motion):
        try:
            self.ctx.row(motion)
            return True
        except StopIteration:
            return False

    def family(self, *prefixes):
        """The base's motions starting with any of the prefixes, in table order
        ("SpecialHi" -> SpecialHi, SpecialHiStart, ...)."""
        out = []
        for r in self.ctx.rows:
            if r[1] and "_ACTION_" in r[1]:
                n = r[1].split("_ACTION_")[1][:-len("_figatree")]
                if n.startswith(prefixes) and n not in out:
                    out.append(n)
        return out

    # ---- moves -------------------------------------------------------------------
    def attack(self, motion, frames, keys, hits, also=(), root=False, drop=EFFECTS, flags=None):
        """One of the base's attacks: new animation, the base's script with
        its hitboxes replaced in order (rescript). Angled variants (`also`)
        the base lacks are skipped."""
        if not self.has(motion):
            return
        also = [m for m in also if self.has(m)]
        self.out.append(self.Move(motion, frames, keys, self.ctx.rescript(motion, hits, drop), flags=flags,
                                  also=also, root=root))

    def special(self, motions, frames, keys, script, root=False, flags=None):
        """The same animation and script on each of the motions (ground and
        air, start, loop and end rows of one special); ones the base lacks
        are skipped."""
        for m in motions:
            if self.has(m):
                self.out.append(self.Move(m, frames, keys, script, root=root, flags=flags))

    def taunt(self, motion, frames, keys):
        if self.has(motion):
            self.out.append(self.Move(motion, frames, keys, self.ctx.rescript(motion, [], QUIET)))

    def throw(self, motion, frames, keys, hits=()):
        """A throw: the base's script (its throw timing and release) with new
        hitboxes; the animation carries the victim through the base's
        throw bone, so keep the hand moves near the base's."""
        if self.has(motion):
            self.out.append(self.Move(motion, frames, keys, self.ctx.rescript(motion, list(hits))))

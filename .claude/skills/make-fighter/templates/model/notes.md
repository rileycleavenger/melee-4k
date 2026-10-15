# <NAME> (<ID>) - design notes

Filled in before building; kept up to date. Outside the repo.

## Source model
- File(s): `model/...` (from the user; source and credits: its readme)
- Format, triangles, materials, textures (albedo, AO, normal, others)
- Skeleton: root, hips, spine, head, arms, hands/fingers, legs, extras
  (cape, hair, hat, tail...) and spare parts hidden away (alternate
  hands, faces, weapons) - from `make_pack.sh --inspect`
- Rest pose (T-pose / A-pose), facing, up axis, units (height in units)

## Base fighter
- Chosen base and why (body shape, height, weight class, how it moves)
- Proportion differences to watch (short arms vs a big head, long legs...)
- Attribute multipliers (fighter.cfg): weight, walk, dash, jump, gravity,
  fall, air - from how the character moves in its games

## Look
- Name as on the door (title case), id, symbol stem
- Costume 1 (Nr): Melee-ified - mesh ratio, texture strength
- Costume 2 (<suffix>): the model as given
- Stock icon: the head's palette (sampled from the textures)
- Portrait pose (an attack's game pose)

## Moveset research
Sources: the character's games and media (list them: titles, its
attacks, spells, items, signature poses and lines). Every move below is
the character's own idea, none the base's.

| Slot | Move (name) | From (game / scene) | Animation idea | Hitbox idea (element, angle, reach) |
|---|---|---|---|---|
| Jab 1/2/3 | | | | |
| Dash attack | | | | |
| Forward tilt (S3 S/Hi/Lw) | | | | |
| Up tilt | | | | |
| Down tilt | | | | |
| Forward smash (S4 S/Hi/Lw) | | | | |
| Up smash | | | | |
| Down smash | | | | |
| Neutral air | | | | |
| Forward air | | | | |
| Back air | | | | |
| Up air | | | | |
| Down air | | | | |
| Neutral B | | | | |
| Side B | | | | |
| Up B (recovery) | | | | |
| Down B | | | | |
| Taunt | | | | |
| Grab, pummel, throws (F/B/Up/Down) | | | | |
| Get-up and ledge attacks | | | | |
| Voice | | | | |

## Approximations (for the report)
-

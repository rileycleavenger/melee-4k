# Custom fighters

Melee 4K can load new fighters from **fighter packs**: folders the game
reads at boot. A custom fighter is an existing fighter (its **base**)
wearing new costumes. It keeps the base's skeleton, animations and
hitboxes, has its own model, textures, menu images and name, and can have
its own special moves and attributes.

Packs are not part of this repository; they hold their makers' art. To
make one, use the `make-fighter` skill (`.claude/skills/make-fighter/`).
It walks through every step and every tool.

## Installing a pack

Put the pack's folder (for example `Ge/`, with `fighter.cfg` in it) into
`fighters/`, in either place:

- **With the game:** `Melee4K.app/Contents/Resources/fighters/` on a Mac
  (SDL's base path inside the app), or `fighters\` next to `melee.exe` on
  Windows. This is how a release ships its fighters, and it is used
  instead of the user folder when it exists.
- **In your user folder:** the same place as your settings
  (`~/Library/Application Support/melee-pc/fighters/` on a Mac,
  `%APPDATA%\melee-pc\fighters\` on Windows).

For testing, `MELEE_FIGHTERS_DIR=/path/to/packs` is used instead of both.
Up to 4 packs load, each with up to 8 costumes. The log says what loaded
(`fighters: Ge "GENO" on Mr, 5 costume(s)`, and `fighters: Ge is Mr
costumes 5-9`).

## In the game

- **Character select.** Each custom fighter gets an icon to the right of
  the bottom row (one at present). Picking it shows its portrait and name;
  X/Y cycle its costumes. In team battles it wears the costumes its
  `teams` line names. Random never picks a custom fighter.
- **Matches.** It plays as its base. If the pack names compiled-in `moves`,
  it uses them instead of the base's specials, and the `fighter.cfg`
  attribute multipliers change its weight, speeds, jumps and falling. Other
  players of the same base fighter keep the base's moves.
- **HUD and results.** It shows its own stock icon, and on the results screen
  its own winner banner and name plate. The announcer says nothing for it.
- **Online.** Two players with the same game version and the same packs
  (the same bytes, compared by hash when they join) can both use custom
  fighters. Otherwise, from v1.1 up, the session is **base fighters only**:
  custom icons are hidden and the lobby says so. v2 can still play v1.1.0
  this way. See [online.md](online.md).

## Pack format

```
fighters/<id>/
  fighter.cfg            what the fighter is
  Pl<id><costume>.dat    one model file per costume (PlGeNr.dat, PlGeRe.dat ...)
  Pl<id>Ui.dat           menu images: icon, portraits, stock icons, results name
  textures/              HD replacements for the costume files' textures
```

`fighter.cfg` has one `key value` per line, with `#` comments:

| Key | Example | Meaning |
|---|---|---|
| `id` | `Ge` | Two letters, unused by the disc's `Pl??.dat` and by other packs |
| `name` | `"GENO"` | Shown in menus; at most 31 characters |
| `base` | `Mr` | The base fighter's two-letter code |
| `symbol` | `PlyGeno5K` | The model's joint symbol stem. The game looks up `<stem>_Share_joint` for costume Nr and `<stem><costume>_Share_joint` for the others, as on the disc |
| `costumes` | `Nr Re Gr Bk Wh` | Costume file suffixes; the first is the default; at most 8 |
| `teams` | `Re Nr Gr` | Optional: the red, blue and green team costumes (default `Re Bu Gr`) |
| `moves` | `Geno` | Optional: special moves compiled into the game (`src/melee/ft/ftcustom.h`) |
| `weight` `walk` `dash` `jump` `gravity` `fall` `air` | `0.94` | Optional: multipliers on the base's attributes, one per line, between 0.1 and 10 |

Everything in the pack except `textures/` is simulation or menu data that
online play hashes: `fighter.cfg` and every `.dat`.

## How it works

- **Pack loading** (`src/pc/fighter_pack.c`). This parses each
  `fighter.cfg`. It then puts every pack file on the game's virtual disc
  (aurora DVD overlay files), so the game opens `/PlGeNr.dat` as though
  it were on the disc. It also hashes the packs in id order.
- **Costumes** (`src/melee/ft/ftcustom.c`). This extends the base fighter's
  costume list and costume-string table with the pack's costumes (Geno is
  Mario's costumes 5 and up). The fighter's kind stays the base's, so every
  per-kind table and check applies unchanged. Per-costume disc tables (for
  example the costume-dependent parts of `PlMr.dat`) are read with the base's
  costume 0 (`ftCustom_DataCostume`).
- **Model files** (`tools/fighter/build_dat.py`). The base costume file is
  copied, keeping its skeleton, bind pose and eye material animation, and
  the new meshes are appended. The base data file shows and hides models by
  index, so the new model fills the slots it shows (normal and low detail),
  and the eye slots keep the base's eye material.
- **Moves** (`ftCustom_Init`). This replaces the base kind's eight special
  entries with shims. Each shim calls the custom fighter's function if the
  fighter wears a costume whose pack has moves, and otherwise the base's.
  Attributes are multiplied where the game copies them from the data file
  (`ftchangeparam.c`).
- **Menus** (`mncharsel.c`, `ifstock.c`, `gmresultplayer.c`). These load
  `Pl<id>Ui.dat` into the screen's heap and point the right texture objects
  at its images.
- **Online** (`src/pc/online.c`). The JOIN message carries the content hash.
  A mismatch puts the session in base-only mode, and v1.1.0 peers are
  answered in v1.1.0's own protocol.

## Tools

The `make-fighter` skill's `tools.md` documents each tool:

| Tool | What |
|---|---|
| `tools/fighter/export_base.py` | Exports a base fighter from your disc (skeleton, meshes, template) |
| `tools/fighter/blender_*.py` | Blender steps: import the base, model primitives, check renders, export, menu renders |
| `tools/fighter/build_dat.py` | Writes a costume file |
| `tools/fighter/build_ui.py` | Writes the menu images file |
| `tools/fighter/validate.py` | Checks a pack |
| `tools/fighter/preview.py` | Plays CPU matches with the fighter and makes a screenshot sheet |
| `tools/test_fighter_pack.c`, `tools/test_fighter_tools.py` | Unit tests |

# Releases and Setup

How a player installs Melee 4K, and how a release is put together.

## What the player gets

One zip per platform, unzipped to a `Melee4K/` folder:

```
Melee4K/
  Melee4K.app            (macOS)   |  Melee4K/melee.exe + DLLs (Windows)
  PUT-YOUR-ROM-HERE/     the player drops their disc image here
  Setup.command          (macOS)   |  Setup.exe (Windows)
  .setup/melee4k-setup   (macOS: the frozen Setup that Setup.command runs)
  README.txt             platforms/release/README.txt
```

They drop the disc image in `PUT-YOUR-ROM-HERE` and run Setup once. Setup
(`tools/setup/setup.py`, frozen with PyInstaller):

1. finds the one disc image in `PUT-YOUR-ROM-HERE` (refuses zips, NKit images
   and more than one image, each with a one-sentence fix);
2. runs `melee --verify-disc <image>`: nod + Redump SHA-1, as in the
   launcher; exit 0 verified, 1 playable but not USA 1.02, 2 unusable. A
   usable image is saved as the disc to boot in `launcher.cfg`;
3. downloads the HD texture pack listed in the manifest baked in at build
   time (`MELEE4K_MANIFEST`), resuming partial parts and checking each part's
   size and SHA-256, and unpacks it into the user `textures/` folder,
   replacing only the packs it ships;
4. clears the download quarantine (macOS; `Setup.command` already did, since
   opening it was the player's approval) or adds a desktop shortcut
   (Windows);
5. starts the game.

Every step is safe to repeat: a finished texture install is recorded in
`textures/.melee4k-pack` and skipped next time. Failures print one sentence
saying what to do; the full log is `setup-log.txt` next to Setup.

`MELEE_USER_DIR` points both the game and Setup at another settings folder,
which is how Setup is tested without touching a real install:

```sh
MELEE_USER_DIR=/tmp/m4k-user python3 tools/setup/setup.py --root RELEASE_DIR \
    --manifest /path/to/manifest.json --no-launch
```

## The texture pack

`tools/setup/make_pack.py TEXTURES OUT --version V --url-base URL` zips a
folder laid out the way the game reads it (`packs/05-css-original`,
`packs/10-community`, `stocks/<style>`) into parts under GitHub's 2 GB
release-asset limit and writes `manifest.json`. Parts are stored, not
deflated: BC7 DDS barely compresses.

Build the folder with the texture tools (see `docs/melee-4k.md`): merge the
community packs and the AI gap-fill, `encode.py` them into
`packs/10-community`, and `original_pack.py` for `packs/05-css-original`.

The pack is hosted as release assets, not on Google Drive (its large-file
warning page and download quotas break scripted downloads). The textures
are derived from Nintendo's artwork, so publishing them is the owner's
decision; it has not been made yet.

## Building a release

macOS (on a Mac, with the repo `.venv` holding pyinstaller):

```sh
MELEE4K_MANIFEST=https://.../manifest.json tools/package_release_macos.sh
# -> dist/Melee4K-macOS-<arch>.zip
```

Windows: `Setup.exe` has to be frozen on Windows (PyInstaller does not
cross-compile), so the Windows release is assembled by CI on a Windows
runner. CI (`.github/workflows/build.yml`) runs on `v*` tags and on manual
dispatch only.

The app is not signed with an Apple Developer ID or a Windows certificate:
README.txt covers the one-time right-click > Open (macOS) and
More info > Run anyway (Windows).

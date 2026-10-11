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
3. downloads the HD texture pack listed in the newest release of
   [melee-4k-textures](https://github.com/rileycleavenger/melee-4k-textures)
   (`MELEE4K_MANIFEST` overrides it for tests), resuming partial parts and checking each part's
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

## The macOS one-line installer

`install.sh` (run as `curl -fsSL .../install.sh | bash`) fetches the newest
release zip with curl, so nothing carries the quarantine flag and Gatekeeper
never shows its "could not verify" dialog (macOS 15 removed right-click >
Open for unsigned downloads). It installs to `~/Melee4K`, waits for a disc in
`PUT-YOUR-ROM-HERE`, runs Setup, and moves the app to
`~/Applications/Melee 4K.app` (a symlink stays in `~/Melee4K` for Setup).
Re-running it updates in place. `MELEE4K_DIR` and `MELEE4K_NO_LAUNCH` exist
for tests.

## The texture pack

`tools/setup/make_pack.py TEXTURES OUT --version V --url-base URL` zips a
folder laid out the way the game reads it (`packs/10-community`,
`packs/20-ai-fill`, `stocks/<style>`) into parts under GitHub's 2 GB
release-asset limit and writes `manifest.json`. Parts are stored, not
deflated: BC7 DDS barely compresses.

Build the folder with the texture tools (see `docs/melee-4k.md`):
`choices_page.py` and `make_choices.py` lay out the community and AI
layers, `restore_alpha.py` and `encode.py` turn them into
`packs/10-community` and `packs/20-ai-fill`.

The pack is hosted as release assets of the public
[melee-4k-textures](https://github.com/rileycleavenger/melee-4k-textures)
repository, not on Google Drive (its large-file warning page and download
quotas break scripted downloads). Setup reads
`releases/latest/download/manifest.json`, so publishing a new pack release
updates every install the next time Setup runs.

## Building a release

macOS (on a Mac, with the repo `.venv` holding pyinstaller):

```sh
tools/package_release_macos.sh
# -> dist/Melee4K-macOS-<arch>.zip
```

Windows: `Setup.exe` has to be frozen on Windows (PyInstaller does not
cross-compile), so the Windows release is assembled by CI on a Windows
runner: run the "Release (Windows)" workflow (`release-windows.yml`) by
hand; its artifact is `Melee4K-Windows-x86_64.zip`. Releases on the game
repository are published by hand with both zips (the full platform matrix in
`build.yml` is manual too, so a tag never publishes upstream's builds).

The app is not signed with an Apple Developer ID or a Windows certificate:
README.txt covers the one-time right-click > Open (macOS) and
More info > Run anyway (Windows).

#!/usr/bin/env python3
"""Melee 4K Setup: put your disc image in PUT-YOUR-ROM-HERE, run this, play.

    Setup.command (macOS) / Setup.exe (Windows), or: python3 tools/setup/setup.py
        [--root DIR] [--manifest URL|FILE] [--no-launch] [--skip-textures]

Steps, each safe to repeat (a second run resumes or skips what is done):
  1. find the disc image in ROOT/PUT-YOUR-ROM-HERE
  2. check it with `melee --verify-disc`, which also saves it as the disc to boot
  3. download the HD texture pack listed in the manifest (resumable, every part
     checked against its SHA-256) and unpack it into the game's textures folder
  4. on macOS, clear the download quarantine from the app
  5. start the game

Standard library only, so the frozen Setup stays small. Every failure prints
one sentence saying what to do next; the full log is ROOT/setup-log.txt.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile

ROM_DIR = "PUT-YOUR-ROM-HERE"
DISC_EXTS = (".iso", ".gcm", ".ciso", ".rvz", ".gcz", ".wbfs")
# Set at release time to the published texture manifest (see docs/setup.md).
DEFAULT_MANIFEST = os.environ.get("MELEE4K_MANIFEST", "")
PACK_VERSION_FILE = ".melee4k-pack"


class SetupError(Exception):
    """A failure the player can fix; the message says how, in one sentence."""


log_file = None


def say(msg=""):
    print(msg, flush=True)
    if log_file:
        log_file.write(msg + "\n")
        log_file.flush()


def log(msg):
    if log_file:
        log_file.write(msg + "\n")
        log_file.flush()


def app_root():
    """The folder Setup lives in (the unzipped release folder)."""
    if getattr(sys, "frozen", False):
        here = os.path.dirname(sys.executable)
        # A macOS one-file build runs from inside Setup.app/Contents/MacOS.
        marker = ".app" + os.sep + "Contents" + os.sep + "MacOS"
        if marker in here:
            here = here[:here.index(marker)]
            here = os.path.dirname(here)
        return here
    return os.getcwd()


def user_dir():
    """Where the game keeps settings and textures: MELEE_USER_DIR, else
    SDL_GetPrefPath(NULL, "melee-pc") (pc_user_dir in src/pc/main.c)."""
    if os.environ.get("MELEE_USER_DIR"):
        return os.environ["MELEE_USER_DIR"]
    system = platform.system()
    if system == "Darwin":
        return os.path.expanduser("~/Library/Application Support/melee-pc")
    if system == "Windows":
        return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "melee-pc")
    base = os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share"))
    return os.path.join(base, "melee-pc")


def find_game(root):
    candidates = {
        "Darwin": ["Melee4K.app/Contents/MacOS/melee", "build/macos/melee"],
        "Windows": ["Melee4K/melee.exe", "melee.exe", "build/windows/melee.exe"],
    }.get(platform.system(), ["melee", "build/linux/melee"])
    for rel in candidates:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            return path
    raise SetupError("The game is missing from this folder; download the release zip again "
                     "and unzip the whole thing.")


def find_disc(root):
    folder = os.path.join(root, ROM_DIR)
    os.makedirs(folder, exist_ok=True)
    found = []
    for name in sorted(os.listdir(folder)):
        lower = name.lower()
        if lower.endswith(".nkit.iso") or lower.endswith(".nkit.gcz"):
            raise SetupError(f"{name} is an NKit image, which the game cannot read; convert it "
                             "back to a full .iso with NKit, or use a .iso/.rvz dump.")
        if lower.endswith(DISC_EXTS):
            found.append(os.path.join(folder, name))
        elif lower.endswith((".zip", ".7z", ".rar")):
            raise SetupError(f"{name} is compressed; unzip it so the .iso (or .rvz) itself is "
                             f"in the {ROM_DIR} folder, then run Setup again.")
    if not found:
        raise SetupError(f"Put your Melee disc image (.iso, .gcm, .ciso or .rvz) in the {ROM_DIR} "
                         "folder, then run Setup again.")
    if len(found) > 1:
        names = ", ".join(os.path.basename(f) for f in found)
        raise SetupError(f"There is more than one disc image in {ROM_DIR} ({names}); keep only "
                         "your Melee disc there, then run Setup again.")
    return found[0]


def verify_disc(game, disc):
    proc = subprocess.Popen([game, "--verify-disc", disc], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    result = ""
    last = -1
    for line in proc.stdout:
        line = line.strip()
        log(line)
        if line.startswith("progress "):
            pct = int(line.split()[1])
            if pct // 10 != last // 10:
                say(f"    {pct}%")
            last = pct
        elif line.startswith(("ok:", "mismatch:", "error:")):
            result = line
    rc = proc.wait()
    if rc == 0:
        say("    Disc verified: Super Smash Bros. Melee (USA) 1.02.")
    elif rc == 1:
        say("    " + result.split(":", 1)[1].strip())
        say("    This disc is not the verified USA 1.02 image; it may still run, but only "
            "USA 1.02 is supported.")
    else:
        detail = result.split(":", 1)[1].strip() if ":" in result else "unknown error"
        raise SetupError(f"The disc image could not be read ({detail}); use a full dump of "
                         "Super Smash Bros. Melee (USA) 1.02 and run Setup again.")


def fetch(url, dest):
    """Download url to dest, resuming a partial file."""
    if not url.startswith(("http://", "https://")):
        shutil.copyfile(url[7:] if url.startswith("file://") else url, dest)
        return
    have = os.path.getsize(dest) if os.path.exists(dest) else 0
    req = urllib.request.Request(url, headers={"User-Agent": "Melee4K-Setup"})
    if have:
        req.add_header("Range", f"bytes={have}-")
    with urllib.request.urlopen(req, timeout=60) as resp:
        mode = "ab" if have and resp.status == 206 else "wb"
        total = int(resp.headers.get("Content-Length") or 0) + (have if mode == "ab" else 0)
        done = have if mode == "ab" else 0
        start, shown = time.time(), 0.0
        with open(dest, mode) as out:
            while True:
                chunk = resp.read(1 << 20)
                if not chunk:
                    break
                out.write(chunk)
                done += len(chunk)
                if total and time.time() - shown > 2:
                    rate = (done - have) / max(1e-3, time.time() - start)
                    eta = (total - done) / max(1.0, rate)
                    say(f"    {done * 100 // total}%  ({done >> 20} / {total >> 20} MB, "
                        f"about {int(eta // 60)} min {int(eta % 60)} s left)")
                    shown = time.time()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_manifest(where):
    try:
        if where.startswith(("http://", "https://")):
            with urllib.request.urlopen(where, timeout=60) as resp:
                return json.load(resp)
        with open(where[7:] if where.startswith("file://") else where) as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        log(f"manifest {where}: {e}")
        raise SetupError("Could not download the HD texture list; check your internet "
                         "connection and run Setup again.")


def install_textures(manifest_src, root):
    textures = os.path.join(user_dir(), "textures")
    manifest = load_manifest(manifest_src)
    version = manifest["version"]
    if not manifest.get("parts"):
        raise SetupError("The HD texture list is empty; run Setup again later.")
    stamp = os.path.join(textures, PACK_VERSION_FILE)
    if os.path.exists(stamp) and open(stamp).read().strip() == version:
        say(f"    HD textures {version} are already installed.")
        return
    cache = os.path.join(root, "downloads")
    os.makedirs(cache, exist_ok=True)
    parts = manifest["parts"]
    for i, part in enumerate(parts, 1):
        dest = os.path.join(cache, part["name"])
        if os.path.exists(dest) and os.path.getsize(dest) == part["size"] \
                and sha256(dest) == part["sha256"]:
            say(f"    Part {i} of {len(parts)} already downloaded.")
            continue
        say(f"    Downloading part {i} of {len(parts)} ({part['size'] >> 20} MB)...")
        for attempt in range(3):
            try:
                fetch(part["url"], dest)
            except OSError as e:
                log(f"download {part['url']}: {e}")
                if attempt == 2:
                    raise SetupError("The HD texture download stopped; check your internet "
                                     "connection and run Setup again (it picks up where it "
                                     "left off).")
                time.sleep(3)
                continue
            if os.path.getsize(dest) == part["size"] and sha256(dest) == part["sha256"]:
                break
            log(f"{dest}: size or checksum mismatch, downloading again")
            os.remove(dest)
        else:
            raise SetupError("The HD texture download was damaged three times; run Setup "
                             "again later.")
    say("    Unpacking...")
    staging = textures + ".new"
    shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(staging)
    for part in parts:
        with zipfile.ZipFile(os.path.join(cache, part["name"])) as z:
            for member in z.namelist():
                target = os.path.realpath(os.path.join(staging, member))
                if not target.startswith(os.path.realpath(staging) + os.sep):
                    raise SetupError("The HD texture download is not valid; run Setup again "
                                     "later.")
            z.extractall(staging)
    # Replace only what the pack owns; anything else the player added stays.
    os.makedirs(textures, exist_ok=True)
    for entry in os.listdir(staging):
        dst = os.path.join(textures, entry)
        if entry == "packs" and os.path.isdir(dst):
            for pack in os.listdir(os.path.join(staging, "packs")):
                shutil.rmtree(os.path.join(dst, pack), ignore_errors=True)
                shutil.move(os.path.join(staging, "packs", pack), os.path.join(dst, pack))
            continue
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        elif os.path.exists(dst):
            os.remove(dst)
        shutil.move(os.path.join(staging, entry), dst)
    shutil.rmtree(staging, ignore_errors=True)
    with open(stamp, "w") as f:
        f.write(version + "\n")
    shutil.rmtree(cache, ignore_errors=True)
    say(f"    HD textures {version} installed.")


def clear_quarantine(root):
    if platform.system() != "Darwin":
        return
    app = os.path.join(root, "Melee4K.app")
    if os.path.isdir(app):
        subprocess.run(["xattr", "-dr", "com.apple.quarantine", app],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def desktop_shortcut(game):
    """Windows: a "Melee 4K" shortcut on the desktop (README step 4)."""
    if platform.system() != "Windows":
        return
    desktop = os.path.join(os.environ.get("USERPROFILE", os.path.expanduser("~")), "Desktop")
    link = os.path.join(desktop, "Melee 4K.lnk")
    script = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:M4K_LINK); "
              "$s.TargetPath = $env:M4K_GAME; $s.WorkingDirectory = $env:M4K_DIR; "
              "$s.IconLocation = $env:M4K_GAME; $s.Save()")
    env = dict(os.environ, M4K_LINK=link, M4K_GAME=game, M4K_DIR=os.path.dirname(game))
    try:
        subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say("    Added a \"Melee 4K\" shortcut to your desktop.")
    except (OSError, subprocess.CalledProcessError) as e:
        log(f"shortcut: {e}")  # optional; the game still runs from its folder


def launch(root, game):
    if platform.system() == "Darwin" and os.path.isdir(os.path.join(root, "Melee4K.app")):
        subprocess.Popen(["open", os.path.join(root, "Melee4K.app")])
    else:
        subprocess.Popen([game], cwd=os.path.dirname(game))


def main():
    global log_file
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", help="release folder (default: Setup's own folder)")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST, help="texture manifest URL or file")
    ap.add_argument("--no-launch", action="store_true")
    ap.add_argument("--skip-textures", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.root or app_root())
    log_file = open(os.path.join(root, "setup-log.txt"), "a")
    log(f"--- Setup {time.strftime('%Y-%m-%d %H:%M:%S')} on {platform.platform()}")
    try:
        say("Melee 4K Setup")
        say("")
        say("[1/4] Finding your disc image...")
        disc = find_disc(root)
        game = find_game(root)
        say(f"    {os.path.basename(disc)}")
        say("[2/4] Checking the disc...")
        verify_disc(game, disc)
        say("[3/4] HD textures...")
        if args.skip_textures:
            say("    Skipped.")
        elif not args.manifest:
            say("    No texture download is configured in this build; the game will use its "
                "original textures.")
        else:
            install_textures(args.manifest, root)
        say("[4/4] Starting the game...")
        clear_quarantine(root)
        desktop_shortcut(game)
        if not args.no_launch:
            launch(root, game)
        say("")
        say("Done. Next time, just open " +
            ("Melee4K.app." if platform.system() == "Darwin"
             else "the Melee 4K shortcut on your desktop."))
        return 0
    except SetupError as e:
        say("")
        say(str(e))
        log("failed: " + str(e))
        return 1
    except Exception as e:  # anything unexpected still ends in a readable line
        import traceback
        log(traceback.format_exc())
        say("")
        say(f"Setup hit an unexpected problem ({e}); the details are in setup-log.txt, "
            "please attach it to a bug report.")
        return 1
    finally:
        if getattr(sys, "frozen", False) and platform.system() == "Windows":
            input("\nPress Enter to close this window.")


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Melee 4K Setup: put your disc image in PUT-YOUR-ROM-HERE, run this, play.
(The game it sets up is product.env's: its name, release repo and files.)

    Setup.command (macOS) / Setup.exe (Windows), or: python3 tools/setup/setup.py
        [--root DIR] [--manifest URL|FILE] [--no-launch] [--skip-textures]
        [--update [--tag vX.Y.Z] [--wait-pid PID]]

Steps, each safe to repeat (a second run resumes or skips what is done):
  1. find the disc image in ROOT/PUT-YOUR-ROM-HERE
  2. check it with `melee --verify-disc`, which also saves it as the disc to boot
  3. download the HD texture pack listed in the manifest (resumable, every part
     checked against its SHA-256) and unpack it into the game's textures folder
  4. on macOS, clear the download quarantine from the app
  5. start the game

--update is what the game's Check for Updates runs (src/pc/updater.cpp): wait
for the game to close, download the newest release (or --tag) for this
platform, replace the game and Setup in place (the disc, saves, settings and
textures stay), then do the steps above without checking the disc again and
start the game.

Standard library plus truststore/certifi (bundled into the frozen Setup), so
it stays small. Every failure prints
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
import urllib.error
import urllib.request
import zipfile

ROM_DIR = "PUT-YOUR-ROM-HERE"
DISC_EXTS = (".iso", ".gcm", ".ciso", ".rvz", ".gcz", ".wbfs")
# The newest published texture pack (docs/setup.md); its manifest pins the
# part URLs of that release. MELEE4K_MANIFEST overrides it for testing.
DEFAULT_MANIFEST = os.environ.get(
    "MELEE4K_MANIFEST",
    "https://github.com/rileycleavenger/melee-4k-textures/releases/latest/download/manifest.json")
PACK_VERSION_FILE = ".melee4k-pack"  # the textures are shared by every product


def read_product():
    """The product (product.env at the repo root: name, release repo, file
    names); a frozen Setup carries the file (--add-data)."""
    here = getattr(sys, "_MEIPASS", None) or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..")
    values = {}
    with open(os.path.join(here, "product.env"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key] = value.strip('"')
    return values


PRODUCT = read_product()
NAME = PRODUCT["PRODUCT_NAME"]  # "Melee 4K"
FILE = PRODUCT["PRODUCT_FILE"]  # "Melee4K": the app, its folder, the zips
SLUG = PRODUCT["PRODUCT_SLUG"]  # "melee4k": Setup's program, its files in user_dir()
REPO = PRODUCT["PRODUCT_REPO"]
RELEASE_ASSET = {"Darwin": f"{FILE}-macOS-arm64.zip", "Windows": f"{FILE}-Windows-x86_64.zip"}
# Where the game looks for Setup when the player asks for an update: this
# install's folder, written by every Setup run (src/pc/updater.cpp).
INSTALL_ROOT_FILE = f"{SLUG}-install-root.txt"


def ssl_contexts():
    """HTTPS verification methods, most portable first. A frozen Setup
    carries its own OpenSSL, which on Windows does not see the system store
    (CERTIFICATE_VERIFY_FAILED on github.com); truststore's Windows path
    crashed inside the frozen Setup.exe ("access violation"). certifi's
    bundle is a plain file and works everywhere, so it goes first; the OS
    verifier and Python's default are fallbacks."""
    import ssl
    out = []
    try:
        import certifi
        out.append(("certifi", lambda: ssl.create_default_context(cafile=certifi.where())))
    except ImportError:
        pass
    try:
        import truststore
        out.append(("system", lambda: truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)))
    except ImportError:
        pass
    out.append(("python default", ssl.create_default_context))
    return out


_working_context = None


def urlopen(req, timeout):
    """urllib.request.urlopen with the first verification method that works,
    remembered for the rest of the run."""
    global _working_context
    if _working_context is not None:
        return urllib.request.urlopen(req, timeout=timeout, context=_working_context)
    last = None
    for name, make in ssl_contexts():
        try:
            ctx = make()
            resp = urllib.request.urlopen(req, timeout=timeout, context=ctx)
        except Exception as e:  # a broken method must not end Setup
            log(f"https via {name}: {e}")
            last = e
            if isinstance(e, urllib.error.HTTPError):
                raise
            continue
        log(f"https via {name}: ok")
        _working_context = ctx
        return resp
    raise last if isinstance(last, OSError) else OSError(str(last))


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
        "Darwin": [f"{FILE}.app/Contents/MacOS/melee", "build/macos/melee"],
        "Windows": [f"{FILE}/melee.exe", "melee.exe", "build/windows/melee.exe"],
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
    req = urllib.request.Request(url, headers={"User-Agent": f"{FILE}-Setup"})
    if have:
        req.add_header("Range", f"bytes={have}-")
    with urlopen(req, timeout=60) as resp:
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
            req = urllib.request.Request(where, headers={"User-Agent": f"{FILE}-Setup"})
            with urlopen(req, timeout=60) as resp:
                return json.load(resp)
        with open(where[7:] if where.startswith("file://") else where) as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        log(f"manifest {where}: {e}")
        if "CERTIFICATE_VERIFY_FAILED" in str(e) or "access violation" in str(e):
            raise SetupError("Could not make a secure connection to GitHub to download the HD "
                             "textures; please report this with setup-log.txt attached.")
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
    # Packs an earlier release installed and this one dropped go too.
    os.makedirs(textures, exist_ok=True)
    for pack in manifest.get("retired_packs", []):
        if pack and os.sep not in pack and "/" not in pack and not pack.startswith("."):
            shutil.rmtree(os.path.join(textures, "packs", pack), ignore_errors=True)
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
    app = os.path.join(root, f"{FILE}.app")
    if os.path.isdir(app):
        subprocess.run(["xattr", "-dr", "com.apple.quarantine", app],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def desktop_shortcut(game):
    """Windows: a NAME shortcut on the desktop (README step 4)."""
    if platform.system() != "Windows":
        return
    # The real Desktop folder: OneDrive moves it out of %USERPROFILE%.
    script = ("$d = [Environment]::GetFolderPath('Desktop'); "
              "$s = (New-Object -ComObject WScript.Shell).CreateShortcut("
              "(Join-Path $d $env:M4K_LINK)); "
              "$s.TargetPath = $env:M4K_GAME; $s.WorkingDirectory = $env:M4K_DIR; "
              "$s.IconLocation = $env:M4K_GAME; $s.Save()")
    env = dict(os.environ, M4K_GAME=game, M4K_DIR=os.path.dirname(game), M4K_LINK=f"{NAME}.lnk")
    try:
        subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say(f"    Added a \"{NAME}\" shortcut to your desktop.")
    except (OSError, subprocess.CalledProcessError) as e:
        log(f"shortcut: {e}")  # optional; the game still runs from its folder


FIREWALL_RULE = NAME


def firewall_rule(game):
    """Windows: let online play's UDP traffic in (host lobby, LAN discovery).

    Without a rule, Windows Firewall drops it unless its first-run prompt was
    answered, which a fullscreen game easily hides, so a friend's call never
    arrives. Adding a rule needs administrator rights: one UAC prompt, once
    (an existing rule for this program is left alone)."""
    if platform.system() != "Windows":
        return
    try:
        shown = subprocess.run(
            ["netsh", "advfirewall", "firewall", "show", "rule", f"name={FIREWALL_RULE}",
             "verbose"], capture_output=True, text=True)
        if shown.returncode == 0 and game.lower() in shown.stdout.lower():
            return
        say(f"    Allowing {NAME} through Windows Firewall for online play "
            "(Windows will ask for permission)...")
        args = (f'advfirewall firewall add rule name="{FIREWALL_RULE}" dir=in action=allow '
                f'program="{game}" protocol=UDP profile=any enable=yes')
        script = ("Start-Process -FilePath netsh -Verb RunAs -Wait -WindowStyle Hidden "
                  "-ArgumentList $env:M4K_ARGS")
        subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                       env=dict(os.environ, M4K_ARGS=args), check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError) as e:
        # Declined or failed: online still works on the networks where the
        # game's own firewall prompt was accepted.
        log(f"firewall rule: {e}")


def remember_root(root):
    """Tell the game where this install is, for Check for Updates."""
    try:
        os.makedirs(user_dir(), exist_ok=True)
        with open(os.path.join(user_dir(), INSTALL_ROOT_FILE), "w") as f:
            f.write(root + "\n")
    except OSError as e:
        log(f"install root: {e}")


def mac_desktop_link(root):
    """macOS: the Desktop shortcut install.sh makes, made again on update."""
    if platform.system() != "Darwin":
        return
    app = os.path.realpath(os.path.join(root, f"{FILE}.app"))
    desk = os.path.expanduser(f"~/Desktop/{NAME}.app")
    if os.path.isdir(app) and (os.path.islink(desk) or not os.path.exists(desk)):
        try:
            if os.path.islink(desk):
                os.remove(desk)
            os.symlink(app, desk)
        except OSError as e:
            log(f"desktop link: {e}")


def wait_for_exit(pid, seconds=30):
    """The game started us and is closing: wait until it has."""
    end = time.time() + seconds
    while time.time() < end:
        if platform.system() == "Windows":
            out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"],
                                 capture_output=True, text=True).stdout
            if str(pid) not in out:
                return
        else:
            try:
                os.kill(pid, 0)
            except OSError:
                return
        time.sleep(0.5)
    raise SetupError(f"{NAME} did not close; close it and choose Check for Updates again.")


def release_asset(tag):
    """(tag, URL) of this platform's zip in the newest release, or in `tag`."""
    name = RELEASE_ASSET.get(platform.system())
    if name is None:
        raise SetupError("Updating is supported on macOS and Windows only.")
    api = (f"https://api.github.com/repos/{REPO}/releases/tags/{tag}" if tag
           else f"https://api.github.com/repos/{REPO}/releases/latest")
    try:
        req = urllib.request.Request(api, headers={"User-Agent": f"{FILE}-Setup",
                                                   "Accept": "application/vnd.github+json"})
        with urlopen(req, timeout=60) as resp:
            release = json.load(resp)
    except (OSError, ValueError) as e:
        log(f"release {api}: {e}")
        raise SetupError("Could not reach GitHub to download the update; check your internet "
                         "connection and try again.")
    for asset in release.get("assets", []):
        if asset.get("name") == name:
            return release.get("tag_name", "?"), asset["browser_download_url"]
    raise SetupError(f"The newest release has no {name}; try again later.")


def replace_tree(src, dst):
    """dst becomes a copy of src (an app bundle or the game folder). The copy
    is made next to dst first and swapped in, so a failure halfway leaves
    the old version runnable."""
    staged, old = dst + ".new", dst + ".old"
    for leftover in (staged, old):
        shutil.rmtree(leftover, ignore_errors=True)
    if platform.system() == "Darwin":
        subprocess.run(["ditto", src, staged], check=True)  # keeps the signature
    else:
        shutil.copytree(src, staged, symlinks=True)
    if os.path.exists(dst):
        os.replace(dst, old)
    os.replace(staged, dst)
    shutil.rmtree(old, ignore_errors=True)


def replace_file(src, dst):
    """Copy over dst, which may be the running Setup: Windows cannot write a
    running program, but it can rename it out of the way."""
    staged, old = dst + ".new", dst + ".old"
    shutil.copy2(src, staged)
    if os.path.exists(dst):
        if os.path.exists(old):
            try:
                os.remove(old)
            except OSError:
                pass  # last update's, still running: harmless
        if platform.system() == "Windows":
            os.replace(dst, old)
    os.replace(staged, dst)


def update(root, tag):
    """Download the release and put it over this install."""
    version, url = release_asset(tag)
    say(f"    Downloading {NAME} {version}...")
    work = os.path.join(root, "downloads", "update")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    zip_path = os.path.join(work, "release.zip")
    for attempt in range(3):
        try:
            fetch(url, zip_path)
            with zipfile.ZipFile(zip_path) as z:
                bad = z.testzip()
                if bad is not None:
                    raise zipfile.BadZipFile(bad)
                for member in z.namelist():
                    target = os.path.realpath(os.path.join(work, member))
                    if not target.startswith(os.path.realpath(work) + os.sep):
                        raise SetupError("The update download is not valid; try again later.")
            break
        except (OSError, zipfile.BadZipFile) as e:
            log(f"update download {url}: {e}")
            if os.path.exists(zip_path):
                os.remove(zip_path)
            if attempt == 2:
                raise SetupError("The update download stopped; check your internet connection "
                                 "and try again.")
            time.sleep(3)
    say("    Installing...")
    if platform.system() == "Darwin":
        # ditto keeps the app's signature and permissions as packaged.
        subprocess.run(["ditto", "-x", "-k", zip_path, os.path.join(work, "new")], check=True)
    else:
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(os.path.join(work, "new"))
    new = os.path.join(work, "new", FILE)
    if not os.path.isdir(new):
        raise SetupError("The update download is not valid; try again later.")
    if platform.system() == "Darwin":
        # install.sh moves the app to ~/Applications and leaves a link here.
        app = os.path.realpath(os.path.join(root, f"{FILE}.app"))
        replace_tree(os.path.join(new, f"{FILE}.app"), app)
        replace_file(os.path.join(new, ".setup", f"{SLUG}-setup"),
                     os.path.join(root, ".setup", f"{SLUG}-setup"))
        os.chmod(os.path.join(root, ".setup", f"{SLUG}-setup"), 0o755)
        subprocess.run(["xattr", "-dr", "com.apple.quarantine", app],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        replace_tree(os.path.join(new, FILE), os.path.join(root, FILE))
        replace_file(os.path.join(new, "Setup.exe"), os.path.join(root, "Setup.exe"))
    for extra in ("README.txt", "Setup.command"):
        if os.path.exists(os.path.join(new, extra)):
            shutil.copy2(os.path.join(new, extra), os.path.join(root, extra))
    shutil.rmtree(work, ignore_errors=True)
    try:
        os.rmdir(os.path.join(root, "downloads"))  # only when nothing else is in it
    except OSError:
        pass
    say(f"    {NAME} {version} installed.")


def launch(root, game):
    if platform.system() == "Darwin" and os.path.isdir(os.path.join(root, f"{FILE}.app")):
        subprocess.Popen(["open", os.path.join(root, f"{FILE}.app")])
    else:
        subprocess.Popen([game], cwd=os.path.dirname(game))


def run_update(root, args):
    """Check for Updates, from the game: replace the game, keep everything
    else, and start it again. A failure leaves the old version in place
    where it can, and says so."""
    say(f"{NAME} update")
    say("")
    if args.wait_pid:
        say(f"[1/3] Waiting for {NAME} to close...")
        wait_for_exit(args.wait_pid)
    else:
        say("[1/3] Starting...")
    say("[2/3] Downloading the update...")
    update(root, args.tag)
    say("[3/3] Finishing...")
    game = find_game(root)
    clear_quarantine(root)
    desktop_shortcut(game)
    mac_desktop_link(root)
    firewall_rule(game)
    remember_root(root)
    if args.manifest:
        try:
            install_textures(args.manifest, root)  # a newer pack, if there is one
        except SetupError as e:
            say("    " + str(e))  # the game still runs on the textures it has
    if not args.no_launch:
        launch(root, game)
    say("")
    say("Done.")
    return 0


def main():
    global log_file
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", help="release folder (default: Setup's own folder)")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST, help="texture manifest URL or file")
    ap.add_argument("--no-launch", action="store_true")
    ap.add_argument("--skip-textures", action="store_true")
    ap.add_argument("--update", action="store_true",
                    help="download the newest release over this install, then start it")
    ap.add_argument("--tag", help="--update: this release instead of the newest")
    ap.add_argument("--wait-pid", type=int, help="--update: wait for this process to exit")
    args = ap.parse_args()

    root = os.path.abspath(args.root or app_root())
    log_file = open(os.path.join(root, "setup-log.txt"), "a")
    log(f"--- Setup {time.strftime('%Y-%m-%d %H:%M:%S')} on {platform.platform()}")
    try:
        if args.update:
            return run_update(root, args)
        say(f"{NAME} Setup")
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
        say("[4/4] Finishing..." if args.no_launch else "[4/4] Starting the game...")
        clear_quarantine(root)
        desktop_shortcut(game)
        mac_desktop_link(root)
        firewall_rule(game)
        remember_root(root)
        if not args.no_launch:
            launch(root, game)
        say("")
        say("Done. Next time, just open " +
            (f"{FILE}.app." if platform.system() == "Darwin"
             else f"the {NAME} shortcut on your desktop."))
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
        if getattr(sys, "frozen", False) and platform.system() == "Windows" and \
                not args.update:
            input("\nPress Enter to close this window.")


if __name__ == "__main__":
    sys.exit(main())

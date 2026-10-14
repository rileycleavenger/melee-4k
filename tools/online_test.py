#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Online play on one machine: a host and 1-3 joiners through real games.

    python3 tools/online_test.py --disc <iso> [--players 2..4] [--games 2]
        [--loss 5] [--delay-ms 50] [--jitter-ms 20] [--match-seconds 40]

Every instance boots straight into its lobby (MELEE_BOOT_SCENE=host/join).
The host starts as soon as everyone is in (MELEE_ONLINE_AUTOSTART); then
each instance is driven from its own key FIFO through character select, a
random stage, a match of random inputs, a quit from the pause menu and back
to character select; after the last game every player holds B back to the
lobby. Every instance records its frames (MELEE_NET_RECORD), and the run
passes when:

  - no instance logged "OUT OF SYNC", "game ended" or "online: failed";
  - every instance entered VS once per game;
  - every instance started each game from the same game data (the hash
    logged on loading the host's);
  - every recording agrees with the host's, checksum for checksum, over the
    frames they have in common (the host compares every 30th frame live;
    this compares all of them).

--loss / --delay-ms / --jitter-ms run every instance through online.c's
link simulator (MELEE_ONLINE_LOSS / _DELAY_MS / _JITTER_MS).
"""
import argparse
import os
import random
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import net_test  # fifo_write(), record_cks()

ROOT = os.path.dirname(HERE)
CACHE_ROOT = os.path.join(tempfile.gettempdir(), "melee_online_cache")  # kept between runs


def sessions(path):
    """Per-frame checksums of each session in a recording: [boot, game 1,
    game 2, ...]. A session starts with a fresh header (magic + seed)."""
    try:
        data = open(path, "rb").read()
    except OSError:
        return []
    magic = data[:4]
    stride = net_test.REC_FORMATS.get(magic)
    if stride is None:
        return []
    out, p = [], 0
    while p + 4 <= len(data):
        if data[p:p + 4] == magic:
            out.append([])
            p += 8
            continue
        if p + stride > len(data):
            break
        out[-1].append(data[p + net_test.CK_OFF:p + net_test.CK_OFF + 4])
        p += stride
    return out


class Instance:
    def __init__(self, index, exe, disc, work, env):
        self.index = index
        self.name = "host" if index == 0 else f"guest{index}"
        self.log_path = os.path.join(work, f"{self.name}.log")
        self.fifo = os.path.join(work, f"{self.name}.keys")
        os.mkfifo(self.fifo)
        self.rec = os.path.join(work, f"{self.name}.rec")
        user = os.path.join(work, f"{self.name}-user")
        os.makedirs(user)
        cache = os.path.join(CACHE_ROOT, str(index))
        os.makedirs(cache, exist_ok=True)
        e = dict(os.environ)
        for key in ("MELEE_NET_REPLAY", "MELEE_ONLINE_JOIN", "MELEE_LOG_FILE"):
            e.pop(key, None)
        e.update({
            "MELEE_USER_DIR": user,
            "MELEE_CACHE_DIR": cache,
            "MELEE_KEY_FIFO": self.fifo,
            "MELEE_NET_RECORD": self.rec,
            "MELEE_PORTMAP_OFF": "1",
            "MELEE_VSYNC": "0",
            "MELEE_WINDOW_TITLE": f"melee-online-{self.name}",
            "MELEE_BOOT_SCENE": "host" if index == 0 else "join",
            "MELEE_MOUSE_TEST": "off",  # the windows open under the real pointer
        })
        if e.get("MELEE_NET_STATE_LOG"):  # each instance's own file
            e["MELEE_NET_STATE_LOG"] = os.path.join(work, f"{self.name}.state")
        if "AURORA_SCREENSHOT_FRAMES" in e:  # each instance's own folder
            e["AURORA_SCREENSHOT_DIR"] = os.path.join(work, f"{self.name}-shots")
            os.makedirs(e["AURORA_SCREENSHOT_DIR"])
        e.update(env)
        self.env = e
        self.exe, self.disc = exe, disc
        self.proc = None

    def start(self, extra=None):
        if extra:
            self.env.update(extra)
        self.log = open(self.log_path, "wb")
        cmd = [self.exe, "--no-card", self.disc]
        if self.exe.lower().endswith(".exe"):
            # The Windows build under Wine: no GPU there, so Dawn's null
            # backend, and the app's own MSVC runtime over Wine's.
            cmd = ["wine64", self.exe, "--no-card", "Z:" + self.disc]
            self.env.update({"WINEDEBUG": "-all", "MELEE_BACKEND": "null",
                             "WINEDLLOVERRIDES": "msvcp140_atomic_wait=n;msvcp140=n"})
        self.proc = subprocess.Popen(cmd, env=self.env, stdout=self.log, stderr=subprocess.STDOUT)

    def key(self, line):
        net_test.fifo_write(self.fifo, line)

    def text(self):
        with open(self.log_path, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def count(self, pattern):
        return len(re.findall(pattern, self.text()))

    def mark(self):
        """Counts from here on (wait_new) ignore what the log already has."""
        self.marked_text = len(self.text())

    def count_new(self, pattern):
        return len(re.findall(pattern, self.text()[self.marked_text:]))

    def wait_new(self, pattern, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.count_new(pattern) >= 1:
                return True
            if self.proc.poll() is not None:
                return False
            time.sleep(0.25)
        return False

    def wait(self, pattern, timeout, n=1):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.count(pattern) >= n:
                return True
            if self.proc.poll() is not None:
                return False
            time.sleep(0.25)
        return False

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def mash(inst, seconds, seed):
    """Random fighting inputs for `seconds`: sticks, A, B, jumps, shields."""
    rng = random.Random(seed)
    keys = ["Left", "Right", "Up", "Down", "X", "Z", "C", "E", "Left+X", "Right+X",
            "Up+Z", "Down+X", "I", "K", "J", "L"]
    end = time.time() + seconds
    while time.time() < end:
        hold = rng.randint(40, 300)
        inst.key(f"{rng.choice(keys)} {hold}")
        time.sleep(hold / 1000.0 + 0.02)


def drive_game(insts, game, match_seconds, log):
    """CSS -> SSS -> match -> pause, quit -> CSS, from every instance's keys."""
    host = insts[0]
    for inst in insts:
        inst.mark()
    # Character select: each hand moves up onto the portraits, sideways by
    # port so no two land on the same spot, and picks; the host presses
    # Start. Tried again from where the hands are until stage select opens.
    time.sleep(3)
    for attempt in range(6):
        for inst in insts:
            inst.key("Up 300" if attempt == 0 else f"{'Up' if attempt % 2 else 'Down'} 150")
        time.sleep(0.6)
        for i, inst in enumerate(insts):
            inst.key(f"{'Left' if (i + attempt) % 2 else 'Right'} {120 + 90 * i}")
        time.sleep(0.5)
        for inst in insts:
            inst.key("X 120")
        time.sleep(1.2)
        host.key("Return 150")
        if all(i.wait_new(r"online: enter SSS", 6) for i in insts):
            break
        log(f"game {game}: stage select not reached yet (attempt {attempt + 1})")
    else:
        return False
    time.sleep(2.5)
    # The cursor starts below the grid; whoever pressed Start moves it (the
    # host). Up onto the stages, then pick.
    host.key("Up 400")
    time.sleep(0.7)
    host.key("X 150")
    if not all(i.wait_new(r"online: enter VS", 30) for i in insts):
        return False
    log(f"game {game}: match running")
    time.sleep(4)
    threads = [threading.Thread(target=mash, args=(inst, match_seconds, 1000 * game + inst.index))
               for inst in insts]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    # Pause and quit (L+R+A+Start held), back to character select.
    time.sleep(0.5)
    host.key("Return 120")
    time.sleep(1.0)
    host.key("Q+E+X+Return 800")
    if not all(i.wait_new(r"online: enter RESULTS", 30) for i in insts):
        return False
    # Results: Start until character select comes back.
    deadline = time.time() + 60
    while time.time() < deadline:
        if all(i.count_new(r"online: enter CSS") >= 1 for i in insts):
            return True
        time.sleep(2)
        for inst in insts:
            inst.key("Return 100")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "macos", "melee"))
    ap.add_argument("--guest-exe", help="the joiners' game (a Windows melee.exe runs under Wine)")
    ap.add_argument("--disc", required=True)
    ap.add_argument("--players", type=int, default=2)
    ap.add_argument("--games", type=int, default=2)
    ap.add_argument("--match-seconds", type=float, default=40)
    ap.add_argument("--loss", type=int, default=0)
    ap.add_argument("--delay-ms", type=int, default=0)
    ap.add_argument("--jitter-ms", type=int, default=0)
    ap.add_argument("--guest-rules", action="store_true",
                    help="joiners start with their own different rules (teams, 1 stock)")
    ap.add_argument("--guest-mouse", metavar="U,V@FRAME",
                    help="the joiners' mouse pointer, fixed there from that frame "
                         "(MELEE_MOUSE_TEST): their hands follow it through lockstep")
    ap.add_argument("--stage", type=int, help="play every game on this stage kind "
                    "(MELEE_ONLINE_TEST_STAGE; 3 = Rainbow Cruise)")
    ap.add_argument("--work", default=None)
    ap.add_argument("--keep", action="store_true", help="leave the instances running at the end")
    a = ap.parse_args()

    work = a.work or tempfile.mkdtemp(prefix="melee-online-")
    if a.work:
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(work)
    t0 = time.time()

    def log(msg):
        print(f"[{time.time() - t0:6.1f}s] {msg}", flush=True)

    env = {}
    if a.stage is not None:
        env["MELEE_ONLINE_TEST_STAGE"] = str(a.stage)
    if a.loss:
        env["MELEE_ONLINE_LOSS"] = str(a.loss)
    if a.delay_ms:
        env["MELEE_ONLINE_DELAY_MS"] = str(a.delay_ms)
    if a.jitter_ms:
        env["MELEE_ONLINE_JITTER_MS"] = str(a.jitter_ms)
    insts = [Instance(i, a.guest_exe if i and a.guest_exe else a.exe, a.disc, work, env)
             for i in range(a.players)]
    ok = False
    try:
        insts[0].start({"MELEE_ONLINE_AUTOSTART": str(a.players)})
        if not insts[0].wait(r"lobby: code \S+", 240):
            log("host never showed a lobby code")
            return 1
        code = re.search(r"lobby: code (\S+)", insts[0].text()).group(1)
        log(f"host lobby code {code}")
        for inst in insts[1:]:
            extra = {"MELEE_ONLINE_JOIN": code}
            if a.guest_rules:
                extra["MELEE_TEST_LOCAL_RULES"] = "1"
            if a.guest_mouse:
                extra["MELEE_MOUSE_TEST"] = a.guest_mouse
            inst.start(extra)
        for game in range(1, a.games + 1):
            if not all(i.wait(r"online: lockstep from frame 0", 240, game) for i in insts):
                log(f"game {game}: not everyone reached lockstep")
                return 1
            log(f"game {game}: lockstep running on {a.players} machines")
            if not drive_game(insts, game, a.match_seconds, log):
                log(f"game {game}: did not get through")
                return 1
            log(f"game {game}: back at character select")
            # Everyone holds B: back to the lobby, where the host starts the
            # next game by itself.
            time.sleep(2)
            for _ in range(3):  # put the characters back first
                for inst in insts:
                    inst.key("Z 120")
                time.sleep(0.5)
            for inst in insts:
                inst.key("Z 3000")
            if not all(i.wait(r"online: back to the lobby", 30, game) for i in insts):
                log(f"game {game}: not everyone got back to the lobby")
                return 1
        ok = True
    finally:
        bad = []
        for inst in insts:
            text = inst.text() if os.path.exists(inst.log_path) else ""
            for pat in (r"OUT OF SYNC", r"online: game ended", r"online: failed"):
                if re.search(pat, text):
                    bad.append(f"{inst.name}: {pat}")
        if not a.keep:
            for inst in insts:
                inst.stop()
        # The game data each machine started each game from.
        hashes = [re.findall(r"game data loaded \([^,]*, hash (\w+)\)", i.text()) for i in insts]
        for inst, h in zip(insts[1:], hashes[1:]):
            if h != hashes[0]:
                bad.append(f"{inst.name}: game data {h}, the host's {hashes[0]}")
        ref = sessions(insts[0].rec)[1:]
        compared = 0
        for inst in insts[1:]:
            mine = sessions(inst.rec)[1:]
            if len(mine) != len(ref):
                bad.append(f"{inst.name}: {len(mine)} sessions recorded, the host {len(ref)}")
            for k, (a_cks, b_cks) in enumerate(zip(ref, mine)):
                n = min(len(a_cks), len(b_cks))
                compared += n
                for f in range(n):
                    if a_cks[f] != b_cks[f]:
                        bad.append(f"{inst.name}: game {k + 1} frame {f} differs from the host's")
                        break
        log(f"compared {compared} guest frames with the host")
        for b in bad:
            log(f"FAIL {b}")
        log(f"logs in {work}")
        if ok and not bad:
            log("PASS")
    return 0 if ok and not bad else 1


if __name__ == "__main__":
    sys.exit(main())

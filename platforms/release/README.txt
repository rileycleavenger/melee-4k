MELEE 4K
========

Super Smash Bros. Melee at 4K, widescreen, with HD textures and smooth
motion on high-refresh displays. You need your own disc image of
Super Smash Bros. Melee (USA, version 1.02): .iso, .gcm, .ciso or .rvz.


INSTALL AND PLAY (macOS)
------------------------
Easiest: open Terminal and paste
  curl -fsSL https://raw.githubusercontent.com/rileycleavenger/melee-4k/v1-pc/install.sh | bash
then drag your disc image into the Finder window it opens.

From this zip instead:
1. Drag your Melee disc image into the PUT-YOUR-ROM-HERE folder.
2. Right-click Setup.command and choose Open. If macOS says it could not
   verify it, open System Settings > Privacy & Security and click
   "Open Anyway" next to Setup.command.
3. Wait for "Done". The first run downloads the HD textures (a few
   minutes), then the game starts.
4. From then on, open Melee4K.app to play.


INSTALL AND PLAY (Windows)
--------------------------
1. Drag your Melee disc image into the PUT-YOUR-ROM-HERE folder.
2. Double-click Setup.exe. If Windows shows "Windows protected your PC",
   click More info, then Run anyway.
3. Wait for "Done". The first run downloads the HD textures (a few
   minutes), then the game starts.
4. From then on, double-click Melee4K\melee.exe (Setup also adds a
   desktop shortcut).


PLAYING
-------
Controllers: DualSense, Switch Pro, Xbox and GameCube adapters work out of
the box (bottom face button A, right one B). Keyboard: arrows = stick, X = A, Z = B, C = X, V = Y, Q/E = L/R,
Enter = Start.

Online: VS Mode > Online. One player picks Host Lobby and shares the code;
up to three friends pick Join Lobby and type or paste it. The host presses
Start when everyone is in. Works Mac <-> PC. Hold B (or Esc) to leave a
lobby. If the lobby says "Same Wi-Fi only", forward UDP port 41000 to the
host's computer or let someone else host.

Updates: Options > Melee 4K > Check for Updates (or F1). The game closes,
updates itself and opens again; your disc, saves and settings stay.

Mouse: moves the character select hand, in the menus too (point to
select, left click A, right click B, wheel changes a value).

Settings: main menu > Options > Melee 4K (screen 16:9 or 4:3, HUD,
resolution, frame rate 60 to 144 fps, HD textures, stock icons, FPS
counter, Unlock All; Left/Right changes a setting). During a match, F1 (or Back/Select/Create on a controller) opens
the same settings plus controls, audio and graphics.


IF SOMETHING GOES WRONG
-----------------------
- Setup says what to do in one sentence. Fix that and run Setup again; it
  picks up where it left off.
- "Disc not recognized": use a full dump of the USA 1.02 disc. NKit images
  and zipped images are not supported; unzip first.
- The game will not open on a Mac: right-click Setup.command > Open once,
  as in step 2.
- Setup's log is setup-log.txt in this folder.


UPDATING
--------
Unzip the new release, move your disc image into its PUT-YOUR-ROM-HERE
folder and run Setup. Your saves, settings and textures are kept.

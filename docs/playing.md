# Playing Melee 4K

Install steps are in the [README](../README.md#install).

## Controls

- **Controllers:** DualSense, Switch Pro, Xbox and GameCube controllers (with
  an adapter) work as soon as they're plugged in. The bottom face button is
  A and the right one is B (Cross and Circle on a DualSense). A DualSense's
  light bar shows your port colour. F1 > Controls remaps any button.
- **Keyboard:** arrows = stick, X = A, Z = B, C = X, V = Y, Q/E = L/R,
  Enter = Start.
- **Mouse:** move the mouse and the character select hand follows it, in the
  menus too: point at an entry to select it, left click is A, right click is
  B, and the wheel changes a value (Left/Right). Touch the stick or d-pad and
  the controller takes over again.

## Online

VS Mode > Online. One player picks **Host Lobby** and shares the code it
shows (Y copies it); up to three friends pick **Join Lobby** and type or paste
it. When everyone is in, the host presses Start and you all go to character
select together, on the host's rules. Mac and PC can play each other, over the
internet or on the same Wi-Fi. Everyone needs the same Melee 4K version.

- **Only the host's network matters.** Melee 4K asks the host's router to
  open UDP port 41000 by itself. If the lobby says "Same Wi-Fi only", forward
  UDP 41000 to the host's computer in the router's settings, or let someone
  else host.
- **B on character select** takes everyone back to the lobby; B in the lobby
  leaves it.
- **Input delay** is picked from the slowest player's connection (2 frames on
  the same Wi-Fi, more over the internet). The host can set it under
  Settings (F1) > Online.
- If a player leaves or the connection drops, everyone goes back to the lobby
  with the reason, and the host can start again.

## Settings

In the game's main menu, **Options > Melee 4K**. Left/Right changes a
setting:

| Setting | |
|---|---|
| Screen | 16:9, the original 4:3, or Auto (fit the window). Changes take effect at once, mid-match too. |
| HUD | Wide (out to the 16:9 edges) or Classic. |
| Resolution | Auto (your display's: 4K on a 4K screen) or 1x to 10x. |
| Frame Rate | 60 (the default), 120 or 144 fps. The game itself always runs at 60; above 60, in-between frames are drawn, so pick 120 or 144 only on a display that fast. |
| HD Textures | On or off. |
| Stocks | The stock icon style: Melee HD (the default), Circle, Smash 4 or Classic pixel. |
| FPS Counter | The frame rate, top right. |
| Unlock All | Every character, stage and mode (on by default). |
| Check for Updates | Looks for a newer Melee 4K; press A again to install it (the game closes, updates and reopens, keeping your disc, saves and settings). |

During a match, **F1** (or Back/Select/Create on a controller) opens the same
settings plus the rest: controls, audio, graphics. To play a different disc
image: F1 > Audio & interface > Disc > Choose another, and the disc picker
opens the next time you start Melee 4K.

## Troubleshooting

- **Setup tells you what to do** in one sentence when something's wrong. Fix
  that and run Setup again; it picks up where it left off.
- **"Disc not recognized":** use a full dump of the USA 1.02 disc. Zipped
  images must be unzipped first; NKit images aren't supported.
- **Mac "could not verify" dialog:** that appears for the downloaded zip, not
  the Terminal install; use the Terminal line in the README.
- **Textures didn't download:** check your internet connection and run Setup
  again; finished parts aren't downloaded twice.
- Setup writes **setup-log.txt** next to itself; attach it to a
  [bug report](https://github.com/rileycleavenger/melee-4k/issues).

# Credits and notices

Melee 4K ships no game data. You supply your own disc image of Super Smash
Bros. Melee; Super Smash Bros. Melee and its characters and artwork are
trademarks and copyrights of Nintendo and HAL Laboratory. This project is
not affiliated with or endorsed by them.

## Code

- **[melee-pc](https://github.com/999sian/melee-pc)**: the native PC port
  this project is a fork of (GPL-3.0-or-later).
- **[doldecomp/melee](https://github.com/doldecomp/melee)**: the
  decompilation the port is built from. Its license terms are its own; see
  `licenses/` and the upstream repository.
- **[aurora](https://github.com/encounter/aurora)**: GameCube SDK
  compatibility layer and WebGPU renderer.
- **Dawn**, **SDL3**, **RmlUi**, **nod**, **xxHash**, **zstd**, **libpng**,
  **monocypher** and the other libraries listed in `licenses/`.
- **[dusklight](https://github.com/TwilitRealm/dusklight)**: the approach this
  project follows.
- **[melee-unlocked](https://github.com/Hero88go/melee-unlocked)**: used as a
  reference for widescreen behaviour (Slippi's widescreen fix lists).

## HD textures

The HD texture download is built from community packs, extended with AI
upscales only of the textures none of them covers. Thank you to everyone whose
work it contains:

- **Super Smash Bros. HD Remix Texture Pack**, organised by
  **Quarantine_Zone (QZ|DOGG)**, combining Melee HD, Faster Melee HD,
  SSBM: HQ, Melee Remastered and original textures. Its credits thank
  Achilles1515, Anutim, Combatman12, Durgan (also the DRGN Bowser stocks),
  Ed94 (Melee Remastered), MrLul (HD Dream Land 64), Ripple (Super Smash
  Bros. HD Remix), Stache, Steelia (SSBM: HQ), Super4ng (stock icons),
  Truckjitsu (Faster Melee, Faster Melee HD), Unclepunch and Warriorknight.
- **Melee HD** (2023 release) by the Melee HD team: David V. Kimball, Alecks,
  Radzo73, Tyleto, Quarantine_Zone and Kirbeast; playtesters MetalLegacy,
  Vianny, Liva, Carldacious, Readek, Stache and Zapfy. Its character select
  portraits come from the Melee Cosmetic Standardization Project (Efisio,
  with Cjag, Radzo73 and Cahalan). Its credits thank UnclePunch and Stache,
  the meleehd.boards.net / MeleeHD community, the SD Remix crew, the Faster
  Melee folks, Karmo1, solidturnip, AlGeorgeRomo and the Smash 2 team (the
  models the original pack was based on), and Neil and ! C O (fonts).
- **Melee HD** project submissions (Melee HD Submissions and the Melee HD
  edits).
- **Faster Melee HD** by Truckjitsu.
- **SD Remix 3.3 additional HD textures** (SD Remix team).
- The selectable stock icon styles (Circle, Smash 4, Classic pixel) come from
  the HD Remix pack's stock icon options.

AI upscales were made with **[Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)**
(BSD-3-Clause), using its `realesrgan-x4plus` and `realesrgan-x4plus-anime`
models, via realesrgan-ncnn-vulkan.

If you made one of these packs and want your work credited differently or
left out, please open an issue.

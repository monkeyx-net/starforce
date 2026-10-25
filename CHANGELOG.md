# Star Force — native SDL2 port — Changelog

---
## Changelog

### v0.1.1 (2026-10-25)

#### Game

- **The emulated board** — the main and sound Z80s, the PIO and CTC on the
  sound CPU's daisy chain, the three background layers, foreground and
  sprites, MAME's scheduler, checked against MAME 0.289 instruction for
  instruction (`--emulate`); the three SN76489As and the waveform DAC
  modelled from MAME's output.
- **Assets extracted once** — the tiles, sprites, font, the DAC's waveform
  and the programs' tables from the player's `starforc.zip` (`--extract`,
  or on the first run; to the save folder when the program's is
  read-only).
- **The game in C by default** — both Z80 programs rewritten in C on the
  board's scheduler: memory and sound equal to the emulated board's at
  every frame. It needs only the extracted assets.
- **Cocktail table** — player two's turns turned round as MAME draws them.

#### Port

- **Operator menu** — F2 (Select+R1 on a pad) opens the DIP switches over
  the game, which waits: both coin slots, ships, bonus ship, difficulty,
  demo sounds, cabinet; saved to `starforce.cfg`.
- **High scores kept** — the BEST PLAYERS table and the top score in
  `starforce.hiscore`.
- **Controls page** — shown over an idle attract mode.
- **Lower CPU use** — the glow's blur is added by the GPU (stretched
  linearly, added over the frame) where the renderer is accelerated, from a
  grid computed on the arcade frame; the smooth scaler skips equal colours;
  4x displays get the effects rendered at 2x and doubled by the GPU; the
  game's picture is composed a row at a time. Effects at 4x went from about
  10 ms a frame to about 1 ms on a desktop.

### v0.1.0 (unreleased)

#### Port

- **Skeleton** — the platform layer, loaders, framebuffer, enhanced
  graphics, packaging, CI and MAME test harness from the sibling Galaga
  port, for a 224x256 portrait screen (integer scaling, or fitted with
  sharp bilinear on screens such as 640x480; scanlines down the screen),
  paced at the board's 59.19 Hz.
- **ROM set** — the MAME `starforc` set from a zip or folder, matched by
  CRC; `--check-rom`, `--dump-rom` (checked against MAME by
  `mame_rom_map`).
- **Test card** — ROM status, an input tester for both players' sticks,
  the two coin slots and the starts, the graphics presets.

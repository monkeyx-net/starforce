# Star Force — native SDL2 port — Changelog

---
## Changelog

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

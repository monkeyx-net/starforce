# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A native C11 / SDL2 port of Star Force (Tehkan, 1984; MAME `starforc`,
driver `tecmo/senjyo.cpp`) for Linux handhelds (aarch64: ArkOS, ROCKNIX,
muOS, Knulli), x86_64 (Steam Deck, desktops), Windows, macOS and the web.
**The plan, decisions, hardware notes, test plan and progress live in
[PROGRESS_STARFORCE_PORT.md](PROGRESS_STARFORCE_PORT.md)**; read it before
larger changes and keep its phase table and hardware section up to date.

The repo starts as a copy of the sibling Galaga port's skeleton
(`../galaga`: platform layer, loaders, framebuffer, enhanced graphics,
packaging, CI, MAME harness, Z80 core, MAME's scheduler), itself from the
Galaxian and Defender ports. Improvements to that shared code may be worth
carrying across (the plan lists some), but ask first.

## Ground Rules

- **The game is being rewritten in C** (plan decision 6): game logic from
  the ROM's behaviour, checked frame by frame against the emulated board
  (`src/emu/`). Until the C phases are done the emulated board is the
  game; afterwards the C game is the default and `--emulate` runs the
  board. MAME is the reference for the emulation: changes to `emu/` must
  keep the `mame` tests passing. Write all emulation from the hardware's
  behaviour, not from MAME's source.
- **The ROM set is read once** (plan decision 1): `starforce --extract`
  writes tiles, sprites, font, the DAC's waveform and tables to `assets/`,
  which the C game reads instead of the ROM. No ROM-derived data in the
  repo, the binary or the packages; the extractor's tables name ROM
  addresses only.
- **One ROM set:** `starforc.zip` (the parent); no device ROMs, no clones
  unless asked.
- **All new code and tooling is C** (built with CMake). The one exception
  is MAME probe scripts, which must be Lua; they only dump to text files,
  and C or CMake does the comparing.
- New files go in `src/`, `CMakeLists.txt`, `.github/` or
  `PROGRESS_STARFORCE_PORT.md`. Ask before adding files elsewhere.
- The user's ROM sets live in `roms/` (git-ignored); never commit ROMs or
  zips, never send them anywhere, and never overwrite or replace the files
  there.

## Commands

```bash
cmake -S . -B build && cmake --build build -j     # build
ctest --test-dir build                            # all tests
ctest --test-dir build -L rom                     # tests needing the ROM set (roms/starforc.zip)
ctest --test-dir build -L mame                    # tests needing MAME + the ROM set
build/starforce --windowed                        # the ROM's own code on the emulated board (dev builds find roms/starforc.zip)
build/starforce --windowed --test-card            # the test card (ROM status, input tester)
build/starforce --check-rom roms/starforc.zip     # verify a ROM set
build/starforce --dump-rom roms/starforc.zip [region]  # MAME's regions as hex lines (maincpu sub fgtiles bgtiles1 bgtiles2 bgtiles3 sprites dac)
build/starforce --extract roms/starforc.zip [dir] # assets from the ROM set (dev builds: build/assets after each build)
SDL_VIDEODRIVER=dummy build/starforce --frames 60 --screenshot out.png   # headless screenshot
cmake --build build --target package_linux package_portmaster   # release archives
cmake -S . -B build-san -DSTARFORCE_SANITIZE=ON   # ASan/UBSan build
emcmake cmake -S . -B build-web && cmake --build build-web --target serve   # web build on http://localhost:8000/
mame starforc -rompath roms -verifyroms           # MAME's check of the set
```

Release builds: `-DSTARFORCE_DEV_ASSETS=OFF` (no source-tree fallbacks).
Commands for phases not yet done (see the plan's table) arrive with them:
the game in C (C0-C3; `--emulate` then selects the board).

## Layout (`src/`)

What is there after P0; the parts marked (E1), (X1), (C0-C3) and (E3)
arrive with their phases.

- `main.c` - SDL setup, the fixed-rate loop (59.19 Hz, the board's; it
  sleeps when no step is due), the graphics pipeline, the test card,
  `--frames`, `--screenshot`, `--rom`, `--check-rom`, `--dump-rom`; later
  a frame of the C game or the board (`--emulate`) per tick, the DIP
  switches (`starforce.cfg`, the operator menu), the high scores
  (`starforce.hiscore`), the controls page (E3)
- `platform/` - the only code that includes SDL: `video.c` (224x256
  portrait framebuffer, integer scaling or fitted with sharp bilinear,
  scanlines down the screen), `input.c` (two 8-way sticks and a button
  each; pads and keyboard), `audio.c` (streams generated samples)
- `gfx/` - draws into the framebuffer in arcade coordinates: `fb.c`,
  `blit.c`, `scale.c`, `enhance.c`, `debugfont.c` (from Galaga), and
  `starforce.c` (a frame from the video RAM: the fg layer, the three
  scrolling bg layers, the sprites in their four priorities, the stripes,
  the palette RAM, the flip; the graphics decoded from the ROM or the
  assets)
- `assets/` - `rom.c` (the starforc set from a zip or folder, matched by
  CRC), `zip.c`, `inflate.c`, `png.c`, `assets.c` (asset folder lookup),
  `savefile.c` (the save folder), `extract.c` (tiles, sprites, font, DAC
  waveform, tables, previews; `manifest.txt` written last; and their
  loaders)
- `emu/` - the emulated board: `z80.c` (from Galaga, with IM 2's
  acknowledge and RETI for a daisy chain), `board.c` (main and sub, the
  memory maps, the sound latch, the PIO and CTC on the sub's daisy chain,
  interrupts, MAME's scheduler: attosecond time, slices, syncs, Z80s
  stopping between machine cycles; the sound's writes timed in the frame),
  `sound.c` (the three SN76489As and the waveform DAC, modelled from
  MAME's output; 811 or 812 samples a frame at 48 kHz)
- `sim/` (C phases) - the game in C: `main_run.c`, `sub_run.c` (each Z80
  program whole, every instruction a case of one resumable function),
  `cpu.h`, `game.c`, `sim.c` (the board's scheduler and devices a frame at
  a time)
- `tests/` - C tests, MAME probes (`tests/mame/`)
- `packaging/` - release layout script, PortMaster launcher and metadata
- `web/` - page shell for the Emscripten build

## Conventions

- C11, `-Wall -Wextra`, no warnings. Builds must also pass in Ubuntu 20.04
  (GCC 9, SDL 2.0.10, the PortMaster base): use only the SDL 2.0.10 API.
- The board is deterministic: one frame per tick, no wall clock.
- Enhanced graphics are cosmetic: classic output stays the arcade frame.
- Match the surrounding code: comment density, naming, idiom.
- Each phase gets its own commit; nothing is pushed or published without
  asking.

## Testing

Every change keeps `ctest` green. Tests arrive with their phase (plan
section 4). Now:

- `enhance` - always runs (scalers, presets, glow)
- `z80` - always runs: every opcode's T-states and machine cycles, known
  flag results, NMI, IM 1, HALT
- `rom_load` - label `rom`: loads the set from the zip, from renamed
  copies in a folder (CRC matching), and fails cleanly on an empty folder
- `mame_rom_map` - label `mame`: MAME's view of every region equals
  `starforce --dump-rom`
- `mame_capture` - label `mame`: runs the scripted game in
  `src/tests/mame/script.cmake`, dumping RAM, video RAM, sprites,
  palette, registers, the sub's RAM and the flip with a snapshot at 16
  frames; each frame drawn from its dump equals MAME's
- `mame_emu` - label `mame`: the board replays that script from power-on:
  MAME's traces of frames 1700-1800 (both CPUs, instruction for
  instruction); all memory and every pixel at every captured frame; the
  sound against MAME's recording (levels within 3 dB)

- `extract` - label `rom`: two extractions are byte-identical; what is
  read back (graphics, waveform, tables) is the ROM set's; the font reads
  the attract mode's words the emulated board draws; no manifest without a
  ROM set

To come: `frame` (label `rom`, C0-C3).

- To debug a difference: `test_emu` with `DUMP_DIR=dir`; `trace_probe.lua`
  with `TRACE_CPU`, `TRACE_FROM`/`TRACE_TO` (needs `-debug -debugger
  none`); `dasm_probe.lua` for the two programs' disassembly. Traces,
  dumps and disassembly stay in `build/`. MAME tests share `build/mame`
  (one at a time); tests that run the game use their own save dir; never
  write to the user's home folder.

## Reference

- ROM: MAME `starforc` (0.289 names); CRCs in `src/assets/rom.c` and the
  plan's section 2.
- Hardware: Tehkan Senjyo-type board (main Z80 at 4 MHz; sub Z80 at 2 MHz
  with a Z80 PIO and CTC; fg tiles, three scrolling bg layers, sprites,
  palette RAM; three SN76489As and an 8-bit DAC). Notes go in the plan as
  they are confirmed.
- MAME 0.289 (`/usr/bin/mame`) runs it headless; see
  `src/tests/mame/mame_common.cmake` for the options.

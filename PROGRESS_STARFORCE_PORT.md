# Star Force native port: plan and progress

A C11 / SDL2 port of Star Force (Tehkan, 1984; MAME `starforc`, driver
`tecmo/senjyo.cpp`) that plays like the arcade game, for PortMaster
handhelds (aarch64), desktops (x86_64 Linux, Windows, macOS) and the web.
It is converted the same way as the sibling Galaga port (`../galaga`):
emulate the board, check the emulator against MAME 0.289, extract the
assets once, then rewrite the game in C, checked frame by frame against
the emulated board.

The repository starts on 2026-10-25 as a copy of Galaga's skeleton
(platform layer, loaders, framebuffer, enhanced graphics, packaging, CI,
MAME harness, the Z80 core and MAME's scheduler), itself from the
Galaxian, Defender, Joust and Robotron ports. The hardware (two Z80s, the
Z80 PIO and CTC on the sound CPU, the three SN76489As and the DAC, the
scrolling background layers) is new.

## 1. Decisions (2026-10-25)

1. **The ROM set is read once** (as Galaga's decision 1). The binary never
   embeds ROM-derived data; it loads it at run time from an `assets/`
   folder that `starforce --extract <starforc.zip>` writes from the
   player's own ROM set (the PortMaster launcher runs it on the first
   start). The repo, CI and releases carry no ROM-derived data: not tiles,
   sprites, the font, the DAC's waveform or tables; the extractor's tables
   name ROM addresses only. Until the game is in C, the emulated board runs
   the ROM's code and reads the ROM set.
2. **MAME is the reference.** Behaviour is established by probing the ROM
   in MAME 0.289 and checked by CTest against the port. Probes are Lua and
   only dump; C or CMake compares. Emulation is written from the hardware's
   behaviour, not from MAME's source.
3. **Shared code is copied, not linked**, from `../galaga/src` (renamed
   galaga → starforce, GALAGA_ → STARFORCE_): `platform/`, `gfx/` (bar
   `galaga.c`), `assets/` (with a new `rom.c` and `extract.c`),
   `packaging/`, `web/`, CI, the MAME CMake and Lua harness; in E1 the Z80
   core and the scheduler from `emu/board.c`. Not needed: the MB88xx core,
   the 06xx, the WSG, the 54xx's noise. Fixes found here that apply there
   are listed in section 5, to carry across once agreed.
4. **Controls:** an 8-way stick and one button per player (MAME: 2
   players, 2 coins). Pad: stick or D-pad, A or B fire; the first pad is
   player one, the second player two (one pad: player two's turns on the
   same pad, as an upright). Select = coin, Start = 1P, L1+Start = 2P,
   Select+L1 = graphics presets, hold Select+Start 1 s = exit. Keyboard:
   arrows (player one), Space/Ctrl fire, 5 coin A, 6 coin B, 1/Enter and
   2 start, F3 presets, F4 glow, F5 scanlines, Esc exit. The DIP switches
   (coinage A/B, lives, cabinet, demo sounds, bonus life, difficulty) come
   from a settings file (`starforce.cfg`) with MAME's defaults; an
   operator menu (F2, Select+R1) changes them while the game waits and
   saves the file.
5. **Enhanced graphics** as in the Galaga port (FILTER and HD presets,
   glow, scanlines), cosmetic only, classic by default. The screen is
   portrait (224x256 as the player sees it, MAME's 256x224 turned 90
   degrees): integer scaling where it fills the screen, else fitted (sharp
   bilinear, pillarboxed); the scanlines run down the screen.
6. **The game in C.** The game logic is rewritten in C (`src/sim/`), from
   the ROM's behaviour (traces, probes, the disassembly MAME writes to
   `build/re`, never committed): each CPU's program whole, instruction by
   instruction, mirroring the ROM's arithmetic and timing, so that the two
   Z80s with the PIO and CTC, scheduled as the board schedules them, do
   what the board does cycle for cycle. The emulated board (`src/emu/`,
   checked against MAME by `mame_emu`) stays as the reference the C game
   is compared with.
7. **High-score saving** (a port feature, if the arcade forgets its table
   at power-off: to confirm by probe): the table with initials,
   `starforce.hiscore` in the save folder.
8. **One ROM set.** `starforc.zip` is all the port needs: no device ROMs.
   The clones (`starforca`, `starforcb`, `starforce`, `megaforc`,
   `megaforcu`) are not supported unless asked for; the loader matches the
   parent's files by CRC, in the zip or a folder.
9. **The DAC's waveform is extracted** (`07b.bin`, 32 bytes, MAME region
   `:dac`): it is ROM data, so it goes to `assets/` with the rest. The
   SN76489As are emulated from their behaviour, checked against MAME's
   recording.

## 2. Hardware

Confirmed so far (MAME 0.289's view, by `-listxml` and `-verifyroms`; the
user's `roms/starforc.zip` verifies good):
- ROM set (`rom_load`, `mame_rom_map`), MAME region and offset:
  - `:maincpu` (32 KB): `3.3p` 8ba27691 ($0000), `2.3mn` 0fc4d2d6 ($4000)
  - `:sub` (8 KB): `1.3hj` 2735bb22
  - `:fgtiles` (12 KB): `7.2fh` f4803339 ($0000), `8.3fh` 96979684
    ($1000), `9.3fh` eead1d5c ($2000)
  - `:bgtiles1` (24 KB): `15.10jk` c3bda12f ($0000), `14.9jk` 9e9384fe
    ($2000), `13.8jk` 84603285 ($4000)
  - `:bgtiles2` (24 KB): `12.10de` fdd9e38b ($0000), `11.9de` 668aea14
    ($2000), `10.8de` c62a19c1 ($4000)
  - `:bgtiles3` (12 KB): `18.10pq` 6455c3ad ($0000), `17.9pq` 68c60d0f
    ($1000), `16.8pq` ce20b469 ($2000)
  - `:sprites` (48 KB): `6.10lm` 5468a21d ($0000), `5.9lm` f71717f8
    ($4000), `4.8lm` dd9d68a4 ($8000)
  - `:dac` (32 bytes): `07b.bin` 68db8300
  - No colour PROMs in the set: the palette is expected in RAM (to probe).
  - MAME keeps every region in the files' order (none is rearranged at
    load: `rom_map_probe.lua` reads each straight).
- CPUs (`-listxml`): `maincpu` Z80 at 4 MHz; `sub` Z80 at 2 MHz with a Z80
  PIO and a Z80 CTC (four channels). Screen: 256x224 visible of 384x264 at
  6 MHz (59.186 Hz; visible lines 16-239), turned 90 degrees. Palettes:
  `palette` and `radar_palette` (the radar is Senjyo's; whether Star Force
  draws it, to probe). Sound: three SN76489A at 2 MHz, an 8-bit R-2R DAC
  and a volume filter, one channel; a sound latch from main to sub.
- DIP switches (MAME's ports DSW1, DSW2; defaults): DSW1 coin A 03 (1C/1C
  = 0 default, 2C/1C 1, 1C/2C 2, 1C/3C 3), coin B 0C (the same by 4, 8,
  C), lives 30 (3 = 00 default, 4 = 10, 5 = 20, 2 = 30), cabinet 40
  (upright set, the default), demo sounds 80 (on set, the default). DSW2
  bonus life 07 (50K, 200K and 500K = 0 default; 100K/300K/800K 1, 50K and
  200K 2, 100K and 300K 3, 50K 4, 100K 5, 200K 6, none 7), difficulty 38
  (easiest 00 default, easy 08, medium 10, difficult 18, hard 20, hardest
  28; 30 and 38 unused), unknown 40 and 80 (off). Ports P1, P2, SYSTEM.

To confirm by probe (written up here as confirmed):
- **Memory maps** of both CPUs (MAME's address maps): ROM, RAM, the fg
  video RAM and colour, the three bg layers' RAM and scroll registers, the
  sprite RAM, the palette RAM, the inputs and DIP switches, the sound
  latch, the PIO and CTC ports, the SN76489As' and the DAC's writes, the
  watchdog if any.
- **Interrupts:** main's (the vertical blank, IM and vector); sub's daisy
  chain (PIO, CTC), the CTC channel clocking the DAC's waveform steps.
- **Video:** the fg layer (8x8, 3 bitplanes), the three bg layers (16x16
  from `bgtiles1`-`2`, 3 bitplanes; `bgtiles3`'s layout), their scroll and
  priorities, the sprites (16x16 and 32x32?, 3 bitplanes, flips), the
  palette RAM's format and weights, the cocktail flip, any star field or
  radar.
- **Sound:** the SN76489As' tone, noise and volume as MAME outputs them;
  the DAC playing `07b.bin` stepped by the CTC, with its volume filter.
- **High scores:** a table with initials in RAM.
- **Encryption:** none expected for the parent set (some clones are
  encrypted: not supported).

## 3. Phases

| Phase | Work | Exit | Status |
|---|---|---|---|
| P0 | `git init`, skeleton from Galaga, loader for the starforc set, test card, MAME ROM map, packaging, CI, `CLAUDE.md`, README | `enhance`, `rom_load`, `mame_rom_map` pass | done 2026-10-25 |
| E1 | Two Z80s, the PIO and CTC, board (memory maps, latches, interrupts, watchdog), video (fg, three bg layers, sprites, palette RAM); the game plays via `--emulate` (silent) | `z80`; `mame_emu`: both CPUs cycle for cycle over traced frames; RAM and pictures equal at captured frames (`mame_capture`) | not started |
| E2 | Three SN76489As, the DAC (waveform from `07b.bin`, stepped by the CTC), the volume filter | each voice exact; the mix's level and timing as MAME's wav | not started |
| X1 | Extractor: fg and bg tiles, sprites, font, DAC waveform, tables | `extract` | not started |
| C0-C3 | RE notes; the C game (both CPUs' programs, the PIO and CTC, the board's scheduler), then the default | `frame`; the C game plays from power-on through game over | not started |
| E3/E4 | Polish (cocktail flip, devices, web), the DIP switches' menu, high-score saving, release | device checklist; published | not started |

Each phase gets its own commit. Nothing is pushed or published without
asking.

## 4. Tests

- `enhance` - always runs (scalers, presets, glow)
- `rom_load` - label `rom`: the set from the zip, from renamed copies in a
  folder (CRC matching, the planes in another order, unrelated files
  beside them), and a clean failure on an empty folder
- `mame_rom_map` - label `mame`: MAME's view of every region (`:maincpu`,
  `:sub`, `:fgtiles`, `:bgtiles1`-`3`, `:sprites`, `:dac`) equals
  `starforce --dump-rom`
- `z80` - always runs: every opcode's T-states and machine cycles, where
  writes fall, known flag results, NMI, IM 1, IM 2 with the daisy chain,
  HALT (from Galaga)
- `mame_capture` - label `mame`: a scripted game in MAME
  (`src/tests/mame/script.cmake`: power-on, the attract mode, a coin, a
  start, over a minute of play), dumping RAM, video RAM, the palette and
  the scroll registers with a snapshot at captured frames; each frame
  drawn from the dump equals MAME's snapshot
- `mame_emu` - label `mame`: the board replays the script from power-on:
  MAME's traces of both CPUs instruction for instruction (cycle and
  registers); at every captured frame all RAM and the picture equal
  MAME's; the sound against MAME's recording of the run (`game.wav`)
- `extract` - label `rom`: two extractions are byte-identical and hold
  every file; what is read back is the ROM set's; the font reads the title
  screen the emulated board draws; no manifest without a ROM set
- `frame` - label `rom`: the C game and the board from power-on with the
  tests' player (an attract cycle, then a game to its end): all memory,
  the latches, the sound chips' registers and the frame's sound equal at
  every frame

## 5. For the sibling ports

- The skeleton came from Galaga's P0 commit with its later scaler and
  main-loop changes (scale.c; sleep when no step is due). The loop's rate
  is now a constant from the board's clocks (`FRAME_HZ`, 59.19 Hz) rather
  than 60: worth a look where a board is not at 60 Hz.

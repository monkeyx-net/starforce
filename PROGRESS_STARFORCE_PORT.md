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

Confirmed in E1 (`mame_emu`: both Z80s instruction for instruction, cycles
and registers, over 400 frames from power-on and over the traced frames of
play; all RAM, the video RAM and registers and every pixel at 16 captured
frames, power-on to over a minute of play; `mame_capture`):
- Memory maps (probe of MAME's address maps): main ROM $0000-$7FFF, RAM
  $8000-$8FFF, fg codes $9000-$93FF and colours $9400-$97FF, sprites
  $9800-$987F, palette $9C00-$9DFF, registers $9E00-$9E3F, bg3 $A000, bg2
  $A800, bg1 $B000 (2 KB each), radar $B800-$BBFF; reads $D000 P1, $D001
  P2, $D002 SYSTEM, $D004 DSW1, $D005 DSW2; writes $D000 the flip (bit 0),
  $D002 the IRQ's acknowledge, $D004 the sound latch. Sub ROM $0000-$1FFF,
  RAM $4000-$43FF, writes $8000, $9000, $A000 (the SN76489As), $D000,
  $E000 (E2); I/O $00-$03 the PIO (bit 0 control, bit 1 port B), $08-$0B
  the CTC. Unmapped reads give 0.
- Inputs (MAME's ports, active high): P1 and P2 bit 0 right, 1 left, 2 up,
  3 down, 4 fire; SYSTEM bit 0 coin 1, 1 coin 2, 2 start 1, 3 start 2.
  MAME's coins are pulses: the bit reads set for 2 frames whatever the
  hold. DSW1 default $C0, DSW2 $00.
- Power-on: time 0 is line 240 (the vertical blank, where frames start).
  Main runs from $0000 (IM 1); its IRQ is raised at each vertical blank
  and held until $D002 is written (the handler writes it at line ~260).
- The sub (IM 2, I = $02, vectors at $0200): the PIO's port A in mode 1
  (input) with interrupts on, vector $00 ($00B3, reads the latch); port B
  output, no interrupts. The CTC's vector base $08: channel 0 timer mode,
  prescale 16, time constant 8 (128 cycles); channel 1 counter mode, time
  constant 176, counting channel 0's zero counts, interrupting (vector $0A,
  $00D0: the sound driver's tick, every 22528 sub cycles, 88.8 Hz);
  channels 2 and 3 timer mode, prescale 256, waiting; the driver reloads
  channel 2 with prescale 16 and a time constant per note (its zero
  counts step the DAC). A timer channel's first zero count comes a period
  after the time constant's write. Daisy chain: the PIO, then the CTC.
- The sound latch (probes writing $D004 from Lua): every write raises the
  PIO's interrupt once the sub has caught up (MAME synchronizes it),
  except the first write since power-on; the PIO's interrupt is pending
  from power-on (taken at the sub's first EI). Writes before the
  acknowledge make one interrupt; the sub reads the latch's latest value.
- MAME's scheduling, which the board copies (as Galaga's): attoseconds
  (main's cycle 250000000000, sub's 500000000000, the pixel
  166666666666), slices of 1/60 s (the driver sets no quantum) cut by the
  timers: the vertical blank (line 240), line 0, the vertical blank's end
  (line 16), the 50 Hz sound update, each running CTC channel's zero
  count, and the syncs ($D002, $D004, the sub's interrupt line changes,
  its acknowledges and RETIs included).
- Video (`gfx/starforce.c`; probes poking the video RAM with main parked,
  then `mame_capture` and `mame_emu` on the game's frames): MAME's 256x224
  turned: screen x = 239 - native y, screen y = native x; native lines
  16-239 shown.
  - The palette: a byte a colour, bits 0-1 red, 2-3 green, 4-5 blue, 6-7
    intensity: a channel is (c x 4 + i) x 17, or 0 where c is 0.
  - Back to front: the backdrop (palette 0), the stripes, the sprites of
    priority 0, bg3, sprites 1, bg2, sprites 2, bg1, sprites 3, the fg
    layer. Pen 0 is transparent everywhere.
  - Graphics: three bitplanes, a third of the region each, the last third
    bit 0; rows top to bottom, bit 7 the left pixel; 16x16 tiles four 8x8
    quarters (top left, top right, bottom left, bottom right).
  - fg: 32x32 tiles of 8x8, cell = row x 32 + column; colour RAM bits
    0-2 the colour (palette colour x 8 + pen), bit 4 the code's ninth
    bit, bit 7 flips the tile top to bottom (bits 3, 5, 6 do nothing
    seen); each column scrolled up by its byte at $9E00-$9E1F.
  - bg1/bg2/bg3: 16 tiles across by 128 down of 16x16 (256 x 2048), a
    byte a tile; bg2 and bg3 colour bits 5-7, bg1 bit 7 then bits 5 and
    6 (colour = bits 5-6 x 2 + bit 7); bg3's tile the low 7 bits;
    palettes 64, 128, 192. bg1 and bg2 both scroll by $9E30-$9E31 (down,
    11 bits) and $9E35 (across); bg3 by $9E20-$9E21 and $9E25. The other
    bytes do nothing seen.
  - Sprites: 32 of 4 bytes: code; bits 0-2 colour (palette 320), 4-5
    priority, 6 flips across, 7 down (native); 256 - native y - size;
    native x. Codes $C0-$FF are 32x32: four 16x16 from (code & $7F) x 4.
    The first sprite on top.
  - The stripes (Senjyo's): unless $9E27 is $FF, palette 384-399 in
    stripes across the native x, colour (x x 16 / max($9E27 + 1, 16)) &
    15. Star Force never writes $9E27 and leaves palette 384-399 black.
  - The radar RAM ($B800) draws yellow pixels in MAME; Star Force never
    addresses it (not drawn).
  - $D000 set turns the whole native picture round (x, y to 255 - x, 255
    - y), the sprites 2 native lines further down.

### 2.1 Sound (E2, measured from MAME's output)

`mame_emu` checks the board's sound against MAME's recording of the
scripted run (`game.wav` from `mame_capture`): in 1/10 s windows where
MAME is not quiet (after the first second), all 448 within 3 dB of its
level (the test asks 90%; mean 0.01 dB, the worst 1.6 dB; the waveforms
correlate at 0.85, MAME's resampler 4 samples behind). Measured with
probes writing the chips from Lua, both CPUs parked (`emu/sound.c`):
- An SN76489A (a chip at the sub's $8000, $9000, $A000): a channel gives
  0 or 4095 x 10^(-v / 10) in MAME's output (2 dB steps, 15 silent),
  unipolar; tone 0 period $100 plays 244 Hz (2 MHz / 32 / N). The noise:
  white or periodic, at the period's rate. At power-on every channel is
  loud (MAME's output clips at 32767) until the sound driver quiets them.
- The DAC: each zero count of the CTC's channel 2 puts the next of the
  first 16 bytes of `07b.bin` on it (16 steps a cycle; bytes 16-31 are 0);
  it gives (v / 256 x 2 - 1) x 1638 times the low nibble of the sub's
  $D000 / 15. It holds 0 until stepped; the volume is full at power-on,
  so MAME's output sits at -1638 until the driver writes $D000 (the driver
  writes $F0-$FF, PSG-style). $E000 is written $FF once (no effect
  heard).
- The sum, sampled at 48 kHz (each sample the mean of the chips' 125 kHz
  ticks in it), clipped to 16 bits.

### 2.2 The ROM set's data (X1, `extract`)

- `starforce --extract` writes `gfx/fg.txt` (the 512 fg tiles, pens 0-7
  in the board's orientation), `gfx/bg1.txt`, `bg2.txt` (256 tiles of
  16x16 each), `bg3.txt` (128), `gfx/sprites.txt` (512 16x16 units),
  `gfx/font.txt` (the messages' ASCII: digits $30-$39, A-Z $41-$5A, a
  space $20; the status line's own: PLAYER $0C-$11, HI-SCO $12-$17, the
  digits 0-7 $18-$1F; its 8 and 9 are $20 and $21, $20 also the
  messages' space, hidden by their colours), `sound/wave.txt` (`07b.bin`),
  `tables.txt` (the two programs whole: main, then sub at +$8000), the
  previews (the tile sets, and the attract mode's title and demo screens
  from the emulated board) and `manifest.txt` last. `extract_load_gfx`
  reads the graphics back for the game in C. Development builds extract
  to `build/assets` after each build; the program extracts on its first
  run with a ROM set (to the save folder when its own is read-only).

### 2.3 The programs (C0-C3, `src/sim/`)

Read from MAME's disassembly (`build/re`, never committed) and the board's
executed instructions (`build/re/tools/cover.c`: sessions with varied
switches and inputs, every PC and every computed jump's target). Each
program is ported whole, as one resumable C function per CPU, every
instruction a case of its switch (`sim/main_run.c`, `sub_run.c`):
drafted by Galaga's translator, adapted (`build/re/tools/z80prog.py`,
never committed), on `sim/cpu.h`, then read. The code is everything
reachable from the reset and interrupt entries, the jump tables and the
PCs the board ran (main 7801 instructions, the sub 682); an instruction
counts its cycles and fetches as the Z80 does, knows the machine cycles
of its accesses (where a slice can stop it) and sets F whole. The jump
tables (`jp (hl)` after `rst $08` / `rst $18`): main's $5051 (32, the
objects' handlers by type, at $1F52 and $2269), $5085 (8, $2289), $5095
(4, $22C3), $509D (11, types $0F-$19, $1572); the sub's $04B9 (16, the
sound commands' handlers, $04B8).
- main: reset $0000 (IM 1) -> $03DF: RAM $8000-$8FFF cleared, $D004
  written 0, SP $9000, EI; a frame waited ($03C6: bit 6 of $8000, which
  the IRQ sets), then the set-up ($0409, $04A8, $0522) and the
  foreground loop ($0401: $3D1D, $091E). IRQ ($0038): the alternate
  registers, IX and IY kept at $8005; the first time, the palette from
  $8E14 to $9C00 (256 bytes) and $8F14 to $9D40 (64), the coinage from
  DSW1 ($0378); then $006A, $1D5F, $0268 (the acknowledge at $D002, the
  controls from $D000 or $D001 by the player up on the cocktail table,
  SYSTEM, DSW1 and DSW2 into $8013-$8016), $02E5, $034D (a sound
  command to $D004). Objects are 20-byte slots, their type in byte 1's
  bits 0-4. Player one's ships at $8166.
- sub: reset $0000 -> $0050: RAM $4000-$43FF cleared, $0640, IM 2 with I
  = $02, the PIO and the CTC set up, the chips quieted ($0025), EI; its
  loop at $0210 starts a new command ($4000 bit 0, set by the PIO's
  interrupt at $00B3, which reads the latch into $4001) and plays ten
  channels of 32 bytes from $4003, writing each to its chip ($8000,
  $9000, $A000; the DAC through $D000, its volume PSG-style, its pitch
  as channel 2's time constant at $042F). The CTC's channel 1 interrupt
  ($00D0) counts the notes' lengths down.

The C game (`sim/sim.c`) runs the two on the board's scheduler and devices
as `emu/board.c` has them, the abort of a slice inside an interrupt's
acknowledge included; `frame` checks it against the board at every frame.

To confirm by probe (written up here as confirmed):
- **High scores:** a table with initials in RAM.
- **The cocktail flip** on MAME's cocktail run (the flip's tile and
  sprite placement is probed, not yet checked on the game's frames).

## 3. Phases

| Phase | Work | Exit | Status |
|---|---|---|---|
| P0 | `git init`, skeleton from Galaga, loader for the starforc set, test card, MAME ROM map, packaging, CI, `CLAUDE.md`, README | `enhance`, `rom_load`, `mame_rom_map` pass | done 2026-10-25 |
| E1 | Two Z80s, the PIO and CTC, board (memory maps, latches, interrupts, watchdog), video (fg, three bg layers, sprites, palette RAM); the game plays via `--emulate` (silent) | `z80`; `mame_emu`: both CPUs cycle for cycle over traced frames; RAM and pictures equal at captured frames (`mame_capture`) | done 2026-10-25 (no watchdog found) |
| E2 | Three SN76489As, the DAC (waveform from `07b.bin`, stepped by the CTC), the volume filter | each voice exact; the mix's level and timing as MAME's wav | done 2026-10-25 |
| X1 | Extractor: fg and bg tiles, sprites, font, DAC waveform, tables | `extract` | done 2026-10-25 |
| C0-C3 | RE notes; the C game (both CPUs' programs, the PIO and CTC, the board's scheduler), then the default | `frame`; the C game plays from power-on through game over | done 2026-10-25: the default, equal to the board every frame (memory, sound) from power-on through games, five switch settings and 40000 frames of the later areas |
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
  writes fall, known flag results, NMI, IM 1, HALT (from Galaga; IM 2
  and the daisy chain are checked by `mame_emu`)
- `mame_capture` - label `mame`: a scripted game in MAME
  (`src/tests/mame/script.cmake`: power-on, the attract mode, a coin, a
  start, over a minute of play), dumping RAM, video RAM, sprites, the
  palette, the registers, the sub's RAM and the flip with a snapshot at
  16 frames; each frame drawn from the dump equals MAME's snapshot
- `mame_emu` - label `mame`: the board replays the script from power-on:
  MAME's traces of frames 1700-1800, both CPUs instruction for
  instruction (cycle and registers); at every captured frame all RAM
  (the sub's too), the video RAM and registers, the flip and the picture
  equal MAME's; the sound against MAME's recording of the run
  (`game.wav`): in 1/10 s windows where MAME is not quiet, at least 90%
  within 3 dB of its level
- `extract` - label `rom`: two extractions are byte-identical and hold
  every file; the graphics, waveform and tables read back are the ROM
  set's; the font reads "GAME OVER", "HI-SCORE", "PLAYER1" and "INSERT
  COIN" off the attract mode the emulated board draws, each letter its
  tile's pixels in its colour; no manifest without a ROM set
- `frame` - label `rom`: the C game and the board from power-on with the
  tests' player (`tests/player.c`: an attract cycle, then games to their
  ends, one player, two, the second coin slot; 12000 frames): all
  memory, the flip and the frame's sound equal at every frame
- `frame_long` - label `rom`: the same for 40000 frames with player one's
  ships kept at 3 in both, through the later areas

## 5. For the sibling ports

- The skeleton came from Galaga's P0 commit with its later scaler and
  main-loop changes (scale.c; sleep when no step is due). The loop's rate
  is now a constant from the board's clocks (`FRAME_HZ`, 59.19 Hz) rather
  than 60: worth a look where a board is not at 60 Hz.

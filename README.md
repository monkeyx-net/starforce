# Star Force - native SDL2 port (work in progress)

A C11 / SDL2 port of the 1984 Tehkan arcade game for Linux handhelds
(PortMaster: ArkOS, ROCKNIX, muOS, Knulli), desktops (Linux, Windows, macOS) and the
web, checked against the original ROM running in MAME.

**Status:** the game runs in C, equal frame for frame (pictures and sound)
to an emulated Star Force board that is itself checked against MAME.
Next: the DIP switches' menu, high scores, testing on devices and a
release. See [PROGRESS_STARFORCE_PORT.md](PROGRESS_STARFORCE_PORT.md).

You need your own MAME `starforc` ROM set (`starforc.zip`, the parent
set); none is included. The first run extracts what the game needs from
it to `assets/` next to the program.

Converted by monkeyx.

```bash
cmake -S . -B build && cmake --build build -j
build/starforce --windowed --rom /path/to/starforc.zip
```

## Controls

Two players, each an 8-way stick and a fire button. With two pads the
first is player one and the second player two; with one pad it (and the
keyboard) drives both, the players taking turns.

| Action | Pad | Keyboard |
|---|---|---|
| Move | left stick or D-pad | arrows |
| Fire | A or B | Space or Ctrl |
| Coin | Select | 5 (6: the second slot) |
| 1-player start | Start | 1 or Enter |
| 2-player start | L1 + Start | 2 |
| Next graphics preset | Select + L1 | F3 |
| Glow on / off | | F4 |
| CRT scanlines on / off | | F5 |
| Quit | hold Select + Start for 1 s | Esc |

## Building

Needs CMake 3.13+, a C11 compiler and SDL 2.0.10+.

```bash
ctest --test-dir build              # tests (ROM and MAME tests need roms/starforc.zip and mame)
cmake --build build --target package_linux package_portmaster
emcmake cmake -S . -B build-web && cmake --build build-web --target serve
```

## License

Apache 2.0 for the source; see [NOTICE](NOTICE) for the third-party material
this project refers to. No ROM data is included or distributed.

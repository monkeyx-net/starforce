Star Force (native SDL2 build)
==============================

Converted by monkeyx.

Work in progress. Run ./starforce, or starforce.exe on Windows (add --windowed
for a window on a desktop). The game is rewritten in C, checked frame by
frame against an emulated Star Force board that is itself checked against
MAME (./starforce --emulate runs that board; --test-card the test card).

ROM set
  The game needs your own MAME starforc ROM set (the parent set, not a
  clone). Put starforc.zip (or a starforc/ folder) next to the program,
  set STARFORCE_ROM to its path, or start with --rom /path/to/starforc.zip.
  FBNeo sets and old file names work too: ROMs are matched by checksum.
  The first run extracts what the game needs from it into assets/ next to
  the program (or the save folder when that is read-only); after that the
  ROM set is not needed. Check a set with:
  ./starforce --check-rom starforc.zip

Controls (two players: an 8-way stick and fire each)
  Move: left stick / D-pad; keyboard arrows
  Fire: A or B; keyboard Space or Ctrl
  Coin: Select / 5 (6: the second slot)
  Start: Start / 1 or Enter;  2-player start: L1+Start / 2
  Graphics: Select+L1 / F3 steps the presets, F4 glow, F5 CRT scanlines
  Operator menu (the DIP switches): Select+R1 / F2
  Exit: hold Select+Start for 1 second / Esc
  With two pads the first is player one and the second player two; with
  one pad it drives both (the players take turns).

Settings and scores
  The DIP switches (coinage, ships, bonus ship, difficulty, demo sounds,
  cabinet) are in starforce.cfg, changed with the operator menu; the BEST
  PLAYERS table is kept in starforce.hiscore. Both live in the save folder:
  $STARFORCE_SAVE_DIR, else ~/.local/share/starforce (%APPDATA%\starforce
  on Windows); the PortMaster port keeps them in its conf/ folder.

Graphics presets (--gfx <preset>)
  classic, classic-crt, crisp, smooth, hd-crisp, hd-smooth, hd-glow, hd-glow-crt

See NOTICE for the third-party material this project refers to.

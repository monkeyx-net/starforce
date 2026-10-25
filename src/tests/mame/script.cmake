# The scripted run the MAME tests capture and test_emu replays (inputs in
# capture_probe.lua's PROBE_SCRIPT format): power-on, the attract mode, a
# coin, a 1-player start, then over a minute of play, moving the ship
# about and firing.
set(capture_script "900:coin 910: 1000:start 1010:")
set(moves "left,fire" "up,fire" "right" "fire" "down,left" "right,up,fire" "fire" "down" "left,up" "right,down,fire")
set(f 1200)
set(k 0)
while(f LESS 5200)
  math(EXPR m "${k} % 10")
  list(GET moves ${m} move)
  # hold each move a while, then let go
  math(EXPR hold "15 + (${k} * 7) % 25")
  math(EXPR off "${f} + ${hold}")
  string(APPEND capture_script " ${f}:${move} ${off}:")
  math(EXPR f "${off} + 5")
  math(EXPR k "${k} + 1")
endwhile()
# frames dumped and snapshotted (the picture is drawn at the vertical
# blank from what memory holds then, so every one is checked)
set(capture_frames 300 600 950 1100 1300 1500 1700 2000 2400 2800 3200 3600 4000 4400 4800 5200)
# frames whose instructions are traced on both CPUs: play
set(trace_from 1700)
set(trace_to 1800)

# The cocktail run (mame_cocktail): the Cabinet switch on cocktail, two
# coins, a 2-player start, no play: the turns change as the ships are lost,
# player two's ($D000 set) drawn turned round
set(cocktail_script "900:coin 901: 930:coin 931: 1000:start2 1010:")
set(cocktail_fields "Cabinet=0")
set(cocktail_frames 1700 1900 2200 2500 2700 3300 3500 3900 4500 4800 5100)

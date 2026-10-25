# A scripted MAME run, dumped and snapshotted at chosen frames; the checks:
#   - mame_capture: the picture drawn from the dumped memory (gfx/starforce.c)
#     against MAME's own frames
#   - mame_emu (emu.cmake) reuses the run: the emulated board against all of
#     it, memory and pictures at every captured frame, and the sound (MAME's
#     recording of the run, game.wav)
#   - mame_cocktail (-DRUN=cocktail): the cocktail run (script.cmake), its
#     pictures only, player two's turned round
#   cmake -DMAME=... -DROMPATH=... -DWORKDIR=... -DROM=... -DPROBE=capture_probe.lua -DCHECK=test_capture [-DRUN=cocktail] -P capture.cmake
foreach(v MAME ROMPATH WORKDIR ROM PROBE CHECK)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "capture.cmake: ${v} not set")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/mame_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/script.cmake")

set(out "${WORKDIR}/capture")
set(wav -wavwrite "${out}/game.wav")
if(RUN STREQUAL "cocktail")
  set(out "${WORKDIR}/capture_cocktail")
  set(wav)
  set(capture_script "${cocktail_script}")
  set(capture_frames ${cocktail_frames})
  set(ENV{PROBE_FIELDS} "${cocktail_fields}")
endif()
file(REMOVE_RECURSE "${out}" "${WORKDIR}/snap")
file(MAKE_DIRECTORY "${out}")
set(ENV{PROBE_OUT} "${out}")
string(REPLACE ";" " " frame_list "${capture_frames}")
set(ENV{PROBE_FRAMES} "${frame_list}")
list(GET capture_frames -1 last)
set(ENV{PROBE_LAST} ${last})
set(ENV{PROBE_SCRIPT} "${capture_script}")
execute_process(COMMAND "${MAME}" ${mame_args} -seconds_to_run 300 -samplerate 48000 ${wav}
                  -autoboot_script "${PROBE}"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "MAME exited with ${rc}")
endif()

# MAME numbers its snapshots in order: name them by frame
set(i 0)
foreach(f ${capture_frames})
  string(LENGTH "${i}" digits)
  math(EXPR pad "4 - ${digits}")
  string(REPEAT "0" ${pad} zeros)
  set(snap "${WORKDIR}/snap/starforc/${zeros}${i}.png")
  if(NOT EXISTS "${snap}" OR NOT EXISTS "${out}/frame_${f}.txt")
    message(FATAL_ERROR "frame ${f}: no capture")
  endif()
  file(RENAME "${snap}" "${out}/${f}.png")
  math(EXPR i "${i} + 1")
endforeach()

foreach(f ${capture_frames})
  execute_process(COMMAND "${CHECK}" "${ROM}" "${out}/frame_${f}.txt" "${out}/${f}.png" ${f} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "frame ${f}: the port's drawing differs from MAME's snapshot")
  endif()
endforeach()

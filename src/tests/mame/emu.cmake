# The emulated board against MAME: runs after mame_capture and replays its
# scripted run from power-on. Checked (test_emu):
#   - instruction traces of MAME's frames trace_from-trace_to, taken here a
#     CPU a run: every CPU cycle for cycle
#   - at every captured frame memory, the latches and the picture
#   cmake -DMAME=... -DROMPATH=... -DWORKDIR=... -DROM=... -DPROBE=trace_probe.lua -DCHECK=test_emu -P emu.cmake
foreach(v MAME ROMPATH WORKDIR ROM PROBE CHECK)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "emu.cmake: ${v} not set")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/script.cmake")
foreach(f ${capture_frames})
  if(NOT EXISTS "${WORKDIR}/capture/frame_${f}.txt" OR NOT EXISTS "${WORKDIR}/capture/${f}.png")
    message(FATAL_ERROR "${WORKDIR}/capture: frame ${f} missing: run mame_capture first")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/mame_common.cmake")

set(traces "${WORKDIR}/trace")
file(REMOVE_RECURSE "${traces}")
file(MAKE_DIRECTORY "${traces}")
set(ENV{PROBE_OUT} "${traces}")
set(ENV{TRACE_FROM} ${trace_from})
set(ENV{TRACE_TO} ${trace_to})
set(ENV{PROBE_SCRIPT} "${capture_script}")
foreach(cpu maincpu sub)
  set(ENV{TRACE_CPU} ${cpu})
  execute_process(COMMAND "${MAME}" ${mame_args} -debug -debugger none -seconds_to_run 300 -autoboot_script "${PROBE}"
                  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
  if(NOT rc EQUAL 0 OR NOT EXISTS "${traces}/${cpu}.tr")
    message(FATAL_ERROR "MAME exited with ${rc} and no ${cpu} trace")
  endif()
endforeach()

execute_process(COMMAND "${CHECK}" "${ROM}" "${capture_script}" "${WORKDIR}/capture" "${traces}" ${trace_from} ${trace_to}
                  ${capture_frames}
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the emulated board differs from MAME")
endif()

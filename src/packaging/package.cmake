# Lay out and archive a release.
#   cmake -DKIND=linux|portmaster|windows|macos -DSRC=<repo> -DBIN=<build dir> -DGAME=<binary>
#         -DARCH=<x86_64|aarch64|arm64> -DVERSION=<x.y.z> -P package.cmake
# Archives land in <build dir>/packages/. Windows packages also take every DLL
# next to the binary (copy SDL2.dll there first). No ROM data is packaged:
# players supply their own starforc.zip.
foreach(v KIND SRC BIN GAME ARCH VERSION)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "package.cmake: ${v} not set")
  endif()
endforeach()

if(ARCH MATCHES "^(AMD64|)$")
  set(ARCH x86_64)  # Windows calls it AMD64; MinGW cross builds leave it empty
endif()

set(out "${BIN}/packages")
set(stage "${out}/stage-${KIND}-${ARCH}")
file(REMOVE_RECURSE "${stage}")

if(KIND STREQUAL "linux")
  set(root "${stage}/starforce-${VERSION}-linux-${ARCH}")
  set(exe_name "starforce")
elseif(KIND STREQUAL "macos")
  set(root "${stage}/starforce-${VERSION}-macos-${ARCH}")
  set(exe_name "starforce")
elseif(KIND STREQUAL "windows")
  set(root "${stage}/starforce-${VERSION}-windows-${ARCH}")
  set(exe_name "starforce.exe")
else()
  set(root "${stage}/starforce")
  set(exe_name "starforce.${ARCH}")
endif()

file(MAKE_DIRECTORY "${root}")
file(COPY "${GAME}" DESTINATION "${root}")
get_filename_component(game_name "${GAME}" NAME)
file(RENAME "${root}/${game_name}" "${root}/${exe_name}.tmp")
file(RENAME "${root}/${exe_name}.tmp" "${root}/${exe_name}")
if(KIND STREQUAL "windows")
  get_filename_component(game_dir "${GAME}" DIRECTORY)
  file(GLOB dlls "${game_dir}/*.dll")
  if(NOT dlls)
    message(FATAL_ERROR "no DLLs next to ${GAME}: copy SDL2.dll there")
  endif()
  file(COPY ${dlls} DESTINATION "${root}")
endif()
file(COPY "${SRC}/NOTICE" "${SRC}/LICENSE" DESTINATION "${root}")
file(COPY "${SRC}/src/packaging/README-native.txt" DESTINATION "${root}")

if(KIND STREQUAL "linux" OR KIND STREQUAL "macos")
  set(archive "${out}/starforce-${VERSION}-${KIND}-${ARCH}.tar.gz")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${archive}" "starforce-${VERSION}-${KIND}-${ARCH}"
                  WORKING_DIRECTORY "${stage}" RESULT_VARIABLE rc)
elseif(KIND STREQUAL "windows")
  set(archive "${out}/starforce-${VERSION}-windows-${ARCH}.zip")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip "starforce-${VERSION}-windows-${ARCH}"
                  WORKING_DIRECTORY "${stage}" RESULT_VARIABLE rc)
else()
  file(COPY "${SRC}/src/packaging/portmaster/StarForce.sh" DESTINATION "${stage}")
  file(COPY "${SRC}/src/packaging/portmaster/port.json" "${SRC}/src/packaging/portmaster/gameinfo.xml"
       DESTINATION "${root}")
  # screenshot: the test card, rendered headless and without a ROM (no local paths)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy
                  "${GAME}" --rom "" --frames 60 --screenshot "${root}/screenshot.png"
                  RESULT_VARIABLE shot_rc OUTPUT_QUIET ERROR_QUIET)
  if(NOT shot_rc EQUAL 0)
    message(WARNING "could not render the PortMaster screenshot")
  endif()
  set(archive "${out}/starforce-${VERSION}-portmaster-${ARCH}.zip")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip "StarForce.sh" starforce
                  WORKING_DIRECTORY "${stage}" RESULT_VARIABLE rc)
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "archiving failed")
endif()
file(REMOVE_RECURSE "${stage}")
message(STATUS "wrote ${archive}")

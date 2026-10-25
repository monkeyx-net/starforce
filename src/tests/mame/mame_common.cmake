# Shared by the MAME tests: the options. Needs MAME, ROMPATH (the dir holding
# starforc.zip) and WORKDIR.
file(MAKE_DIRECTORY "${WORKDIR}")
# MAME keeps the DIP switches it was last given in its cfg folder: every
# test starts without one
file(REMOVE_RECURSE "${WORKDIR}/cfg")
set(mame_args starforc -rompath "${ROMPATH}" -cfg_directory "${WORKDIR}/cfg" -nvram_directory "${WORKDIR}/nvram"
    -snapshot_directory "${WORKDIR}/snap" -snapview native
    # every frame rendered, so a snapshot shows the frame it is taken in
    -video none -sound none -nothrottle -frameskip 0 -noplugin data -skip_gameinfo)

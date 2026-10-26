# cmake -DROOT=CHECKOUT -DDEST=WORK/port/src -DPROTOCOL=GENERATED -DICON=FILE -P PortSources.cmake: the port's
# sources as the runner's build reads them (ModernGekko's MODERNGEKKO_FZGX_PORT_DIR), mirrored from the checkout:
#   DEST/core, DEST/moderngekko   port/core and port/moderngekko, without __pycache__, tests and patches
#   DEST/facts/generated          port/facts/generated (the adapter's addresses: facts.h)
#   DEST/protocol/generated       the protocol's generated code (pins.tsv REPO_PROTOCOL; the status board's layout)
#   DEST/app/app.ico              the launcher's icon (pins.tsv REPO_LAUNCHER; runner.rc), when it is there
# Only files whose bytes changed are written, so the runner's build compiles only those again; files that are no
# longer in the checkout are removed (port.cmake globs its sources).
cmake_minimum_required(VERSION 3.25)

function(mirror from to)
  file(GLOB_RECURSE have LIST_DIRECTORIES false RELATIVE "${from}" "${from}/*")
  list(FILTER have EXCLUDE REGEX "(^|/)(__pycache__|tests|patches)(/|$)")
  foreach(f IN LISTS have)
    get_filename_component(dir "${to}/${f}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")   # COPY_FILE makes no folders: a fresh work folder has none
    file(COPY_FILE "${from}/${f}" "${to}/${f}" ONLY_IF_DIFFERENT)
  endforeach()
  if(EXISTS "${to}")
    file(GLOB_RECURSE there LIST_DIRECTORIES false RELATIVE "${to}" "${to}/*")
    foreach(f IN LISTS there)
      if(NOT f IN_LIST have)
        file(REMOVE "${to}/${f}")
      endif()
    endforeach()
  endif()
endfunction()

foreach(part IN ITEMS core moderngekko)
  file(MAKE_DIRECTORY "${DEST}/${part}")
  mirror("${ROOT}/port/${part}" "${DEST}/${part}")
endforeach()
file(MAKE_DIRECTORY "${DEST}/facts/generated" "${DEST}/protocol/generated" "${DEST}/app")
mirror("${ROOT}/port/facts/generated" "${DEST}/facts/generated")
mirror("${PROTOCOL}" "${DEST}/protocol/generated")
if(EXISTS "${ICON}")
  file(COPY_FILE "${ICON}" "${DEST}/app/app.ico" ONLY_IF_DIFFERENT)
endif()

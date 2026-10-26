# port_pin(NAME OUT): the value of port/setup/pins.tsv's row NAME (the one table of repository paths and tool
# versions; port/setup/lib.sh and tools.py read the same file). A missing row stops the configure.
set(PORT_PINS_FILE "${CMAKE_CURRENT_LIST_DIR}/../../setup/pins.tsv")
function(port_pin name out)
  file(STRINGS "${PORT_PINS_FILE}" rows REGEX "^${name}\t")
  if(NOT rows)
    message(FATAL_ERROR "port/setup/pins.tsv has no row ${name}")
  endif()
  list(GET rows 0 row)
  string(REGEX REPLACE "^${name}\t([^\t]*).*$" "\\1" value "${row}")
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

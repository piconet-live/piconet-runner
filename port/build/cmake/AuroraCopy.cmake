# AuroraCopy.cmake (cmake -DFROM=BUILD -DDEPS=FETCHED -DTO=RUNNER_FOLDER -P): the aurora target's install step. aurora-present.dll
# and the libraries it loads at run time (Dawn's webgpu_dawn.dll, SDL3.dll, the shader compiler's dxcompiler.dll and
# dxil.dll, and whatever else of the build's DLLs it links), copied beside the runner, which loads it from there.
file(GLOB_RECURSE dlls "${FROM}/*.dll" "${DEPS}/*-build/*.dll" "${DEPS}/dawn_prebuilt-src/*.dll")
list(FILTER dlls EXCLUDE REGEX "/CMakeFiles/")
set(seen "")
if(NOT EXISTS "${FROM}/aurora-present.dll")
  message(FATAL_ERROR "AuroraCopy: no aurora-present.dll in ${FROM}")
endif()
foreach(dll IN LISTS dlls)
  get_filename_component(name "${dll}" NAME)
  if(name IN_LIST seen)
    continue()
  endif()
  list(APPEND seen "${name}")
  message(STATUS "aurora: ${name} beside the runner")
  file(COPY_FILE "${dll}" "${TO}/${name}" ONLY_IF_DIFFERENT)
endforeach()

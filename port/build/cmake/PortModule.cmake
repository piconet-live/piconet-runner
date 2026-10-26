# cmake -DPARAMS=FILE -P PortModule.cmake: the native module (the superbuild's target `module`; PARAMS is the file
# CMakeLists.txt wrote at configure time). Into W/work/cg-N and W/work/mod-cg-N, both deleted first:
#   1. the recompiler's input, cg-N/main-combo.dol: the stock executable with the patch list's words, main.rel's
#      clean .text with its words, the image as a text section and the EXTRA sections (port/image/gameinput.py);
#      the bundle's patches.json, manifest.json and symbols.json beside it, what the module was built from
#   2. the recompiler (W/work/dolrecomp-port) with the switches file's DOLRECOMP_* settings, the profile and
#      --profile-generate, into cg-N/generated; its log is cg-N.log, the switches as given cg-N.env
#   3. the fast lookup tables (the recompiler's tools/fastlookup.py, with the same switches): without
#      DOLRECOMP_CLOSED_ROOTS no table of the "closed body may be entered" bytes is written, and a build with closed
#      roots whose native_tables.c does not set them is refused
#   4. the module template's CMake project (W/work/module-template), compiled with LLVM's clang into mod-cg-N; its
#      log is cg-N-mod.log
# cmake -DSTEP=compile -DW=WORK -DN=NAME -DLLVM=FOLDER -DJOBS=16 -P PortModule.cmake: step 4 alone, for code that is
# in cg-N/generated already (port/speed/hybrid-module.sh)
cmake_minimum_required(VERSION 3.25)
if(DEFINED PARAMS)
  include("${PARAMS}")
endif()
set(CG "${W}/work/cg-${N}")
set(MOD "${W}/work/mod-cg-${N}")
set(G "${CG}/generated")
file(TO_NATIVE_PATH "${CG}" CG_NATIVE)

function(run log)   # run LOG COMMAND...: the command's output appended to LOG (- : shown); stops at a failure
  execute_process(COMMAND ${ARGN} OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE failed)
  if(log STREQUAL "-")
    string(STRIP "${out}" out)
    message(STATUS "${out}")
  else()
    file(APPEND "${log}" "${out}")
  endif()
  if(failed)
    message(FATAL_ERROR "module: failed (${failed}): ${ARGN}; see ${log}")
  endif()
endfunction()

function(lf file)   # LF line ends, as fastlookup.py writes them on Linux (a Windows Python writes CRLF): the same files
  # on every platform. (Python, not file(WRITE): CMake writes text files with the platform's line ends. And not
  # through run(): the code's semicolons would split it into a list.)
  execute_process(COMMAND "${PY}" -c "import sys; p = sys.argv[1]; d = open(p, 'rb').read(); open(p, 'wb').write(d.replace(b'\\r\\n', b'\\n'))" "${file}"
                  RESULT_VARIABLE failed)
  if(failed)
    message(FATAL_ERROR "module: line ends of ${file} not made LF (${failed})")
  endif()
endfunction()

function(compile_module)   # step 4: cg-N/generated into mod-cg-N with the module template's project
  string(TIMESTAMP now "%H:%M:%S")
  message(STATUS "module ${N}: ${now}, the module's compile (log: ${CG}-mod.log)")
  set(MG "${W}/ModernGekko/vendor/dolphin")
  file(WRITE "${CG}-mod.log" "")
  run("${CG}-mod.log" "${CMAKE_COMMAND}" -E env CMAKE_NINJA_FORCE_RESPONSE_FILE=1
      "${CMAKE_COMMAND}" -S "${W}/work/module-template" -B "${MOD}" -G Ninja -DCMAKE_BUILD_TYPE=Release
      "-DCMAKE_C_COMPILER=${LLVM}/bin/clang.exe" -DGAME_ID=GFZE01 "-DCMAKE_SHARED_LINKER_FLAGS=-Xlinker /Brepro"
      "-DGENERATED_DIR=${G}" "-DGXRUNTIME_DIR=${MG}/GXRuntime" "-DCHASSIS_ABI_DIR=${MG}/Source/Core/Core/PowerPC/StaticRecomp")
  run("${CG}-mod.log" "${CMAKE_COMMAND}" --build "${MOD}" -j${JOBS})
  string(TIMESTAMP now "%H:%M:%S")
  file(SHA1 "${MOD}/gGFZE01_recomp.dll" sum)
  string(SUBSTRING "${sum}" 0 8 sum)
  message(STATUS "module ${N}: ${now}, ${MOD}/gGFZE01_recomp.dll (${sum})")
endfunction()

if(STEP STREQUAL "compile")
  compile_module()
  return()
endif()

string(TIMESTAMP now "%H:%M:%S")
message(STATUS "module ${N}: ${now}, the recompiler's input")
file(REMOVE_RECURSE "${CG}" "${MOD}")
file(MAKE_DIRECTORY "${CG}")
set(extra_args "")
foreach(e IN LISTS EXTRA)
  list(APPEND extra_args --extra "${e}")
endforeach()
run(- "${PY}" "${CMAKE_CURRENT_LIST_DIR}/../../image/gameinput.py" --dol "${W}/extracted/GFZE01/sys/main.dol.orig"
    --rel "${W}/derived/main.rel" --bundle "${BUNDLE}" --combined "${CG}/main-combo.dol" ${extra_args})
foreach(f IN ITEMS patches.json manifest.json symbols.json)
  file(COPY_FILE "${BUNDLE}/${f}" "${CG}/${f}")
endforeach()

# The switches file is one line, "env: set A=B&& set C=D&& ...": each value is what cmd's `set` gives it
file(READ "${SWITCHES}" envs)
string(REGEX REPLACE "^env: " "" envs "${envs}")
string(REGEX REPLACE "[\r\n]+$" "" envs "${envs}")
file(WRITE "${CG}.env" "env: ${envs}\n")
set(ENV{DOLRECOMP_LLVM_CACHE} "${W_NATIVE}\\llvm-cache")
set(ENV{DOLRECOMP_MEM2_BASE} "0xA0000000")
set(closed_roots OFF)
string(REPLACE "&&" ";" parts "${envs}")
foreach(p IN LISTS parts)
  string(STRIP "${p}" p)
  if(p MATCHES "^set ([^=]+)=(.*)$")
    set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
    if(CMAKE_MATCH_1 STREQUAL "DOLRECOMP_CLOSED_ROOTS" AND CMAKE_MATCH_2 MATCHES "^[0-9A-Fa-f]")
      set(closed_roots ON)
    endif()
  endif()
endforeach()

set(profile_args "")
if(PROFILE AND NOT PROFILE STREQUAL "none")
  file(TO_NATIVE_PATH "${PROFILE}" p)
  set(profile_args --profile-use "${p}")
endif()
if(PROFILE_GENERATE)
  list(APPEND profile_args --profile-generate "${PROFILE_GENERATE}")
endif()
string(TIMESTAMP now "%H:%M:%S")
message(STATUS "module ${N}: ${now}, the recompiler (log: ${CG}.log)")
file(TO_NATIVE_PATH "${RECOMPILER}" tool)
execute_process(COMMAND "${tool}" -j${JOBS} --backend=llvm --native-abi off ${profile_args} --cpu gekko --gamecube
                        "${CG_NATIVE}\\main-combo.dol" "${CG_NATIVE}"
                OUTPUT_FILE "${CG}.log" ERROR_FILE "${CG}.log" RESULT_VARIABLE failed)
if(failed)
  message(FATAL_ERROR "module: the recompiler failed (${failed}); see ${CG}.log")
endif()
file(COPY_FILE "${CG}/main-combo.dol" "${G}/main.dol")

# fastlookup.py reads the switches from its environment (set above)
run(- "${PY}" "${W}/DolRecomp/tools/fastlookup.py" "${G}/generated.h")
lf("${G}/generated.h")
lf("${G}/native_tables.c")
if(closed_roots)
  file(STRINGS "${G}/native_tables.c" closed REGEX "closed_flags\\[number\\]" LIMIT_COUNT 1)
  if(NOT closed)
    message(FATAL_ERROR "module: the build has closed roots and native_tables.c does not set the closed bytes")
  endif()
endif()
if(PROFILE_GENERATE)
  file(TOUCH "${G}/pgo-generate")
endif()

compile_module()

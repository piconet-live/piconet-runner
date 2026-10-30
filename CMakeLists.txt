# The runner's build: one CMake superbuild over the three CMake projects it is made of, configured by a preset per
# platform (CMakePresets.json; Windows today).
#
#   cmake --preset windows                       configure (a Developer environment: vcvars64, LLVM, Python, ninja)
#   cmake --build --preset windows               the recompiler and the runner
#   cmake --build --preset windows --target module   the native module (needs the work folder's disc inputs and a
#                                                profile, PORT_PROFILE=FILE or PORT_PROFILE=none)
#
# Why a superbuild (ExternalProject) and not add_subdirectory: the three projects are built with two compilers (MSVC
# for the recompiler and the runner, clang for the module), the module's CMake project can only be configured after
# the recompiler built here has written its sources, and the forks are built unchanged, as their own projects, at
# their pins (forks/upstream.tsv) and with the configure lines every earlier build used, so their outputs keep their
# bytes. Their sources are the work folder's trees (port/setup/trees.sh), on a Windows drive, as before.
#
# Targets:
#   recompiler   PORT_WORK/work/dolrecomp-port/dolrecomp.exe    (DolRecomp with the LLVM backend; part of all)
#   runner       PORT_WORK/ModernGekko/build/moderngekko-run.exe (ModernGekko with the port; part of all)
#   image        PORT_WORK/port/bundle-NAME (port/image/build.py; a Python with keystone, PORT_PYTHON)
#   module       PORT_WORK/work/mod-cg-NAME/gGFZE01_recomp.dll  (the recompiler's code, compiled with clang)
# NAME is PORT_BUILD_NAME. What follows a build (the game folder, the staged launcher, the build manifests, a PC's
# machine sharing) is port/build/build.sh's: port/build/README.md.
cmake_minimum_required(VERSION 3.25)
project(piconet_runner LANGUAGES NONE)
include(ExternalProject)

set(PORT_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/build/cmake")
include("${PORT_CMAKE_DIR}/PortPins.cmake")   # port_pin(): port/setup/pins.tsv's rows
port_pin(REPO_SWITCHES switches_pin)
port_pin(REPO_LAUNCHER launcher_pin)
port_pin(REPO_PROTOCOL protocol_pin)
port_pin(VERSION_LLVM llvm_version)

# ---- settings (a preset's cacheVariables, CMakeUserPresets.json or -D) -------------------------------------------
set(PORT_WORK "${CMAKE_CURRENT_SOURCE_DIR}/build/work" CACHE PATH   # port/setup/work-folder.env's default, which CMake cannot read
    "The work folder port/setup/setup.sh made (its trees, the disc's files), on a Windows drive")
set(PORT_BUILD_NAME "dev" CACHE STRING "The build's name: port/bundle-NAME, work/cg-NAME, work/mod-cg-NAME")
set(PORT_BUNDLE "" CACHE PATH "The image's bundle the module is built from (default: port/bundle-NAME, the image target's)")
set(PORT_PROFILE "" CACHE STRING "The module's training profile (a file), or none (the same game, slower)")
set(PORT_PROFILE_GENERATE "" CACHE STRING "An instrumented module: the recompiler's --profile-generate file pattern")
set(PORT_RECOMPILER "" CACHE FILEPATH "Another recompiler for the module (a dolrecomp.exe; default: the target recompiler's)")
set(PORT_SWITCHES "" CACHE FILEPATH "The recompiler's switches (default: pins.tsv REPO_SWITCHES, the released ones)")
set(PORT_EXTRA "" CACHE STRING "More text sections, ADDR:FILE;... (default: the movie player's two modules; none: none)")
set(PORT_PYTHON "" CACHE FILEPATH "A Python with the module keystone, for the image (default: Python3 found)")
set(PORT_LLVM_ROOT "" CACHE PATH "LLVM ${llvm_version} with lib/cmake/llvm (default: the folder above clang on PATH)")
set(PORT_SOURCE_COMMIT "" CACHE STRING "The checkout's commit as the image's manifest names it (default: git describe there)")
option(PORT_HOST_TIMERS "A profiling runner, with the core's scoped timers (never a player's)" OFF)
set(PORT_JOBS 16 CACHE STRING "Parallel jobs of each of the three builds")

file(TO_CMAKE_PATH "${PORT_WORK}" W)
if(NOT PORT_SWITCHES)
  set(PORT_SWITCHES "${CMAKE_CURRENT_SOURCE_DIR}/${switches_pin}")
endif()
if(PORT_EXTRA STREQUAL "none")
  set(PORT_EXTRA "")
elseif(NOT PORT_EXTRA)
  set(PORT_EXTRA "805960D4:${W}/derived/loaded-05-movie.bin;8059EF6C:${W}/derived/loaded-0c-movie_module.bin")
endif()
set(NAME "${PORT_BUILD_NAME}")
if(NOT NAME MATCHES "^[A-Za-z0-9]+$")
  message(FATAL_ERROR "PORT_BUILD_NAME is letters and digits (${NAME})")
endif()

if(NOT EXISTS "${W}/DolRecomp/CMakeLists.txt" OR NOT EXISTS "${W}/ModernGekko/CMakeLists.txt")
  message(FATAL_ERROR "No source trees in the work folder ${W}: port/setup/setup.sh makes one (or PORT_WORK=<a work "
                      "folder setup.sh made>). Its trees must be this checkout's pins (forks/check.sh --trees).")
endif()

# LLVM: the recompiler links it, the module is compiled with its clang
if(NOT PORT_LLVM_ROOT)
  find_program(PORT_CLANG clang)
  if(PORT_CLANG)
    get_filename_component(PORT_LLVM_ROOT "${PORT_CLANG}" DIRECTORY)
    get_filename_component(PORT_LLVM_ROOT "${PORT_LLVM_ROOT}" DIRECTORY)
  endif()
endif()
file(TO_CMAKE_PATH "${PORT_LLVM_ROOT}" LLVM)
if(NOT EXISTS "${LLVM}/lib/cmake/llvm/LLVMConfig.cmake")
  message(FATAL_ERROR "No LLVM ${llvm_version} with lib/cmake/llvm (clang+llvm-${llvm_version}-x86_64-pc-windows-msvc, "
                      "github.com/llvm/llvm-project releases): put its bin on PATH or set PORT_LLVM_ROOT (now '${LLVM}')")
endif()
set(EXE "")
if(CMAKE_HOST_WIN32)
  set(EXE ".exe")
endif()

# ---- the recompiler: DolRecomp, from the work folder's tree ------------------------------------------------------
# BUILD_ALWAYS: ninja inside decides what is out of date (a tree moved to another pin rebuilds what changed).
set(RECOMPILER "${W}/work/dolrecomp-port/dolrecomp${EXE}")
set(MODULE_RECOMPILER "${RECOMPILER}")
if(PORT_RECOMPILER)
  file(TO_CMAKE_PATH "${PORT_RECOMPILER}" MODULE_RECOMPILER)
endif()
ExternalProject_Add(recompiler
  PREFIX "${CMAKE_CURRENT_BINARY_DIR}/ep"
  SOURCE_DIR "${W}/DolRecomp"
  BINARY_DIR "${W}/work/dolrecomp-port"
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDOLRECOMP_ENABLE_LLVM=ON
             -DCMAKE_EXE_LINKER_FLAGS=/Brepro -DLLVM_DIR=${LLVM}/lib/cmake/llvm
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target dolrecomp -j${PORT_JOBS}
  BUILD_BYPRODUCTS "${RECOMPILER}"
  INSTALL_COMMAND ""
  BUILD_ALWAYS ON
  USES_TERMINAL_CONFIGURE ON
  USES_TERMINAL_BUILD ON)

# ---- the runner: ModernGekko with the port ------------------------------------------------------------------------
# The port's sources are mirrored into PORT_WORK/port/src first (only changed files are written, so only those are
# compiled again), where the Windows compiler reads them whatever drive the checkout is on.
get_filename_component(LAUNCHER_CHECKOUT "${CMAKE_CURRENT_SOURCE_DIR}/${launcher_pin}" ABSOLUTE)
set(PORT_ICON "${LAUNCHER_CHECKOUT}/net/src/Launcher.App/Assets/Icon/app.ico")
if(NOT EXISTS "${PORT_ICON}")
  message(WARNING "No launcher checkout at ${LAUNCHER_CHECKOUT} (pins.tsv REPO_LAUNCHER): the runner is built without its icon")
endif()
add_custom_target(port_sources
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -DDEST=${W}/port/src
          -DPROTOCOL=${CMAKE_CURRENT_SOURCE_DIR}/${protocol_pin}/protocol/generated -DICON=${PORT_ICON}
          -P "${PORT_CMAKE_DIR}/PortSources.cmake"
  COMMENT "The port's sources, mirrored into ${W}/port/src"
  VERBATIM)
if(PORT_HOST_TIMERS)
  set(timers ON)
else()
  set(timers OFF)
endif()
ExternalProject_Add(runner
  PREFIX "${CMAKE_CURRENT_BINARY_DIR}/ep"
  SOURCE_DIR "${W}/ModernGekko"
  BINARY_DIR "${W}/ModernGekko/build"
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "-DCMAKE_EXE_LINKER_FLAGS=/MAP /Brepro"
             -DMODERNGEKKO_FZGX_PORT_DIR=${W}/port/src -DFZGX_HOST_TIMERS=${timers}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target moderngekko-port -j${PORT_JOBS}
  BUILD_BYPRODUCTS "${W}/ModernGekko/build/moderngekko-run${EXE}"
  INSTALL_COMMAND ""
  DEPENDS port_sources
  BUILD_ALWAYS ON
  USES_TERMINAL_CONFIGURE ON
  USES_TERMINAL_BUILD ON)

# ---- the image: the mod's native form (port/image/build.py) -------------------------------------------------------
if(NOT PORT_PYTHON)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    set(PORT_PYTHON "${Python3_EXECUTABLE}")
  endif()
endif()
set(PY "${PORT_PYTHON}")
if(PY)
  execute_process(COMMAND "${PY}" -c "import keystone" RESULT_VARIABLE no_keystone OUTPUT_QUIET ERROR_QUIET)
else()
  set(no_keystone 1)
endif()
set(BUNDLE "${W}/port/bundle-${NAME}")
set(GAME_DIR "${W}/extracted/GFZE01")
set(DISC_INPUTS "${GAME_DIR}/sys/main.dol.orig" "${W}/derived/main.rel")
if(no_keystone)
  add_custom_target(image
    COMMAND ${CMAKE_COMMAND} -E echo "image: no Python with the module keystone ('${PY}'): pip install keystone-engine, or PORT_PYTHON=<a python that has it>"
    COMMAND ${CMAKE_COMMAND} -E false VERBATIM)
else()
  file(GLOB_RECURSE image_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/port/image/*.py")
  # the commit the image's manifest names, in a file the command reads (a new commit alone builds no new image)
  set(commit_file "${CMAKE_CURRENT_BINARY_DIR}/source-commit.txt")
  file(CONFIGURE OUTPUT "${commit_file}" CONTENT "${PORT_SOURCE_COMMIT}")
  add_custom_command(
    OUTPUT "${BUNDLE}/mod.bin" "${BUNDLE}/patches.json" "${BUNDLE}/symbols.json" "${BUNDLE}/manifest.json"
    COMMAND "${PY}" "${CMAKE_CURRENT_SOURCE_DIR}/port/image/build.py" "${BUNDLE}" --rel "${W}/derived/main.rel"
            --dol "${GAME_DIR}/sys/main.dol.orig" --commit-file "${commit_file}"
    DEPENDS ${image_sources} "${CMAKE_CURRENT_SOURCE_DIR}/port/facts/generated/facts.py" ${DISC_INPUTS}
    COMMENT "The image, into ${BUNDLE}"
    VERBATIM)
  add_custom_target(image DEPENDS "${BUNDLE}/mod.bin")
endif()

# ---- the module: the recompiler's code for the game with the image, compiled with clang ---------------------------
set(MODULE_BUNDLE "${PORT_BUNDLE}")
if(NOT MODULE_BUNDLE)
  set(MODULE_BUNDLE "${BUNDLE}")
endif()
file(TO_CMAKE_PATH "${MODULE_BUNDLE}" MODULE_BUNDLE)
set(MODULE_DLL "${W}/work/mod-cg-${NAME}/gGFZE01_recomp.dll")
set(profile "${PORT_PROFILE}")
if(NOT profile)
  set(profile "${W}/input/module.profdata")   # where setup.sh --profile puts the one handed out with a release
endif()
# (the bundle's mod.bin and patches.json are what the module is compiled from; its manifest.json and symbols.json are
# copied beside it as a record)
set(module_deps "${PORT_SWITCHES}" ${DISC_INPUTS} "${MODULE_BUNDLE}/mod.bin" "${MODULE_BUNDLE}/patches.json"
    "${MODULE_RECOMPILER}" "${PORT_CMAKE_DIR}/PortModule.cmake")
foreach(e IN LISTS PORT_EXTRA)
  string(REGEX REPLACE "^[^:]*:" "" f "${e}")
  list(APPEND module_deps "${f}")
endforeach()
if(NOT profile STREQUAL "none" AND NOT PORT_PROFILE_GENERATE)
  if(NOT EXISTS "${profile}")
    string(CONCAT module_problem "no profile at ${profile}: the one handed out with the release (setup.sh --profile "
           "FILE, or PORT_PROFILE=FILE), or PORT_PROFILE=none for a module built without one (the same game, slower); "
           "the gates' runs/profile.sh trains one")
  endif()
  list(APPEND module_deps "${profile}")
endif()
if(NOT PORT_BUNDLE AND no_keystone)
  set(module_problem "the module needs the image, and the image a Python with the module keystone (PORT_PYTHON)")
endif()
if(PORT_PROFILE_GENERATE)
  set(profile "none")   # an instrumented module is trained by nothing
endif()
# The module's settings, written at configure time and read by the build script (no paths or switches through a
# shell's quoting)
file(TO_NATIVE_PATH "${W}" W_NATIVE)
set(params "${CMAKE_CURRENT_BINARY_DIR}/module-${NAME}.cmake")
file(CONFIGURE OUTPUT "${params}" CONTENT [=[
set(W "@W@")
set(W_NATIVE [[@W_NATIVE@]])
set(N "@NAME@")
set(BUNDLE "@MODULE_BUNDLE@")
set(SWITCHES "@PORT_SWITCHES@")
set(EXTRA "@PORT_EXTRA@")
set(PROFILE "@profile@")
set(PROFILE_GENERATE [[@PORT_PROFILE_GENERATE@]])
set(LLVM "@LLVM@")
set(PY "@PY@")
set(JOBS "@PORT_JOBS@")
set(RECOMPILER "@MODULE_RECOMPILER@")
]=] @ONLY)
if(module_problem)
  add_custom_target(module
    COMMAND ${CMAKE_COMMAND} -E echo "module: ${module_problem}"
    COMMAND ${CMAKE_COMMAND} -E false VERBATIM)
else()
  add_custom_command(
    OUTPUT "${MODULE_DLL}"
    COMMAND ${CMAKE_COMMAND} -DPARAMS=${params} -P "${PORT_CMAKE_DIR}/PortModule.cmake"
    DEPENDS ${module_deps} "${params}"
    COMMENT "The native module ${NAME} (about 11 minutes; logs: ${W}/work/cg-${NAME}.log and cg-${NAME}-mod.log)"
    USES_TERMINAL
    VERBATIM)
  add_custom_target(module DEPENDS "${MODULE_DLL}")
  if(NOT PORT_RECOMPILER)
    add_dependencies(module recompiler)
  endif()
  if(NOT PORT_BUNDLE)
    add_dependencies(module image)
  endif()
endif()

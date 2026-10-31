# The F-Zero GX port in ModernGekko's build: the host-side core (port/core) as a library of its own, and the adapter
# (port/moderngekko/src) compiled into ModernGekko's runtime library, which already sees the emulator's headers.
# Included from ModernGekko's CMakeLists.txt when MODERNGEKKO_FZGX_PORT_DIR names the port folder (see
# patches/moderngekko). Without that variable nothing of the port is built.
set(_fzgx "${MODERNGEKKO_FZGX_PORT_DIR}")
# Every source file of port/core/src is part of the library: a file added there needs no line here.
file(GLOB _fzgx_core_sources CONFIGURE_DEPENDS "${_fzgx}/core/src/*.cpp")
add_library(fzgx_core STATIC ${_fzgx_core_sources})
# protocol/generated: the launcher's status board (status_board.g.h, generated from protocol/board.toml), which the
# core's fzgx/board.h includes, so the adapter sees it too; build.sh copies it beside the core
target_include_directories(fzgx_core
    PUBLIC "${_fzgx}/core/include" "${_fzgx}/protocol/generated"
    PRIVATE "${_fzgx}/core/src" "${_fzgx}/core/third_party/picojson")
target_compile_features(fzgx_core PUBLIC cxx_std_20)
# liblzma (xz-utils): the compact replay file's xz parts (protocol/generated/replay_compact.g.h). The emulator's own
# build already has it for its disc formats (WIA, RVZ): Dolphin's Externals/liblzma, or the system's
# (dolphin_find_optional_system_library), as LibLZMA::LibLZMA; a system's is found again here, where its imported
# target is not seen.
if(NOT TARGET LibLZMA::LibLZMA)
    find_package(LibLZMA REQUIRED)
endif()
target_link_libraries(fzgx_core PRIVATE LibLZMA::LibLZMA)
if(WIN32)
    target_compile_definitions(fzgx_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(fzgx_core PUBLIC ws2_32)
endif()
# A profiling build (build.sh with HOST_TIMERS=1): the core's scoped timers (core/src/host_timers.h). Never a player's.
option(FZGX_HOST_TIMERS "the core's scoped timers, for a profiling build" OFF)
if(FZGX_HOST_TIMERS)
    target_compile_definitions(fzgx_core PRIVATE FZGX_HOST_TIMERS=1)
endif()

# Every source file of port/moderngekko/src is part of the adapter: a file added there needs no line here.
file(GLOB _fzgx_adapter_sources CONFIGURE_DEPENDS "${_fzgx}/moderngekko/src/*.cpp")
target_sources(moderngekko PRIVATE ${_fzgx_adapter_sources})
# the facts table's generated header (port/facts/generated/facts.h): the adapter's addresses and offsets
target_include_directories(moderngekko PUBLIC "${_fzgx}/moderngekko/src" "${_fzgx}/facts/generated")
target_compile_definitions(moderngekko PUBLIC MODERNGEKKO_FZGX_PORT=1)
target_link_libraries(moderngekko PRIVATE fzgx_core)
# "Show inputs" (inputs_overlay.cpp): m-overlay's pictures (art/inputs, MIT, LICENSE.txt beside them) carried inside the
# program, as bytes in a generated file that is never committed.
file(GLOB _fzgx_inputs_art CONFIGURE_DEPENDS "${_fzgx}/moderngekko/art/inputs/*.png")
set(_fzgx_inputs_bytes "")
set(_fzgx_inputs_table "")
foreach(_png IN LISTS _fzgx_inputs_art)
    get_filename_component(_name "${_png}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_name}" _id)
    file(READ "${_png}" _hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," _hex "${_hex}")
    string(APPEND _fzgx_inputs_bytes "static const unsigned char ART_${_id}[] = {${_hex}};\n")
    string(APPEND _fzgx_inputs_table "    {\"${_name}\", ART_${_id}, sizeof ART_${_id}},\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fzgx_generated/inputs_art.g.inc" @ONLY
     CONTENT "@_fzgx_inputs_bytes@static const ArtFile ART[] = {\n@_fzgx_inputs_table@};\n")
target_include_directories(moderngekko PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fzgx_generated")
# the panel before a race (panel.cpp) is drawn into the emulator's own on-screen frame
if(TARGET imgui)
    target_link_libraries(moderngekko PRIVATE imgui)
endif()
# the game window's strip (frame_paint.cpp) is drawn with GDI+
if(WIN32)
    target_link_libraries(moderngekko PRIVATE gdiplus)
endif()
# The runner's program file carries the app's icon (src/runner.rc; build.sh puts the icon file beside the sources,
# in app/) and says that it scales itself with the screen (src/runner.manifest, which the linker merges into the
# program's manifest). The runner's target is made after this file is read, so the lines run at the end of the
# CMakeLists.txt that includes it. Without the icon file (a build that did not come through build.sh) the runner
# has no icon.
function(_fzgx_runner_icon)
    if(WIN32 AND TARGET moderngekko-run)
        target_sources(moderngekko-run PRIVATE "${MODERNGEKKO_FZGX_PORT_DIR}/moderngekko/src/runner.manifest")
    endif()
    if(WIN32 AND TARGET moderngekko-run AND EXISTS "${MODERNGEKKO_FZGX_PORT_DIR}/app/app.ico")
        target_sources(moderngekko-run PRIVATE "${MODERNGEKKO_FZGX_PORT_DIR}/moderngekko/src/runner.rc")
        target_include_directories(moderngekko-run PRIVATE "${MODERNGEKKO_FZGX_PORT_DIR}/app")
    endif()
endfunction()
cmake_language(DEFER CALL _fzgx_runner_icon)
# The undo log compares 18 MiB of memory by page hashes at least once a displayed frame: XXH3 from the emulator's own
# copy of xxHash, as the fork used, where the build has it (the core's portable hash is three times slower).
if(TARGET xxhash::xxhash)
    target_link_libraries(moderngekko PRIVATE xxhash::xxhash)
    target_compile_definitions(moderngekko PRIVATE FZGX_PORT_XXHASH=1)
endif()
# The replay scrubber's undo log (core scrub.cpp) packed with the emulator's own copy of zstd, where the build has it
# (its disc images use it); without it the units stay flat (and the replay holds about three times more).
if(TARGET zstd::zstd)
    target_link_libraries(moderngekko PRIVATE zstd::zstd)
    target_compile_definitions(moderngekko PRIVATE FZGX_PORT_ZSTD=1)
endif()

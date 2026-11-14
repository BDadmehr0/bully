# =============================================================================
#  Bully Studio - Modular C++ Game Development Platform
#  Target platform: Arch Linux / Omarchy (GCC or Clang, C++20, Ninja)
#
#  Every layer below is a separate CMake target with an explicit public
#  interface, so modules can be built, tested, embedded or replaced
#  independently:
#
#    bully::core           - errors, ids, json, fs, logging, hashing, time
#    bully::formats        - versioned binary resource formats (.bmodel, ...)
#    bully::pipeline       - asset import/convert/validate/cache/package
#    bully::plugins        - versioned C-ABI plugin host
#    bully::studio_core    - project metadata (SQLite), documents, editors
#    bully::runtime        - SDL3/OpenGL game runtime (optional)
#    bully_studio          - Qt 6 desktop shell (optional)
#    bully_game            - playable runtime executable (optional)
#    bully_asset_cli       - headless pipeline/QA tool (always buildable)
# =============================================================================
cmake_minimum_required(VERSION 3.24)

project(BullyStudio
    VERSION 0.1.0
    DESCRIPTION "Modular C++ game development platform"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(BullyHelpers)

# ----------------------------------------------------------------- options ---
option(BULLY_BUILD_STUDIO      "Build the Qt 6 desktop application" ON)
option(BULLY_BUILD_RUNTIME     "Build the game host executable (SDL3 window when available, headless otherwise)" ON)
option(BULLY_BUILD_TESTS       "Build unit/integration tests" ON)
option(BULLY_BUILD_CLI         "Build the headless asset/pipeline CLI" ON)
option(BULLY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(BULLY_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)
option(BULLY_STL_ASSERTIONS   "Enable libstdc++ container assertions (_GLIBCXX_ASSERTIONS)" ON)

# Global language settings.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

bully_setup_warnings()

# ------------------------------------------------------------ third party ---
# Qt 6 is only required by the studio shell; everything else stays headless.
if(BULLY_BUILD_STUDIO)
    find_package(Qt6 6.4 COMPONENTS Core Gui Widgets OpenGLWidgets Sql Concurrent QUIET)
    if(NOT Qt6_FOUND)
        message(WARNING
            "Qt6 >= 6.4 not found - the Bully Studio desktop shell will NOT be built.\n"
            "Install it on Arch Linux:  sudo pacman -S qt6-base qt6-tools\n"
            "All headless targets (pipeline, formats, runtime, CLI, tests) still build.")
        set(BULLY_BUILD_STUDIO OFF)
    else()
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTOUIC ON)
        set(CMAKE_AUTORCC ON)
    endif()
endif()

# SDL3/OpenGL are optional: they only enable the *windowed* game host. The
# headless runtime subsystems and the game executable's --headless mode do not
# need a graphics stack, so the build never fails for a missing display library.
set(BULLY_HAVE_SDL3 OFF)
if(BULLY_BUILD_RUNTIME)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
        if(SDL3_FOUND AND NOT TARGET SDL3::SDL3)
            add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
        endif()
    endif()
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OPENGL_FOUND)
        set(BULLY_HAVE_SDL3 ON)
    else()
        message(STATUS
            "SDL3 or OpenGL not found - bully_game builds headless-only.\n"
            "Install on Arch Linux:  sudo pacman -S sdl3 mesa")
    endif()
endif()

# ----------------------------------------------------------------- layers ---
add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(formats)
add_subdirectory(exporters)
add_subdirectory(asset_pipeline)
add_subdirectory(plugins)
add_subdirectory(studio)
add_subdirectory(templates)

if(BULLY_BUILD_CLI)
    add_subdirectory(apps/asset_cli)
endif()

# The runtime *subsystems* (assets, story, clock, input) are headless and always
# built with the engine. The game host executable is headless too: only its
# optional SDL3 window needs a graphics stack.
add_subdirectory(engine/runtime)

if(BULLY_BUILD_RUNTIME)
    add_subdirectory(apps/game)
endif()

if(BULLY_BUILD_STUDIO)
    add_subdirectory(apps/studio)
endif()

if(BULLY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    # `ctest --presets headless` is a common typo (running ctest from the source
    # root rather than the build tree). It reports "No tests were found" and
    # exits 0, which reads as success in CI while nothing was tested. A stale
    # CTestTestfile.cmake beside the top-level CMakeLists.txt turns that into a
    # hard error naming the correct command instead.
    file(WRITE "${CMAKE_SOURCE_DIR}/CTestTestfile.cmake"
        "# Generated by CMake: this file exists so that running ctest from the\n"
        "# source tree fails instead of silently reporting success.\n"
        "message(FATAL_ERROR\n"
        "  \"ctest was run from the source directory, not the build directory.\\n\"\n"
        "  \"No tests were run. Use one of:\\n\"\n"
        "  \"  ctest --preset headless        # from the repository root\\n\"\n"
        "  \"  ctest --test-dir build/headless\\n\"\n"
        "  \"  make test\")\n")
endif()

# ------------------------------------------------------------- summary ------
bully_print_configuration()

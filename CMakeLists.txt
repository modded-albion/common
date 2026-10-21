# ******************************************************************************
# Copyright (c) 2026 Masters of Albion Reforged developers
# ******************************************************************************

# The shared code of the mod loader and the mods of Masters of Albion. A mod project includes this folder as a git
# submodule (external/common) with add_subdirectory. Built alone, the project builds the libraries and the tests.
#
# Each module that goes into the game folder (version.dll and each mod DLL) uses the static C runtime and brings no
# other DLL. Thus every target of the projects uses the static C runtime, and vcpkg uses the triplet x64-windows-static.

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(
  moa_common
  VERSION 0.1.0
  LANGUAGES C CXX
)

if (PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

option(MOA_COMMON_BUILD_TESTS "Build the tests of common" ${PROJECT_IS_TOP_LEVEL})
option(MOA_WARNINGS_AS_ERRORS "Make the compiler stop on a warning" ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Result, the binary reader and the export paths: headers only.
add_library(moa_common INTERFACE)
target_include_directories(moa_common INTERFACE src/common/include)
target_compile_features(moa_common INTERFACE cxx_std_20)

# The moon, the sun and the stars.
add_library(
  moa_astro STATIC
  src/astro/include/moa/astro/Moon.h
  src/astro/include/moa/astro/Sky.h
  src/astro/include/moa/astro/StarTable.h
  src/astro/src/Moon.cpp
  src/astro/src/Sky.cpp
  src/astro/src/StarTable.cpp
)
target_include_directories(moa_astro PUBLIC src/astro/include)
target_compile_features(moa_astro PUBLIC cxx_std_20)
moa_set_warnings(moa_astro)

# The mod zips: a zip reader and writer in memory (miniz), the checks of the entry names, and the file mod.json.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Miniz.cmake)
# miniz is not our code: it gets no warning flags.
add_library(moa_miniz STATIC ${MOA_MINIZ_SOURCES})
target_include_directories(moa_miniz PUBLIC ${MOA_MINIZ_DIR} ${MOA_MINIZ_CONFIG_DIR})
target_compile_definitions(moa_miniz PUBLIC ${MOA_MINIZ_DEFINITIONS})
target_compile_options(moa_miniz PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)

add_library(
  moa_modzip STATIC
  src/modzip/include/moa/modzip/ModInfo.h
  src/modzip/include/moa/modzip/Zip.h
  src/modzip/src/ModInfo.cpp
  src/modzip/src/Zip.cpp
)
target_include_directories(moa_modzip PUBLIC src/modzip/include)
target_link_libraries(moa_modzip PUBLIC moa_common PRIVATE moa_miniz nlohmann_json::nlohmann_json)
moa_set_warnings(moa_modzip)

# The map overrides: the files of edited map data that the loader gives to the game, and their checks.
add_library(
  moa_mapoverride STATIC
  src/mapoverride/include/moa/mapoverride/MapOverride.h
  src/mapoverride/include/moa/mapoverride/Sha1.h
  src/mapoverride/src/MapOverride.cpp
  src/mapoverride/src/Sha1.cpp
)
target_include_directories(moa_mapoverride PUBLIC src/mapoverride/include)
target_link_libraries(moa_mapoverride PUBLIC moa_common PRIVATE nlohmann_json::nlohmann_json)
moa_set_warnings(moa_mapoverride)

# The parts of the mods that do not need the game: the tests use them with fakes.
add_library(
  moa_mod_core STATIC
  src/core/include/moa_mod/CrashDumpRules.h
  src/core/include/moa_mod/FrameCallbacks.h
  src/core/include/moa_mod/Hotkey.h
  src/core/include/moa_mod/MainThreadTasks.h
  src/core/include/moa_mod/SettingsFields.h
  src/core/include/moa_mod/SettingsFile.h
  src/core/include/moa_mod/SlotSwap.h
  src/core/include/moa_mod/StartupGuard.h
  src/core/include/moa_mod/UnityMath.h
  src/core/src/FrameCallbacks.cpp
  src/core/src/Hotkey.cpp
  src/core/src/SettingsFile.cpp
  src/core/src/StartupGuard.cpp
)
target_include_directories(moa_mod_core PUBLIC src/core/include)
target_link_libraries(moa_mod_core PUBLIC moa_common nlohmann_json::nlohmann_json)
moa_set_warnings(moa_mod_core)

# The interface between the loader and a mod DLL: a C header.
add_library(moa_mod_api INTERFACE)
target_include_directories(moa_mod_api INTERFACE src/api/include)

# spdlog with the fmt library of vcpkg. The static triplet makes both static, so no DLL goes into the game folder.
add_library(moa_mod_spdlog INTERFACE)
target_link_libraries(moa_mod_spdlog INTERFACE spdlog::spdlog)

# The C++ side of the interface in a mod DLL.
add_library(moa_mod_sdk STATIC src/sdk/include/moa_mod/sdk/HostLogSink.h src/sdk/include/moa_mod/sdk/ModHost.h
                               src/sdk/src/ModHost.cpp
)
target_include_directories(moa_mod_sdk PUBLIC src/sdk/include)
target_link_libraries(moa_mod_sdk PUBLIC moa_mod_api moa_mod_core moa_mod_spdlog)
moa_set_warnings(moa_mod_sdk)

if (WIN32)
  # The parts that run in the game: IL2CPP, the managed objects, the method hooks, the hotkeys, the messages of the
  # game and the crash dumps.
  add_library(
    moa_mod_game STATIC
    src/game/include/moa_mod/CrashDump.h
    src/game/include/moa_mod/GameMessages.h
    src/game/include/moa_mod/Hooks.h
    src/game/include/moa_mod/HotkeyInput.h
    src/game/include/moa_mod/Il2Cpp.h
    src/game/include/moa_mod/Managed.h
    src/game/include/moa_mod/ModStatus.h
    src/game/src/CrashDump.cpp
    src/game/src/GameMessages.cpp
    src/game/src/Hooks.cpp
    src/game/src/HotkeyInput.cpp
    src/game/src/Il2Cpp.cpp
    src/game/src/Managed.cpp
  )
  target_include_directories(moa_mod_game PUBLIC src/game/include)
  target_compile_definitions(moa_mod_game PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(moa_mod_game PUBLIC moa_mod_core moa_mod_spdlog)
  moa_set_warnings(moa_mod_game)
endif ()

if (MOA_COMMON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()

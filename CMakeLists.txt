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
target_include_directories(moa_common INTERFACE src)
target_compile_features(moa_common INTERFACE cxx_std_20)

# The moon, the sun and the stars.
add_library(
  moa_astro STATIC
  src/astro/Moon.h
  src/astro/Sky.h
  src/astro/StarTable.h
  src/astro/Moon.cpp
  src/astro/Sky.cpp
  src/astro/StarTable.cpp
)
target_include_directories(moa_astro PUBLIC src)
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
  src/modzip/ModInfo.h
  src/modzip/Zip.h
  src/modzip/ModInfo.cpp
  src/modzip/Zip.cpp
)
target_include_directories(moa_modzip PUBLIC src)
target_link_libraries(moa_modzip PUBLIC moa_common PRIVATE moa_miniz nlohmann_json::nlohmann_json)
moa_set_warnings(moa_modzip)

# The map overrides: the files of edited map data that the loader gives to the game, and their checks.
add_library(
  moa_mapoverride STATIC
  src/mapoverride/MapOverride.h
  src/mapoverride/Sha1.h
  src/mapoverride/MapOverride.cpp
  src/mapoverride/Sha1.cpp
)
target_include_directories(moa_mapoverride PUBLIC src)
target_link_libraries(moa_mapoverride PUBLIC moa_common PRIVATE nlohmann_json::nlohmann_json)
moa_set_warnings(moa_mapoverride)

# The parts of the mods that do not need the game: the tests use them with fakes.
add_library(
  moa_mod_core STATIC
  src/mod_core/CrashDumpRules.h
  src/mod_core/Folders.h
  src/mod_core/FrameCallbacks.h
  src/mod_core/Hotkey.h
  src/mod_core/MainThreadTasks.h
  src/mod_core/SettingsFields.h
  src/mod_core/SettingsFile.h
  src/mod_core/SlotSwap.h
  src/mod_core/StartupGuard.h
  src/mod_core/UnityMath.h
  src/mod_core/Folders.cpp
  src/mod_core/FrameCallbacks.cpp
  src/mod_core/Hotkey.cpp
  src/mod_core/SettingsFile.cpp
  src/mod_core/StartupGuard.cpp
)
target_include_directories(moa_mod_core PUBLIC src)
target_link_libraries(moa_mod_core PUBLIC moa_common nlohmann_json::nlohmann_json)
moa_set_warnings(moa_mod_core)

# The interface between the loader and a mod DLL: a C header.
add_library(moa_mod_api INTERFACE)
target_include_directories(moa_mod_api INTERFACE src)

# spdlog with the fmt library of vcpkg. The static triplet makes both static, so no DLL goes into the game folder.
add_library(moa_mod_spdlog INTERFACE)
target_link_libraries(moa_mod_spdlog INTERFACE spdlog::spdlog)

# The C++ side of the interface in a mod DLL.
add_library(
  moa_mod_sdk STATIC src/mod_sdk/HostLogSink.h src/mod_sdk/ModHost.h src/mod_sdk/ModHost.cpp
)
target_include_directories(moa_mod_sdk PUBLIC src)
target_link_libraries(moa_mod_sdk PUBLIC moa_mod_api moa_mod_core moa_mod_spdlog)
moa_set_warnings(moa_mod_sdk)

if (WIN32)
  # The parts that run in the game: IL2CPP, the managed objects, the method hooks, the hotkeys, the messages of the
  # game and the crash dumps.
  add_library(
    moa_mod_game STATIC
    src/mod_game/CrashDump.h
    src/mod_game/GameMessages.h
    src/mod_game/Hooks.h
    src/mod_game/HotkeyInput.h
    src/mod_game/Il2Cpp.h
    src/mod_game/Managed.h
    src/mod_game/ModStatus.h
    src/mod_game/CrashDump.cpp
    src/mod_game/GameMessages.cpp
    src/mod_game/Hooks.cpp
    src/mod_game/HotkeyInput.cpp
    src/mod_game/Il2Cpp.cpp
    src/mod_game/Managed.cpp
  )
  target_include_directories(moa_mod_game PUBLIC src)
  target_compile_definitions(moa_mod_game PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(moa_mod_game PUBLIC moa_mod_core moa_mod_spdlog)
  moa_set_warnings(moa_mod_game)
endif ()

if (MOA_COMMON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()

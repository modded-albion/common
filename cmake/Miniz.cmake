# The mod zips use miniz (inflate, deflate and the zip format). The research mod uses the static C runtime and must
# bring no other DLL into the game folder, so each target compiles the miniz sources itself, from the same archive as
# the vcpkg port (vcpkg/ports/miniz/portfile.cmake). This file gives MOA_MINIZ_DIR (the sources), MOA_MINIZ_CONFIG_DIR
# (the export header that the miniz CMake file makes) and MOA_MINIZ_SOURCES. Each target that compiles the sources
# also gets MOA_MINIZ_DEFINITIONS.

include_guard(GLOBAL)

set(MOA_MINIZ_VERSION "3.1.2")
set(MOA_MINIZ_SHA512
    "5f553990632cb15d996ccfa92641167863807867baa9ff0b9a94ae0e40daf782df1746e75ea228a93892be1f574c9261838c9edb94e40bbf559ab22f21068c41"
)
set(_moa_miniz_archive_name "richgel999-miniz-${MOA_MINIZ_VERSION}.tar.gz")
set(_moa_miniz_deps "${CMAKE_BINARY_DIR}/_deps/moa_miniz")

# Use the archive in the download cache of vcpkg. If it is not there, download it once into the build folder.
set(_moa_miniz_archive "$ENV{VCPKG_DOWNLOADS}/${_moa_miniz_archive_name}")
if (NOT DEFINED ENV{VCPKG_DOWNLOADS} OR NOT EXISTS "${_moa_miniz_archive}")
  set(_moa_miniz_archive "${_moa_miniz_deps}/${_moa_miniz_archive_name}")
  if (NOT EXISTS "${_moa_miniz_archive}")
    message(STATUS "Download the miniz sources ${MOA_MINIZ_VERSION}")
    file(
      DOWNLOAD "https://github.com/richgel999/miniz/archive/${MOA_MINIZ_VERSION}.tar.gz" "${_moa_miniz_archive}"
      EXPECTED_HASH SHA512=${MOA_MINIZ_SHA512}
      STATUS _moa_miniz_status
    )
    list(GET _moa_miniz_status 0 _moa_miniz_code)
    if (NOT _moa_miniz_code EQUAL 0)
      file(REMOVE "${_moa_miniz_archive}")
      message(FATAL_ERROR "The download of the miniz sources failed: ${_moa_miniz_status}")
    endif ()
  endif ()
endif ()
file(SHA512 "${_moa_miniz_archive}" _moa_miniz_hash)
if (NOT _moa_miniz_hash STREQUAL MOA_MINIZ_SHA512)
  message(FATAL_ERROR "${_moa_miniz_archive} is not the archive of miniz ${MOA_MINIZ_VERSION}")
endif ()

set(MOA_MINIZ_DIR "${_moa_miniz_deps}/miniz-${MOA_MINIZ_VERSION}")
if (NOT EXISTS "${MOA_MINIZ_DIR}/miniz_zip.c")
  file(ARCHIVE_EXTRACT INPUT "${_moa_miniz_archive}" DESTINATION "${_moa_miniz_deps}")
endif ()

# The CMake file of miniz makes miniz_export.h. The sources compile into each target, so the macro is empty.
set(MOA_MINIZ_CONFIG_DIR "${_moa_miniz_deps}/config")
file(WRITE "${MOA_MINIZ_CONFIG_DIR}/miniz_export.h.new" "#pragma once\n#define MINIZ_EXPORT\n")
file(COPY_FILE "${MOA_MINIZ_CONFIG_DIR}/miniz_export.h.new" "${MOA_MINIZ_CONFIG_DIR}/miniz_export.h" ONLY_IF_DIFFERENT)

set(MOA_MINIZ_SOURCES
    "${MOA_MINIZ_DIR}/miniz.c" "${MOA_MINIZ_DIR}/miniz_tdef.c" "${MOA_MINIZ_DIR}/miniz_tinfl.c"
    "${MOA_MINIZ_DIR}/miniz_zip.c"
)
# The zips stay in memory: no file functions. No time: each entry that the writer adds has the same date, so the same
# files give the same zip.
set(MOA_MINIZ_DEFINITIONS MINIZ_NO_STDIO MINIZ_NO_TIME)

# Make the values visible in every folder of the project.
set(MOA_MINIZ_DIR "${MOA_MINIZ_DIR}" CACHE INTERNAL "")
set(MOA_MINIZ_CONFIG_DIR "${MOA_MINIZ_CONFIG_DIR}" CACHE INTERNAL "")
set(MOA_MINIZ_SOURCES "${MOA_MINIZ_SOURCES}" CACHE INTERNAL "")
set(MOA_MINIZ_DEFINITIONS "${MOA_MINIZ_DEFINITIONS}" CACHE INTERNAL "")

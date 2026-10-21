/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <filesystem>

#include <common/Result.h>
#include <windows.h>

namespace moa_mod
{

/// Write a minidump when a thread of the game fills its stack (see `DecideCrashDump`), or when a guard of the mod
/// catches an access violation. A vectored exception handler sees each exception first. The thread of the exception
/// can have almost no stack, so a worker thread that waits from the start writes the dump. One process writes a
/// maximum of three dumps. Then the exception continues to the next handler (the crash handler of the game).
///
/// Install it one time in the game process. It stays until the process ends.
[[nodiscard]] moa::Result<void> InstallCrashDumpWriter(const std::filesystem::path& folder);

/// The filter of an `__except` guard of the mod. For an access violation, write a dump (if the writer is installed)
/// and return `EXCEPTION_EXECUTE_HANDLER`. For any other exception, return `EXCEPTION_CONTINUE_SEARCH`.
[[nodiscard]] int FilterAccessViolation(EXCEPTION_POINTERS* pointers);

} // namespace moa_mod

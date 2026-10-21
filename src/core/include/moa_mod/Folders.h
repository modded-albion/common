/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <filesystem>

namespace moa_mod
{

/// True if `path` is `folder` or a folder or file in it. The test ignores the case of letters and the form of the
/// separators, as Windows does.
[[nodiscard]] bool IsInsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder);

} // namespace moa_mod

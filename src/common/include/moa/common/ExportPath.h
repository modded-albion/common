/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cctype>

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "Result.h"

namespace moa
{

/// The folders that hold the game. The tools read them and never write into them.
constexpr std::array<std::string_view, 2> k_GameFolders {
    "D:/masters_of_Albion_original",
    "F:/SteamLibrary/steamapps/common/Masters of Albion",
};

/// The folder for changed copies of the game data.
constexpr std::string_view k_GameDataExportFolder = "D:/masters_of_albion/exports/data";

/// The default folder for the mod zips that the editor writes.
constexpr std::string_view k_ModExportFolder = "D:/masters_of_albion/exports/mods";

namespace detail
{
[[nodiscard]] inline std::string NormalPath(const std::filesystem::path& path)
{
	std::error_code error;
	auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, error), error).generic_string();
	std::ranges::transform(absolute, absolute.begin(),
	                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	while (absolute.size() > 1 && absolute.back() == '/')
	{
		absolute.pop_back();
	}
	return absolute;
}
} // namespace detail

/// Return true if the path is the folder or is in it. The test ignores case and the kind of separator.
[[nodiscard]] inline bool PathIsInFolder(const std::filesystem::path& path, const std::filesystem::path& folder)
{
	const auto item = detail::NormalPath(path);
	const auto root = detail::NormalPath(folder);
	return item == root || (item.size() > root.size() && item.starts_with(root) && item[root.size()] == '/');
}

/// Return true if the path is in a folder of the game.
[[nodiscard]] inline bool PathIsInGameFolder(const std::filesystem::path& path)
{
	return std::ranges::any_of(k_GameFolders, [&](std::string_view folder) { return PathIsInFolder(path, folder); });
}

/// Make sure that a tool can write the output: it is in the allowed folder, it is not in a game folder, and it is not
/// one of the input files.
[[nodiscard]] inline Result<void> CheckExportTarget(const std::filesystem::path& output,
                                                    const std::filesystem::path& allowedFolder,
                                                    std::span<const std::filesystem::path> inputs)
{
	if (PathIsInGameFolder(output))
	{
		return MakeError("{} is in a game folder; the tools never write there", output.string());
	}
	if (!PathIsInFolder(output, allowedFolder) || detail::NormalPath(output) == detail::NormalPath(allowedFolder))
	{
		return MakeError("the output must be a file in {}; {} is not", allowedFolder.string(), output.string());
	}
	for (const auto& input : inputs)
	{
		if (detail::NormalPath(output) == detail::NormalPath(input))
		{
			return MakeError("the output {} is an input file", output.string());
		}
	}
	return {};
}

} // namespace moa

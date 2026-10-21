/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cwctype>

#include <algorithm>
#include <string>

#include <moa_mod/Folders.h>

namespace moa_mod
{

namespace
{
[[nodiscard]] wchar_t FoldCase(wchar_t c)
{
	return c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
}
} // namespace

bool IsInsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder)
{
	auto normalised = [](const std::filesystem::path& value) {
		auto text = value.lexically_normal().wstring();
		std::ranges::transform(text, text.begin(), FoldCase);
		while (text.size() > 1 && text.back() == L'/')
		{
			text.pop_back();
		}
		return text;
	};
	const auto inner = normalised(path);
	const auto outer = normalised(folder);
	if (outer.empty() || !inner.starts_with(outer))
	{
		return false;
	}
	return inner.size() == outer.size() || inner[outer.size()] == L'/' || outer.back() == L'/';
}

} // namespace moa_mod

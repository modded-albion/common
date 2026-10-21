/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cctype>

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <moa_mod/Hotkey.h>

namespace moa_mod
{

namespace
{
/// The names of the Unity `KeyCode` enum that a hotkey can use, other than the letters, the digits, the F keys and
/// the keypad digits.
constexpr std::array<std::string_view, 33> k_NamedKeys {
    "Home",           "End",         "Insert",     "Delete",       "PageUp",      "PageDown",     "Pause",
    "ScrollLock",     "Backquote",   "Minus",      "Equals",       "LeftBracket", "RightBracket", "Semicolon",
    "Quote",          "Comma",       "Period",     "Slash",        "Backslash",   "Space",        "Tab",
    "Return",         "Backspace",   "UpArrow",    "DownArrow",    "LeftArrow",   "RightArrow",   "KeypadDivide",
    "KeypadMultiply", "KeypadMinus", "KeypadPlus", "KeypadPeriod", "KeypadEnter",
};

[[nodiscard]] std::string Lower(std::string_view text)
{
	std::string result(text);
	std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

[[nodiscard]] std::string_view Trim(std::string_view text)
{
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
	{
		text.remove_prefix(1);
	}
	while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
	{
		text.remove_suffix(1);
	}
	return text;
}

/// Read a number of one or two digits. Return -1 if the text is not that.
[[nodiscard]] int SmallNumber(std::string_view text)
{
	if (text.empty() || text.size() > 2 || !std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c) != 0; }))
	{
		return -1;
	}
	int value = 0;
	for (const char c : text)
	{
		value = value * 10 + (c - '0');
	}
	return value;
}

/// Find the `KeyCode` name of a key, with the case of Unity. Return an empty text if the name is not permitted.
[[nodiscard]] std::string KeyName(std::string_view text)
{
	const auto lower = Lower(text);
	if (lower.size() == 1 && std::isalpha(static_cast<unsigned char>(lower[0])) != 0)
	{
		return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(lower[0]))));
	}
	if (lower.size() == 1 && std::isdigit(static_cast<unsigned char>(lower[0])) != 0)
	{
		return "Alpha" + lower;
	}
	if (lower.starts_with("alpha"))
	{
		const auto digit = SmallNumber(std::string_view(lower).substr(5));
		return digit >= 0 && digit <= 9 ? "Alpha" + std::to_string(digit) : std::string();
	}
	if (lower.starts_with('f'))
	{
		const auto number = SmallNumber(std::string_view(lower).substr(1));
		if (number >= 1 && number <= 15)
		{
			return "F" + std::to_string(number);
		}
	}
	if (lower.starts_with("keypad"))
	{
		const auto digit = SmallNumber(std::string_view(lower).substr(6));
		if (digit >= 0 && digit <= 9)
		{
			return "Keypad" + std::to_string(digit);
		}
	}
	const auto found = std::ranges::find_if(k_NamedKeys, [&](std::string_view name) { return Lower(name) == lower; });
	return found != k_NamedKeys.end() ? std::string(*found) : std::string();
}
} // namespace

moa::Result<Hotkey> ParseHotkey(std::string_view text)
{
	text = Trim(text);
	if (text.empty() || Lower(text) == "none")
	{
		return Hotkey {};
	}
	std::vector<std::string_view> parts;
	for (const auto part : std::views::split(text, '+'))
	{
		parts.emplace_back(Trim(std::string_view(part.begin(), part.end())));
	}
	Hotkey hotkey;
	for (size_t i = 0; i + 1 < parts.size(); ++i)
	{
		const auto modifier = Lower(parts[i]);
		bool* flag = modifier == "ctrl" || modifier == "control" ? &hotkey.control
		             : modifier == "alt"                         ? &hotkey.alt
		             : modifier == "shift"                       ? &hotkey.shift
		                                                         : nullptr;
		if (flag == nullptr)
		{
			return moa::MakeError("the hotkey '{}' has the modifier '{}'; use Ctrl, Alt or Shift", text, parts[i]);
		}
		if (*flag)
		{
			return moa::MakeError("the hotkey '{}' has the modifier '{}' two times", text, parts[i]);
		}
		*flag = true;
	}
	hotkey.key = KeyName(parts.back());
	if (hotkey.key.empty())
	{
		return moa::MakeError("the hotkey '{}' has the key '{}', which is not permitted; use a letter, a digit, F1 to F15 or "
		                      "a name such as Home or PageUp",
		                      text, parts.back());
	}
	return hotkey;
}

std::string FormatHotkey(const Hotkey& hotkey)
{
	if (!hotkey.Enabled())
	{
		return "none";
	}
	std::string text;
	text += hotkey.control ? "Ctrl+" : "";
	text += hotkey.alt ? "Alt+" : "";
	text += hotkey.shift ? "Shift+" : "";
	return text + hotkey.key;
}

bool HotkeyFires(const Hotkey& hotkey, ModifierKeys held, bool keyDown)
{
	return hotkey.Enabled() && keyDown && held.control == hotkey.control && held.alt == hotkey.alt &&
	       held.shift == hotkey.shift;
}

} // namespace moa_mod

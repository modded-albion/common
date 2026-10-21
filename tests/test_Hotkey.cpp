/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <gtest/gtest.h>
#include <mod_core/Hotkey.h>

using namespace moa_mod;

TEST(Hotkey, ReadsModifiersAndKeysInAnyCase)
{
	const auto hotkey = ParseHotkey("ctrl + Shift+f5");
	ASSERT_TRUE(hotkey);
	EXPECT_TRUE(hotkey->control);
	EXPECT_FALSE(hotkey->alt);
	EXPECT_TRUE(hotkey->shift);
	EXPECT_EQ(hotkey->key, "F5");
	EXPECT_EQ(FormatHotkey(*hotkey), "Ctrl+Shift+F5");

	EXPECT_EQ(ParseHotkey("Alt+c")->key, "C");
	EXPECT_EQ(ParseHotkey("Ctrl+7")->key, "Alpha7");
	EXPECT_EQ(ParseHotkey("Ctrl+Alpha7")->key, "Alpha7");
	EXPECT_EQ(ParseHotkey("Control+pageup")->key, "PageUp");
	EXPECT_EQ(ParseHotkey("keypad3")->key, "Keypad3");
	EXPECT_EQ(ParseHotkey("F15")->key, "F15");
	EXPECT_EQ(ParseHotkey("F")->key, "F");
}

TEST(Hotkey, NoneAndEmptyTurnTheHotkeyOff)
{
	EXPECT_FALSE(ParseHotkey("")->Enabled());
	EXPECT_FALSE(ParseHotkey("None")->Enabled());
	EXPECT_EQ(FormatHotkey(Hotkey {}), "none");
}

TEST(Hotkey, RefusesBadText)
{
	EXPECT_FALSE(ParseHotkey("Ctrl+"));
	EXPECT_FALSE(ParseHotkey("Ctrl+Ctrl+F5"));
	EXPECT_FALSE(ParseHotkey("Meta+F5"));
	EXPECT_FALSE(ParseHotkey("Ctrl+F16"));
	EXPECT_FALSE(ParseHotkey("Ctrl+F0"));
	EXPECT_FALSE(ParseHotkey("Ctrl+Shift"));
	EXPECT_FALSE(ParseHotkey("Ctrl+LeftControl"));
	EXPECT_FALSE(ParseHotkey("Ctrl+Alpha10"));
}

TEST(Hotkey, FormatAndParseGiveTheSameHotkey)
{
	for (const auto* text : {"Ctrl+Shift+F5", "Ctrl+F1", "F4", "Alt+Shift+Home", "Keypad7"})
	{
		const auto hotkey = ParseHotkey(text);
		ASSERT_TRUE(hotkey) << text;
		EXPECT_EQ(*ParseHotkey(FormatHotkey(*hotkey)), *hotkey);
	}
}

TEST(Hotkey, FiresOnlyWithExactlyItsModifiers)
{
	const auto hotkey = *ParseHotkey("Ctrl+Shift+F5");
	EXPECT_TRUE(HotkeyFires(hotkey, {.control = true, .shift = true}, true));
	EXPECT_FALSE(HotkeyFires(hotkey, {.control = true, .shift = true}, false));
	EXPECT_FALSE(HotkeyFires(hotkey, {.control = true}, true));
	EXPECT_FALSE(HotkeyFires(hotkey, {.control = true, .alt = true, .shift = true}, true));
	EXPECT_FALSE(HotkeyFires(Hotkey {}, {}, true));
}

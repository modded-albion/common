/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <common/Result.h>
#include <mod_core/Hotkey.h>

#include "mod_game/Managed.h"

namespace moa_mod
{

/// Reads the hotkeys of the mods through the `Input` class of Unity, on the main thread. The keys are the values of
/// the Unity `KeyCode` enum, found by their names. The hotkeys do nothing while a text field of the game has the
/// keyboard.
class HotkeyInput
{
public:
	/// Find the methods of Unity and of the game that the hotkeys need. Return an error that names the missing ones.
	[[nodiscard]] static moa::Result<HotkeyInput> Bind(const Managed& managed);

	/// A hotkey and the name that the log uses for it.
	struct NamedHotkey
	{
		std::string name;
		Hotkey hotkey;
	};

	/// Use these hotkeys. `enabled` false turns all of them off. A key name that Unity does not know stops only that
	/// hotkey, and the log tells which one.
	void SetHotkeys(bool enabled, std::span<const NamedHotkey> hotkeys);

	/// Return the indexes (in the list of `SetHotkeys`) of the hotkeys that the user pressed in this frame. Call it on
	/// the main thread, one time in each frame.
	[[nodiscard]] std::vector<size_t> Poll() const;

private:
	explicit HotkeyInput(const Managed& managed)
	    : _managed(&managed)
	{
	}

	[[nodiscard]] std::optional<int32_t> KeyCode(const char* name) const;
	[[nodiscard]] bool Call(const MethodInfo* method, int32_t key) const;

	/// A hotkey with the value of its key.
	struct Binding
	{
		Hotkey hotkey;
		int32_t key = 0;
	};

	const Managed* _managed;
	Il2CppClass* _keyCodeClass = nullptr;
	const MethodInfo* _anyKeyDown = nullptr;
	const MethodInfo* _getKeyDown = nullptr;
	const MethodInfo* _getKey = nullptr;
	const MethodInfo* _inputFieldActive = nullptr;
	/// The left key and the right key of Ctrl, Alt and Shift.
	std::array<std::array<int32_t, 2>, 3> _modifiers {};
	bool _enabled = false;
	std::vector<std::optional<Binding>> _bindings;
};

} // namespace moa_mod

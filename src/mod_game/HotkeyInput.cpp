/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "mod_game/HotkeyInput.h"

#include <spdlog/spdlog.h>

namespace moa_mod
{

namespace
{
/// The flag of a field that is a constant, such as a value of an enum.
constexpr int k_FieldLiteralFlag = 0x0040;
constexpr std::array<std::array<const char*, 2>, 3> k_ModifierNames {{
    {"LeftControl", "RightControl"},
    {"LeftAlt", "RightAlt"},
    {"LeftShift", "RightShift"},
}};
} // namespace

moa::Result<HotkeyInput> HotkeyInput::Bind(const Managed& managed)
{
	HotkeyInput input(managed);
	Binder binder(managed);
	auto* unityInput = binder.Class("UnityEngine", "Input");
	auto* utility = binder.Class("", "Utility");
	input._keyCodeClass = binder.Class("UnityEngine", "KeyCode");
	input._anyKeyDown = binder.Method(unityInput, "get_anyKeyDown", {}, "System.Boolean", true);
	input._getKeyDown = binder.Method(unityInput, "GetKeyDown", {"UnityEngine.KeyCode"}, "System.Boolean", true);
	input._getKey = binder.Method(unityInput, "GetKey", {"UnityEngine.KeyCode"}, "System.Boolean", true);
	input._inputFieldActive = binder.Method(utility, "IsInputFieldActive", {}, "System.Boolean", true);
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	for (size_t i = 0; i < k_ModifierNames.size(); ++i)
	{
		for (size_t side = 0; side < 2; ++side)
		{
			const auto value = input.KeyCode(k_ModifierNames[i][side]);
			if (!value)
			{
				return moa::MakeError("the enum UnityEngine.KeyCode has no value {}", k_ModifierNames[i][side]);
			}
			input._modifiers[i][side] = *value;
		}
	}
	return input;
}

std::optional<int32_t> HotkeyInput::KeyCode(const char* name) const
{
	auto* field = _managed->il2cpp_class_get_field_from_name(_keyCodeClass, name);
	if (field == nullptr || (_managed->Api().il2cpp_field_get_flags(field) & k_FieldLiteralFlag) == 0)
	{
		return std::nullopt;
	}
	// The values of KeyCode fit in 32 bits. The buffer is larger, so that a wider enum cannot write past it.
	int64_t value = 0;
	_managed->Api().il2cpp_field_static_get_value(field, &value);
	return static_cast<int32_t>(value);
}

void HotkeyInput::SetHotkeys(bool enabled, std::span<const NamedHotkey> hotkeys)
{
	_enabled = enabled;
	_bindings.assign(hotkeys.size(), std::nullopt);
	for (size_t index = 0; index < hotkeys.size(); ++index)
	{
		const auto& [name, hotkey] = hotkeys[index];
		if (!hotkey.Enabled())
		{
			continue;
		}
		if (const auto key = KeyCode(hotkey.key.c_str()); key)
		{
			_bindings[index] = Binding {.hotkey = hotkey, .key = *key};
		}
		else
		{
			spdlog::error("hotkeys: Unity has no key {}; the hotkey {} is off", hotkey.key, name);
		}
	}
}

bool HotkeyInput::Call(const MethodInfo* method, int32_t key) const
{
	auto result = _managed->Call<bool>(method, nullptr, {&key});
	return result && *result;
}

std::vector<size_t> HotkeyInput::Poll() const
{
	std::vector<size_t> actions;
	if (!_enabled)
	{
		return actions;
	}
	// One call in most frames: no key went down.
	if (auto any = _managed->Call<bool>(_anyKeyDown, nullptr); !any || !*any)
	{
		return actions;
	}
	std::optional<ModifierKeys> held;
	for (size_t action = 0; action < _bindings.size(); ++action)
	{
		const auto& binding = _bindings[action];
		if (!binding || !Call(_getKeyDown, binding->key))
		{
			continue;
		}
		if (!held)
		{
			const auto down = [&](size_t index) {
				return Call(_getKey, _modifiers[index][0]) || Call(_getKey, _modifiers[index][1]);
			};
			held = ModifierKeys {.control = down(0), .alt = down(1), .shift = down(2)};
		}
		if (HotkeyFires(binding->hotkey, *held, true))
		{
			actions.push_back(action);
		}
	}
	if (!actions.empty())
	{
		// The user types in a text field of the game: the keys are text, not hotkeys.
		if (auto typing = _managed->Call<bool>(_inputFieldActive, nullptr); !typing || *typing)
		{
			actions.clear();
		}
	}
	return actions;
}

} // namespace moa_mod

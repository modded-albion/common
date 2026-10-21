/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

#include "mod_game/Managed.h"

namespace moa_mod
{

/// Short messages for the user on the screen of the game, for example "Camera mod: on". The message is a popup of the
/// game (`MAPopup`, the popup of the HUD tags) on the HUD of the town. The log gets each message too.
///
/// The names are found at the first message. If a name is missing or a call fails, the messages go only to the log
/// from then on. When the game has no town HUD (for example on the title screen), the message goes only to the log.
/// Call all functions on the main thread.
class GameMessages
{
public:
	explicit GameMessages(const Managed& managed)
	    : _managed(managed)
	{
	}
	GameMessages(const GameMessages&) = delete;
	GameMessages& operator=(const GameMessages&) = delete;

	/// Show a message for a time. A new message replaces the message on the screen.
	void Show(const std::string& text, double seconds);
	/// Remove the message when its time is over. Call it one time in each frame.
	void Tick();
	/// Remove the message now.
	void Hide();

	/// Describe the state, for the query `mods.settings`.
	[[nodiscard]] nlohmann::json Describe() const;

private:
	enum class State
	{
		NotBound,
		Ready,
		LogOnly,
	};

	void Bind();
	void Fail(const std::string& error);
	void Place(Il2CppObject* popup) const;

	const Managed& _managed;
	State _state = State::NotBound;
	std::string _error;
	const MethodInfo* _create = nullptr;
	const MethodInfo* _destroy = nullptr;
	const MethodInfo* _transform = nullptr;
	const MethodInfo* _setAnchorMin = nullptr;
	const MethodInfo* _setAnchorMax = nullptr;
	const MethodInfo* _setPivot = nullptr;
	const MethodInfo* _setAnchoredPosition = nullptr;
	size_t _hudHolder = 0;
	Il2CppObject* _popup = nullptr;
	uintptr_t _handle = 0;                ///< The GC handle that keeps the managed object of the popup.
	const MethodInfo* _isAlive = nullptr; ///< `UnityEngine.Object.op_Implicit`: false for a destroyed object.
	std::chrono::steady_clock::time_point _hideAt {};
	uint64_t _shown = 0;
};

} // namespace moa_mod

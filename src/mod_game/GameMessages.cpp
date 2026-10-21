/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "mod_game/GameMessages.h"

#include <array>

#include <spdlog/spdlog.h>

namespace moa_mod
{

namespace
{
/// The preferred width of the popup, in pixels of the HUD canvas.
constexpr float k_PopupWidth = 520.0f;
/// The place of the popup on the HUD: the centre, near the top.
constexpr std::array<float, 2> k_PopupAnchor {0.5f, 0.85f};
constexpr std::array<float, 2> k_PopupPivot {0.5f, 0.5f};
} // namespace

void GameMessages::Bind()
{
	_state = State::LogOnly;
	const auto& m = _managed;
	if (m.il2cpp_gchandle_new == nullptr || m.il2cpp_gchandle_free == nullptr)
	{
		Fail("GameAssembly.dll does not export the GC handle functions");
		return;
	}
	Binder binder(m);
	auto* popup = binder.Class("", "MAPopup");
	auto* manager = binder.Class("", "NGManager");
	auto* component = binder.Class("UnityEngine", "Component");
	auto* rect = binder.Class("UnityEngine", "RectTransform");
	_create = binder.Method(popup, "Create", {"System.String", "UnityEngine.Transform", "System.Single", "System.Boolean"},
	                        "MAPopup", true);
	_destroy = binder.Method(popup, "DestroyMe", {}, "System.Void");
	_hudHolder = binder.Field(manager, "m_HUDHolder", "UnityEngine.Transform");
	_transform = binder.Method(component, "get_transform", {}, "UnityEngine.Transform");
	auto* unityObject = binder.Class("UnityEngine", "Object");
	_isAlive = binder.Method(unityObject, "op_Implicit", {"UnityEngine.Object"}, "System.Boolean", true);
	if (!binder.Errors().empty())
	{
		Fail(binder.Errors());
		return;
	}
	// The place of the popup is optional: without these methods, the popup stays where the game puts it.
	Binder placeBinder(m);
	_setAnchorMin = placeBinder.Method(rect, "set_anchorMin", {"UnityEngine.Vector2"}, "System.Void");
	_setAnchorMax = placeBinder.Method(rect, "set_anchorMax", {"UnityEngine.Vector2"}, "System.Void");
	_setPivot = placeBinder.Method(rect, "set_pivot", {"UnityEngine.Vector2"}, "System.Void");
	_setAnchoredPosition = placeBinder.Method(rect, "set_anchoredPosition", {"UnityEngine.Vector2"}, "System.Void");
	if (!placeBinder.Errors().empty())
	{
		spdlog::warn("mod messages: the popup stays where the game puts it: {}", placeBinder.Errors());
		_setAnchorMin = _setAnchorMax = _setPivot = _setAnchoredPosition = nullptr;
	}
	_state = State::Ready;
	spdlog::info("mod messages: the messages use the popup of the game");
}

void GameMessages::Fail(const std::string& error)
{
	_state = State::LogOnly;
	_error = error;
	spdlog::error("mod messages: {}; the messages go only to the log", error);
}

void GameMessages::Place(Il2CppObject* popup) const
{
	if (_setAnchorMin == nullptr)
	{
		return;
	}
	auto transform = _managed.Invoke(_transform, popup);
	if (!transform || *transform == nullptr)
	{
		return;
	}
	// The transform of a UI object is a RectTransform. The calls fail with an error, not a crash, if it is not.
	auto anchor = k_PopupAnchor;
	auto pivot = k_PopupPivot;
	std::array<float, 2> zero {};
	for (const auto& [method, value] : {std::pair {_setAnchorMin, anchor.data()}, std::pair {_setAnchorMax, anchor.data()},
	                                    std::pair {_setPivot, pivot.data()}, std::pair {_setAnchoredPosition, zero.data()}})
	{
		if (auto result = _managed.Invoke(method, *transform, {value}); !result)
		{
			spdlog::warn("mod messages: the mod could not move the popup: {}", result.GetError().message);
			return;
		}
	}
}

void GameMessages::Show(const std::string& text, double seconds)
{
	spdlog::info("mod message: {}", text);
	if (_state == State::NotBound)
	{
		Bind();
	}
	if (_state != State::Ready)
	{
		return;
	}
	Hide();
	// The popup needs the HUD of the town. On the title screen and during a load, the message goes only to the log.
	auto manager = _managed.SingletonInstance("NGManager");
	if (!manager)
	{
		return;
	}
	auto* holder = ReadField<Il2CppObject*>(*manager, _hudHolder);
	if (holder == nullptr)
	{
		return;
	}
	auto width = k_PopupWidth;
	bool onHud = true;
	auto popup = _managed.Invoke(_create, nullptr, {_managed.String(text.c_str()), holder, &width, &onHud});
	if (!popup)
	{
		Fail(popup.GetError().message);
		return;
	}
	if (*popup == nullptr)
	{
		Fail("the game made no popup");
		return;
	}
	_popup = *popup;
	_handle = _managed.il2cpp_gchandle_new(_popup, false);
	_hideAt = std::chrono::steady_clock::now() +
	          std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
	_shown += 1;
	Place(_popup);
}

void GameMessages::Tick()
{
	if (_popup != nullptr && std::chrono::steady_clock::now() >= _hideAt)
	{
		Hide();
	}
}

void GameMessages::Hide()
{
	if (_popup == nullptr)
	{
		return;
	}
	// The game can destroy the popup first, for example at a change of scene or when the user closes it. Then the mod
	// only removes its GC handle.
	const auto alive = _managed.Call<bool>(_isAlive, nullptr, {_popup});
	if (alive && *alive)
	{
		if (auto result = _managed.Invoke(_destroy, _popup); !result)
		{
			spdlog::info("mod messages: the popup was already gone: {}", result.GetError().message);
		}
	}
	else
	{
		spdlog::info("mod messages: the game destroyed the popup before the mod");
	}
	_managed.il2cpp_gchandle_free(_handle);
	_handle = 0;
	_popup = nullptr;
}

nlohmann::json GameMessages::Describe() const
{
	const char* state = _state == State::Ready ? "game popup" : _state == State::LogOnly ? "log only" : "not used yet";
	return {{"method", state}, {"error", _error}, {"shown", _shown}, {"visible", _popup != nullptr}};
}

} // namespace moa_mod

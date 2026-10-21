/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include <moa/common/Result.h>
#include <moa_mod/FrameCallbacks.h>
#include <moa_mod/SlotSwap.h>
#include <nlohmann/json.hpp>

#include "moa_mod/Il2Cpp.h"

namespace moa_mod
{

/// Find a class by its namespace and name in all loaded assemblies. Return null if no assembly has it.
[[nodiscard]] Il2CppClass* FindClass(const Il2CppApi& api, const char* space, const char* name);

/// Find a method of a class by its name and its count of parameters.
[[nodiscard]] moa::Result<const MethodInfo*> FindMethod(const Il2CppApi& api, const char* space, const char* className,
                                                        const char* methodName, int parameterCount);

/// A hook on a managed method that has no return value and no parameters (for example the `Update` of a
/// MonoBehaviour), or two parameters that each fit in one register (an object, or a value type of 8 bytes or less). The hook
/// puts its own code pointer into the method. Unity then calls the hook, the hook calls the functions in `Before`, the original
/// method and the functions in `After`. Unity calls a message such as `Update` through the method, so the hook sees each frame.
/// Code of the game that calls the method directly does not go through the hook.
///
/// The object puts the original code pointer back when it goes out of scope.
class MethodHook
{
public:
	~MethodHook();
	MethodHook(const MethodHook&) = delete;
	MethodHook& operator=(const MethodHook&) = delete;

	/// Install the hook on `space.className.methodName`. `parameterTypes` are the C# full names of the parameters:
	/// none, or two types that each fit in one register. Return an error, and change nothing, if the method does not
	/// exist, has other parameters, shares its code with another method, or if all hook places are in use.
	[[nodiscard]] static moa::Result<std::unique_ptr<MethodHook>>
	Install(const Il2CppApi& api, const char* space, const char* className, const char* methodName,
	        std::initializer_list<std::string_view> parameterTypes = {});

	/// The functions that run before the original method, on the main thread.
	[[nodiscard]] FrameCallbacks& Before();
	/// The functions that run after the original method, on the main thread.
	[[nodiscard]] FrameCallbacks& After();

	/// Put the original code pointer back. Return false if another program changed the pointer after the install.
	/// With `log` false, the function only does atomic writes. `DllMain` uses that form, because a lock can stop
	/// the process at exit.
	bool Remove(bool log = true);
	/// A parameter of the current call (or of the last call) of a method with two parameters: the value of a value
	/// type, or the object. Read it only on the main thread, during a call.
	[[nodiscard]] void* Argument(size_t index) const;

	/// The object of the current call (or of the last call) of the method, for example the `GameManager`. The
	/// functions in `Before` and `After` use it. Read it only on the main thread, during a call.
	[[nodiscard]] void* Instance() const;

	[[nodiscard]] bool Installed() const;
	[[nodiscard]] nlohmann::json Describe() const;

	struct Slot;

	/// Only `Install` can make a key, so only `Install` can make a hook.
	class Key
	{
		friend class MethodHook;
		Key() = default;
	};

	MethodHook(Key key, Slot& slot, std::string name);

private:
	Slot& _slot;
	std::string _name;
};

} // namespace moa_mod

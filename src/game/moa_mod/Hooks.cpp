/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/Hooks.h"

#include <array>
#include <atomic>
#include <format>
#include <utility>

#include <spdlog/spdlog.h>
#include <windows.h>

namespace moa_mod
{

/// One place for a hook. The game calls a fixed function for each place, because the code pointer of a method
/// cannot carry data. Thus the places are in a table that lives as long as the process.
struct MethodHook::Slot
{
	std::atomic<bool> inUse = false;
	std::atomic<void*> original = nullptr;
	std::atomic<uint64_t> calls = 0;
	std::atomic<void*> self = nullptr;
	std::array<std::atomic<void*>, 2> arguments {};
	SlotSwap swap;
	FrameCallbacks before;
	FrameCallbacks after;
};

namespace
{
constexpr size_t k_SlotCount = 6;
constexpr uint32_t k_MethodStaticFlag = 0x0010;

using ManagedMethod = void (*)(void* self, const MethodInfo* method);
using ManagedMethodWithTwoParameters = void (*)(void* self, void* first, void* second, const MethodInfo* method);

std::array<MethodHook::Slot, k_SlotCount> s_slots;

/// The code that the game calls in place of the method of the hook in place `Index`. A managed exception from the
/// original method goes back to the game through this function, as if the hook did not exist.
template <size_t Index>
void HookEntry(void* self, const MethodInfo* method)
{
	auto& slot = s_slots[Index];
	slot.calls.fetch_add(1, std::memory_order_relaxed);
	slot.self.store(self, std::memory_order_relaxed);
	slot.before.Run();
	if (const auto original = reinterpret_cast<ManagedMethod>(slot.original.load()); original != nullptr)
	{
		original(self, method);
	}
	slot.after.Run();
}

/// The code that the game calls in place of a method with two parameters that each fit in one register.
template <size_t Index>
void HookEntryWithTwoParameters(void* self, void* first, void* second, const MethodInfo* method)
{
	auto& slot = s_slots[Index];
	slot.calls.fetch_add(1, std::memory_order_relaxed);
	slot.self.store(self, std::memory_order_relaxed);
	slot.arguments[0].store(first, std::memory_order_relaxed);
	slot.arguments[1].store(second, std::memory_order_relaxed);
	slot.before.Run();
	if (const auto original = reinterpret_cast<ManagedMethodWithTwoParameters>(slot.original.load()); original != nullptr)
	{
		original(self, first, second, method);
	}
	slot.after.Run();
}

template <size_t... Indices>
constexpr std::array<void*, k_SlotCount> MakeEntries(std::index_sequence<Indices...> /*indices*/)
{
	return {reinterpret_cast<void*>(&HookEntry<Indices>)...};
}

template <size_t... Indices>
constexpr std::array<void*, k_SlotCount> MakeEntriesWithTwoParameters(std::index_sequence<Indices...> /*indices*/)
{
	return {reinterpret_cast<void*>(&HookEntryWithTwoParameters<Indices>)...};
}

const std::array<void*, k_SlotCount> k_Entries = MakeEntries(std::make_index_sequence<k_SlotCount>());
const std::array<void*, k_SlotCount> k_EntriesWithTwoParameters =
    MakeEntriesWithTwoParameters(std::make_index_sequence<k_SlotCount>());

/// Return true if the process can write to the memory at `address`.
[[nodiscard]] bool IsWritable(const void* address)
{
	MEMORY_BASIC_INFORMATION information {};
	if (VirtualQuery(address, &information, sizeof(information)) == 0 || information.State != MEM_COMMIT)
	{
		return false;
	}
	constexpr DWORD writable = PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
	return (information.Protect & writable) != 0 && (information.Protect & PAGE_GUARD) == 0;
}

/// The code pointer is the first field of a MethodInfo. The dump uses the same rule to get the RVA of a method.
[[nodiscard]] void** CodeSlot(const MethodInfo* method)
{
	return reinterpret_cast<void**>(const_cast<MethodInfo*>(method));
}

/// Count the methods of all classes that use the code at `code`. Generic methods and small methods often share one
/// body. A hook on a shared body would change all of them.
[[nodiscard]] size_t CountMethodsWithCode(const Il2CppApi& api, const void* code)
{
	size_t count = 0;
	for (const auto& assembly : ListClasses(api))
	{
		for (const auto* klass : assembly.classes)
		{
			void* iterator = nullptr;
			while (const auto* method = api.il2cpp_class_get_methods(klass, &iterator))
			{
				count += *CodeSlot(method) == code ? 1 : 0;
			}
		}
	}
	return count;
}
} // namespace

Il2CppClass* FindClass(const Il2CppApi& api, const char* space, const char* name)
{
	size_t count = 0;
	const auto** assemblies = api.il2cpp_domain_get_assemblies(api.il2cpp_domain_get(), &count);
	for (size_t i = 0; i < count; ++i)
	{
		const auto* image = api.il2cpp_assembly_get_image(assemblies[i]);
		if (auto* klass = api.il2cpp_class_from_name(image, space, name); klass != nullptr)
		{
			return klass;
		}
	}
	return nullptr;
}

moa::Result<const MethodInfo*> FindMethod(const Il2CppApi& api, const char* space, const char* className,
                                          const char* methodName, int parameterCount)
{
	const auto* klass = FindClass(api, space, className);
	if (klass == nullptr)
	{
		return moa::MakeError("no assembly has the class '{}.{}'", space, className);
	}
	const auto* method = api.il2cpp_class_get_method_from_name(klass, methodName, parameterCount);
	if (method == nullptr)
	{
		return moa::MakeError("the class '{}' has no method '{}' with {} parameters", className, methodName, parameterCount);
	}
	return method;
}

MethodHook::MethodHook(Key /*key*/, Slot& slot, std::string name)
    : _slot(slot)
    , _name(std::move(name))
{
}

MethodHook::~MethodHook()
{
	Remove();
	_slot.before.Clear();
	_slot.after.Clear();
	_slot.inUse = false;
}

moa::Result<std::unique_ptr<MethodHook>> MethodHook::Install(const Il2CppApi& api, const char* space, const char* className,
                                                             const char* methodName,
                                                             const std::vector<std::string>& parameterTypes)
{
	const auto name = std::format("{}.{}.{}", space, className, methodName);
	if (parameterTypes.size() != 0 && parameterTypes.size() != 2)
	{
		return moa::MakeError("a hook on '{}' needs no parameters or two parameters", name);
	}
	auto method = FindMethod(api, space, className, methodName, static_cast<int>(parameterTypes.size()));
	if (!method)
	{
		return method.GetError();
	}
	uint32_t index = 0;
	for (const auto& expected : parameterTypes)
	{
		if (const auto type = api.TypeName(api.il2cpp_method_get_param(*method, index)); type != expected)
		{
			return moa::MakeError("the parameter {} of '{}' is '{}', not '{}'", index, name, type, expected);
		}
		++index;
	}
	uint32_t implementationFlags = 0;
	if ((api.il2cpp_method_get_flags(*method, &implementationFlags) & k_MethodStaticFlag) != 0)
	{
		return moa::MakeError("the method '{}' is static", name);
	}
	if (const auto returnType = api.TypeName(api.il2cpp_method_get_return_type(*method)); returnType != "System.Void")
	{
		return moa::MakeError("the method '{}' returns '{}', not System.Void", name, returnType);
	}
	auto** codeSlot = CodeSlot(*method);
	if (!IsWritable(codeSlot))
	{
		return moa::MakeError("the mod cannot write to the method '{}'", name);
	}
	auto* code = *codeSlot;
	if (code == nullptr)
	{
		return moa::MakeError("the method '{}' has no code", name);
	}
	if (const auto users = CountMethodsWithCode(api, code); users != 1)
	{
		return moa::MakeError("the code of '{}' is shared by {} methods", name, users);
	}

	for (size_t i = 0; i < k_SlotCount; ++i)
	{
		auto& slot = s_slots[i];
		bool expected = false;
		if (!slot.inUse.compare_exchange_strong(expected, true))
		{
			continue;
		}
		slot.calls = 0;
		slot.self = nullptr;
		slot.original = code;
		slot.swap =
		    SlotSwap::Install(codeSlot, code, parameterTypes.size() == 0 ? k_Entries[i] : k_EntriesWithTwoParameters[i]);
		if (!slot.swap.Installed())
		{
			slot.original = nullptr;
			slot.inUse = false;
			return moa::MakeError("the code pointer of '{}' changed during the install", name);
		}
		spdlog::info("hook: installed on {}", name);
		return std::make_unique<MethodHook>(Key(), slot, name);
	}
	return moa::MakeError("all {} hook places are in use", k_SlotCount);
}

FrameCallbacks& MethodHook::Before()
{
	return _slot.before;
}

FrameCallbacks& MethodHook::After()
{
	return _slot.after;
}

bool MethodHook::Remove(bool log)
{
	if (!_slot.swap.Installed())
	{
		return true;
	}
	const auto restored = _slot.swap.Restore();
	if (!log)
	{
		return restored;
	}
	spdlog::info("hook: removed from {} ({})", _name, restored ? "original code back" : "another program changed it");
	// The entry function keeps the original pointer: a call that started before the removal must still complete.
	return restored;
}

void* MethodHook::Instance() const
{
	return _slot.self.load(std::memory_order_relaxed);
}

void* MethodHook::Argument(size_t index) const
{
	return index < _slot.arguments.size() ? _slot.arguments[index].load(std::memory_order_relaxed) : nullptr;
}

bool MethodHook::Installed() const
{
	return _slot.swap.Installed();
}

nlohmann::json MethodHook::Describe() const
{
	auto describe = [](const FrameCallbacks& callbacks) {
		auto list = nlohmann::json::array();
		for (const auto& status : callbacks.Statuses())
		{
			list.push_back(
			    {{"name", status.name}, {"enabled", status.enabled}, {"calls", status.calls}, {"error", status.error}});
		}
		return list;
	};
	return {
	    {"method", _name},
	    {"installed", Installed()},
	    {"calls", _slot.calls.load()},
	    {"before", describe(_slot.before)},
	    {"after", describe(_slot.after)},
	};
}

} // namespace moa_mod

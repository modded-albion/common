/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "mod_game/Il2Cpp.h"

#include <windows.h>

namespace moa_mod
{

moa::Result<Il2CppApi> Il2CppApi::Load()
{
	const auto module = GetModuleHandleW(L"GameAssembly.dll");
	if (module == nullptr)
	{
		return moa::MakeError("GameAssembly.dll is not loaded");
	}

	Il2CppApi api;
	api.gameAssemblyBase = reinterpret_cast<uintptr_t>(module);
	std::string missing;
#define MOA_IL2CPP_LOAD(ret, name, params)                                     \
	api.name = reinterpret_cast<ret(*) params>(GetProcAddress(module, #name)); \
	if (api.name == nullptr)                                                   \
	{                                                                          \
		missing += missing.empty() ? #name : ", " #name;                       \
	}
	MOA_IL2CPP_FUNCTIONS(MOA_IL2CPP_LOAD)
#undef MOA_IL2CPP_LOAD

	if (!missing.empty())
	{
		return moa::MakeError("GameAssembly.dll does not export: {}", missing);
	}
	return api;
}

std::string Il2CppApi::TypeName(const Il2CppType* type) const
{
	if (type == nullptr)
	{
		return {};
	}
	auto* name = il2cpp_type_get_name(type);
	if (name == nullptr)
	{
		return {};
	}
	std::string result(name);
	il2cpp_free(name);
	return result;
}

uint64_t Il2CppApi::MethodRva(const MethodInfo* method) const
{
	// The first field of MethodInfo is the pointer to the code of the method.
	const auto code = *reinterpret_cast<const uintptr_t*>(method);
	return code > gameAssemblyBase ? code - gameAssemblyBase : 0;
}

std::string Il2CppApi::FullName(const Il2CppClass* klass) const
{
	std::string name = il2cpp_class_get_name(klass);
	for (auto* outer = il2cpp_class_get_declaring_type(klass); outer != nullptr; outer = il2cpp_class_get_declaring_type(outer))
	{
		name = std::string(il2cpp_class_get_name(outer)) + "/" + name;
		klass = outer;
	}
	const std::string space = il2cpp_class_get_namespace(klass);
	return space.empty() ? name : space + "." + name;
}

std::vector<AssemblyClasses> ListClasses(const Il2CppApi& api)
{
	std::vector<AssemblyClasses> result;
	size_t assemblyCount = 0;
	const auto** assemblies = api.il2cpp_domain_get_assemblies(api.il2cpp_domain_get(), &assemblyCount);
	for (size_t i = 0; i < assemblyCount; ++i)
	{
		const auto* image = api.il2cpp_assembly_get_image(assemblies[i]);
		AssemblyClasses assembly {.name = api.il2cpp_image_get_name(image), .classes = {}};
		const auto count = api.il2cpp_image_get_class_count(image);
		assembly.classes.reserve(count);
		for (size_t j = 0; j < count; ++j)
		{
			if (const auto* klass = api.il2cpp_image_get_class(image, j); klass != nullptr)
			{
				assembly.classes.push_back(klass);
			}
		}
		result.push_back(std::move(assembly));
	}
	return result;
}

} // namespace moa_mod

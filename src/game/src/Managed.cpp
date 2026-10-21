/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa_mod/Managed.h"

#include <vector>

#include <windows.h>

#include "moa_mod/Hooks.h"

namespace moa_mod
{

namespace
{
constexpr uint32_t k_MethodStaticFlag = 0x0010;
constexpr uint32_t k_FieldStaticFlag = 0x0010;
} // namespace

moa::Result<Managed> Managed::Load(const Il2CppApi& api)
{
	const auto module = GetModuleHandleW(L"GameAssembly.dll");
	if (module == nullptr)
	{
		return moa::MakeError("GameAssembly.dll is not loaded");
	}
	Managed managed;
	managed._api = &api;
	std::string missing;
#define MOA_IL2CPP_LOAD(ret, name, params)                                         \
	managed.name = reinterpret_cast<ret(*) params>(GetProcAddress(module, #name)); \
	if (managed.name == nullptr)                                                   \
	{                                                                              \
		missing += missing.empty() ? #name : ", " #name;                           \
	}
	MOA_IL2CPP_MOD_FUNCTIONS(MOA_IL2CPP_LOAD)
#undef MOA_IL2CPP_LOAD
#define MOA_IL2CPP_LOAD_OPTIONAL(ret, name, params) \
	managed.name = reinterpret_cast<ret(*) params>(GetProcAddress(module, #name));
	MOA_IL2CPP_OPTIONAL_FUNCTIONS(MOA_IL2CPP_LOAD_OPTIONAL)
#undef MOA_IL2CPP_LOAD_OPTIONAL
	if (!missing.empty())
	{
		return moa::MakeError("GameAssembly.dll does not export: {}", missing);
	}
	return managed;
}

moa::Result<Il2CppClass*> Managed::Class(const char* space, const char* name) const
{
	auto* klass = FindClass(*_api, space, name);
	if (klass == nullptr)
	{
		return moa::MakeError("no assembly has the class '{}{}{}'", space, *space == '\0' ? "" : ".", name);
	}
	return klass;
}

moa::Result<const MethodInfo*> Managed::Method(const Il2CppClass* klass, const char* name,
                                               std::initializer_list<std::string_view> parameterTypes,
                                               std::string_view returnType, bool isStatic) const
{
	const auto& api = *_api;
	const auto describe = [&] {
		std::string text = std::format("{}.{}(", api.il2cpp_class_get_name(klass), name);
		for (const auto& type : parameterTypes)
		{
			text += std::format("{}{}", text.ends_with('(') ? "" : ", ", type);
		}
		return text + std::format(") -> {}{}", returnType, isStatic ? ", static" : "");
	};
	void* iterator = nullptr;
	while (const auto* method = api.il2cpp_class_get_methods(klass, &iterator))
	{
		if (std::string_view(api.il2cpp_method_get_name(method)) != name ||
		    api.il2cpp_method_get_param_count(method) != parameterTypes.size())
		{
			continue;
		}
		bool same = api.TypeName(api.il2cpp_method_get_return_type(method)) == returnType;
		uint32_t index = 0;
		for (const auto& type : parameterTypes)
		{
			same = same && api.TypeName(api.il2cpp_method_get_param(method, index++)) == type;
		}
		if (!same)
		{
			continue;
		}
		uint32_t implementationFlags = 0;
		const auto methodIsStatic = (api.il2cpp_method_get_flags(method, &implementationFlags) & k_MethodStaticFlag) != 0;
		if (methodIsStatic != isStatic)
		{
			return moa::MakeError("the method {} is {}", describe(), methodIsStatic ? "static" : "not static");
		}
		return method;
	}
	return moa::MakeError("no method {}", describe());
}

moa::Result<size_t> Managed::Field(Il2CppClass* klass, const char* name, std::string_view typeName) const
{
	const auto& api = *_api;
	auto* field = il2cpp_class_get_field_from_name(klass, name);
	if (field == nullptr)
	{
		return moa::MakeError("the class '{}' has no field '{}'", api.il2cpp_class_get_name(klass), name);
	}
	if ((api.il2cpp_field_get_flags(field) & k_FieldStaticFlag) != 0)
	{
		return moa::MakeError("the field '{}.{}' is static", api.il2cpp_class_get_name(klass), name);
	}
	if (const auto type = api.TypeName(api.il2cpp_field_get_type(field)); type != typeName)
	{
		return moa::MakeError("the field '{}.{}' has the type '{}', not '{}'", api.il2cpp_class_get_name(klass), name, type,
		                      typeName);
	}
	return api.il2cpp_field_get_offset(field);
}

moa::Result<FieldInfo*> Managed::StaticField(Il2CppClass* klass, const char* name, std::string_view typeName) const
{
	const auto& api = *_api;
	auto* field = il2cpp_class_get_field_from_name(klass, name);
	if (field == nullptr)
	{
		return moa::MakeError("the class '{}' has no field '{}'", api.il2cpp_class_get_name(klass), name);
	}
	if ((api.il2cpp_field_get_flags(field) & k_FieldStaticFlag) == 0)
	{
		return moa::MakeError("the field '{}.{}' is not static", api.il2cpp_class_get_name(klass), name);
	}
	if (const auto type = api.TypeName(api.il2cpp_field_get_type(field)); type != typeName)
	{
		return moa::MakeError("the field '{}.{}' has the type '{}', not '{}'", api.il2cpp_class_get_name(klass), name, type,
		                      typeName);
	}
	return field;
}

moa::Result<Il2CppObject*> Managed::Invoke(const MethodInfo* method, void* self, std::initializer_list<void*> arguments) const
{
	return InvokeArray(method, self, std::span<void* const>(arguments.begin(), arguments.size()));
}

moa::Result<Il2CppObject*> Managed::InvokeArray(const MethodInfo* method, void* self, std::span<void* const> arguments) const
{
	if (method == nullptr)
	{
		return moa::MakeError("the method is not known");
	}
	uint32_t implementationFlags = 0;
	if (self == nullptr && (_api->il2cpp_method_get_flags(method, &implementationFlags) & k_MethodStaticFlag) == 0)
	{
		return moa::MakeError("the object for '{}' is null", _api->il2cpp_method_get_name(method));
	}
	std::vector<void*> values(arguments.begin(), arguments.end());
	Il2CppObject* exception = nullptr;
	auto* result = il2cpp_runtime_invoke(method, self, values.empty() ? nullptr : values.data(), &exception);
	if (exception != nullptr)
	{
		return moa::MakeError("'{}' threw {}", _api->il2cpp_method_get_name(method),
		                      _api->FullName(il2cpp_object_get_class(exception)));
	}
	return result;
}

moa::Result<Il2CppObject*> Managed::SingletonInstance(const char* className) const
{
	auto klass = Class("", className);
	if (!klass)
	{
		return klass.GetError();
	}
	const auto* parent = _api->il2cpp_class_get_parent(*klass);
	if (parent == nullptr || std::string_view(_api->il2cpp_class_get_name(parent)) != "MonoSingleton`1")
	{
		return moa::MakeError("the class '{}' does not derive from MonoSingleton", className);
	}
	const auto* getter = _api->il2cpp_class_get_method_from_name(parent, "get_Me", 0);
	if (getter == nullptr)
	{
		return moa::MakeError("MonoSingleton<{}> has no property 'Me'", className);
	}
	auto instance = Invoke(getter, nullptr);
	if (!instance)
	{
		return instance.GetError();
	}
	if (*instance == nullptr)
	{
		return moa::MakeError("there is no {} now", className);
	}
	return *instance;
}

Il2CppObject* Managed::String(const char* text) const
{
	return il2cpp_string_new(text);
}

std::string Managed::Text(Il2CppObject* string) const
{
	if (string == nullptr)
	{
		return {};
	}
	const auto length = il2cpp_string_length(string);
	const auto* characters = reinterpret_cast<const wchar_t*>(il2cpp_string_chars(string));
	if (length <= 0 || characters == nullptr)
	{
		return {};
	}
	const auto size = WideCharToMultiByte(CP_UTF8, 0, characters, length, nullptr, 0, nullptr, nullptr);
	std::string text(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, characters, length, text.data(), size, nullptr, nullptr);
	return text;
}

Il2CppObject* Managed::TypeObject(const Il2CppClass* klass) const
{
	return il2cpp_type_get_object(_api->il2cpp_class_get_type(klass));
}

moa::Result<Il2CppObject*> Managed::NewArray(Il2CppClass* element, size_t count, size_t itemSize) const
{
	if (il2cpp_array_new == nullptr || il2cpp_array_object_header_size == nullptr ||
	    il2cpp_class_array_element_size == nullptr || il2cpp_array_length == nullptr)
	{
		return moa::MakeError("GameAssembly.dll does not export the array functions");
	}
	if (element == nullptr)
	{
		return moa::MakeError("the class of the array items is not known");
	}
	const auto size = il2cpp_class_array_element_size(element);
	if (size <= 0 || static_cast<size_t>(size) != itemSize)
	{
		return moa::MakeError("an item of {}[] has {} bytes, not {}", _api->FullName(element), size, itemSize);
	}
	auto* array = il2cpp_array_new(element, count);
	if (array == nullptr)
	{
		return moa::MakeError("the game made no array of {} items of {}", count, _api->FullName(element));
	}
	return array;
}

std::byte* Managed::ArrayData(Il2CppObject* array) const
{
	return reinterpret_cast<std::byte*>(array) + il2cpp_array_object_header_size();
}

size_t Managed::ArrayLength(Il2CppObject* array) const
{
	return array == nullptr || il2cpp_array_length == nullptr ? 0 : il2cpp_array_length(array);
}

Il2CppObject* Managed::ArrayItem(Il2CppObject* array, size_t index) const
{
	return ReadField<Il2CppObject*>(ArrayData(array), index * sizeof(Il2CppObject*));
}

void Managed::SetReferenceField(Il2CppObject* object, size_t offset, Il2CppObject* value) const
{
	auto** slot = reinterpret_cast<void**>(reinterpret_cast<std::byte*>(object) + offset);
	if (il2cpp_gc_wbarrier_set_field != nullptr)
	{
		il2cpp_gc_wbarrier_set_field(object, slot, value);
	}
	else
	{
		*slot = value;
	}
}

void Managed::SetArrayItem(Il2CppObject* array, size_t index, Il2CppObject* item) const
{
	auto** slot = reinterpret_cast<void**>(ArrayData(array) + index * sizeof(Il2CppObject*));
	if (il2cpp_gc_wbarrier_set_field != nullptr)
	{
		il2cpp_gc_wbarrier_set_field(array, slot, item);
	}
	else
	{
		*slot = item;
	}
}

void Binder::AddError(const moa::Error& error)
{
	_errors += _errors.empty() ? error.message : "; " + error.message;
}

Il2CppClass* Binder::Class(const char* space, const char* name)
{
	auto klass = _managed.Class(space, name);
	if (!klass)
	{
		AddError(klass.GetError());
		return nullptr;
	}
	return *klass;
}

const MethodInfo* Binder::Method(const Il2CppClass* klass, const char* name,
                                 std::initializer_list<std::string_view> parameterTypes, std::string_view returnType,
                                 bool isStatic)
{
	if (klass == nullptr)
	{
		return nullptr;
	}
	auto method = _managed.Method(klass, name, parameterTypes, returnType, isStatic);
	if (!method)
	{
		AddError(method.GetError());
		return nullptr;
	}
	return *method;
}

size_t Binder::Field(Il2CppClass* klass, const char* name, std::string_view typeName)
{
	if (klass == nullptr)
	{
		return 0;
	}
	auto offset = _managed.Field(klass, name, typeName);
	if (!offset)
	{
		AddError(offset.GetError());
		return 0;
	}
	return *offset;
}

} // namespace moa_mod

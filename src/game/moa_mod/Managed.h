/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

#include <moa/common/Result.h>

#include "moa_mod/Il2Cpp.h"

struct Il2CppObject;

namespace moa_mod
{

// The il2cpp_* exports that only the mods need. If one is missing, the mods stay off, but the dump and the
// inspector still work.
#define MOA_IL2CPP_MOD_FUNCTIONS(X)                                                             \
	X(Il2CppObject*, il2cpp_runtime_invoke, (const MethodInfo*, void*, void**, Il2CppObject**)) \
	X(Il2CppObject*, il2cpp_string_new, (const char*))                                          \
	X(void*, il2cpp_object_unbox, (Il2CppObject*))                                              \
	X(Il2CppClass*, il2cpp_object_get_class, (Il2CppObject*))                                   \
	X(FieldInfo*, il2cpp_class_get_field_from_name, (Il2CppClass*, const char*))                \
	X(Il2CppObject*, il2cpp_type_get_object, (const Il2CppType*))                               \
	X(uint16_t*, il2cpp_string_chars, (Il2CppObject*))                                          \
	X(int32_t, il2cpp_string_length, (Il2CppObject*))

// The il2cpp_* exports that only some parts of the mods need. If one is missing, it stays null, and only the part that
// needs it stays off. A GC handle of this IL2CPP version has the size of a pointer: a 32-bit handle loses its high bits.
#define MOA_IL2CPP_OPTIONAL_FUNCTIONS(X)                                                       \
	X(Il2CppObject*, il2cpp_method_get_object, (const MethodInfo*, Il2CppClass*))              \
	X(uintptr_t, il2cpp_gchandle_new, (Il2CppObject*, bool))                                   \
	X(void, il2cpp_gchandle_free, (uintptr_t))                                                 \
	X(void, il2cpp_field_static_set_value, (FieldInfo*, void*))                                \
	X(Il2CppObject*, il2cpp_object_new, (const Il2CppClass*))                                  \
	X(Il2CppClass*, il2cpp_class_from_system_type, (Il2CppObject*))                            \
	X(const MethodInfo*, il2cpp_object_get_virtual_method, (Il2CppObject*, const MethodInfo*)) \
	X(Il2CppObject*, il2cpp_array_new, (Il2CppClass*, uintptr_t))                              \
	X(uint32_t, il2cpp_array_length, (Il2CppObject*))                                          \
	X(Il2CppClass*, il2cpp_class_get_element_class, (Il2CppClass*))                            \
	X(int, il2cpp_array_object_header_size, ())                                                \
	X(int, il2cpp_class_array_element_size, (const Il2CppClass*))                              \
	X(void, il2cpp_gc_wbarrier_set_field, (Il2CppObject*, void**, void*)) X(void*, il2cpp_resolve_icall, (const char*))

/// Calls into managed code by name, for the mods. Every lookup checks the names and the types, and returns an
/// error if the game does not have them. Use it only on the main thread of the game, or on a thread that IL2CPP
/// attached, and change Unity objects only on the main thread.
class Managed
{
public:
#define MOA_IL2CPP_MEMBER(ret, name, params) ret(*name) params = nullptr;
	MOA_IL2CPP_MOD_FUNCTIONS(MOA_IL2CPP_MEMBER)
	MOA_IL2CPP_OPTIONAL_FUNCTIONS(MOA_IL2CPP_MEMBER)
#undef MOA_IL2CPP_MEMBER

	/// Find the exports that the mods need. Return an error that names the missing exports.
	[[nodiscard]] static moa::Result<Managed> Load(const Il2CppApi& api);

	[[nodiscard]] const Il2CppApi& Api() const { return *_api; }

	/// Find a class in all loaded assemblies.
	[[nodiscard]] moa::Result<Il2CppClass*> Class(const char* space, const char* name) const;

	/// Find a method by its name, its parameter types (as C# full names, for example "System.Single"), its return
	/// type and whether it is static. The match is exact, so an overload with other types is not used.
	[[nodiscard]] moa::Result<const MethodInfo*> Method(const Il2CppClass* klass, const char* name,
	                                                    std::initializer_list<std::string_view> parameterTypes,
	                                                    std::string_view returnType, bool isStatic) const;

	/// Find an instance field by its name and type, and return its offset in the object.
	[[nodiscard]] moa::Result<size_t> Field(Il2CppClass* klass, const char* name, std::string_view typeName) const;

	/// Find a static field by its name and type.
	[[nodiscard]] moa::Result<FieldInfo*> StaticField(Il2CppClass* klass, const char* name, std::string_view typeName) const;

	/// Read a static field of a value type.
	template <typename T>
	[[nodiscard]] T ReadStatic(FieldInfo* field) const
	{
		T value {};
		_api->il2cpp_field_static_get_value(field, &value);
		return value;
	}

	/// Write a static field of a value type. Return false if the game does not export the function.
	template <typename T>
	[[nodiscard]] bool WriteStatic(FieldInfo* field, T value) const
	{
		if (il2cpp_field_static_set_value == nullptr)
		{
			return false;
		}
		il2cpp_field_static_set_value(field, &value);
		return true;
	}

	/// Call a method. `self` is the object, or null for a static method. Each argument is a pointer to a value
	/// type, or an object for a reference type. A managed exception becomes an error.
	[[nodiscard]] moa::Result<Il2CppObject*> Invoke(const MethodInfo* method, void* self,
	                                                std::initializer_list<void*> arguments = {}) const;

	/// Call a method with a list of arguments that the caller makes at run time, as `Invoke`.
	[[nodiscard]] moa::Result<Il2CppObject*> InvokeArray(const MethodInfo* method, void* self,
	                                                     std::span<void* const> arguments) const;

	/// Call a method that returns a value type, and copy the value.
	template <typename T>
	[[nodiscard]] moa::Result<T> Call(const MethodInfo* method, void* self, std::initializer_list<void*> arguments = {}) const
	{
		auto boxed = Invoke(method, self, arguments);
		if (!boxed)
		{
			return boxed.GetError();
		}
		if (*boxed == nullptr)
		{
			return moa::MakeError("the method returned no value");
		}
		T value {};
		std::memcpy(&value, il2cpp_object_unbox(*boxed), sizeof(T));
		return value;
	}

	/// Return the static instance of a class that derives from `MonoSingleton<T>` (the `Me` property).
	[[nodiscard]] moa::Result<Il2CppObject*> SingletonInstance(const char* className) const;

	/// Make a managed string.
	[[nodiscard]] Il2CppObject* String(const char* text) const;

	/// Return the text of a managed string in UTF-8.
	[[nodiscard]] std::string Text(Il2CppObject* string) const;

	/// Return the `System.Type` object of a class, for methods such as `GetComponent(Type)`.
	[[nodiscard]] Il2CppObject* TypeObject(const Il2CppClass* klass) const;

	/// Make a managed array of `count` items of the class `element`. Return an error if the game does not export the
	/// array functions, or if an item of the array does not have `itemSize` bytes.
	[[nodiscard]] moa::Result<Il2CppObject*> NewArray(Il2CppClass* element, size_t count, size_t itemSize) const;

	/// Make a managed array of a value type and copy the items into it.
	template <typename T>
	[[nodiscard]] moa::Result<Il2CppObject*> ValueArray(Il2CppClass* element, std::span<const T> items) const
	{
		auto array = NewArray(element, items.size(), sizeof(T));
		if (array && !items.empty())
		{
			std::memcpy(ArrayData(*array), items.data(), items.size_bytes());
		}
		return array;
	}

	/// Return the address of the first item of a managed array.
	[[nodiscard]] std::byte* ArrayData(Il2CppObject* array) const;

	/// Return the count of the items of a managed array, or 0 for a null array.
	[[nodiscard]] size_t ArrayLength(Il2CppObject* array) const;

	/// Return the item `index` of a managed array of references.
	[[nodiscard]] Il2CppObject* ArrayItem(Il2CppObject* array, size_t index) const;

	/// Put a reference into the item `index` of a managed array of references, with the write barrier of the garbage
	/// collector when the game exports it.
	void SetArrayItem(Il2CppObject* array, size_t index, Il2CppObject* item) const;

	/// Put a reference into a field of a managed object at an offset that `Field` gave, with the write barrier of the
	/// garbage collector when the game exports it.
	void SetReferenceField(Il2CppObject* object, size_t offset, Il2CppObject* value) const;

private:
	const Il2CppApi* _api = nullptr;
};

/// Read a field of a managed object at an offset that `Managed::Field` gave.
template <typename T>
[[nodiscard]] T ReadField(const void* object, size_t offset)
{
	T value {};
	std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(T));
	return value;
}

/// Write a field of a managed object at an offset that `Managed::Field` gave. Use it only for value types: a
/// reference needs the write barrier of the garbage collector.
template <typename T>
void WriteField(void* object, size_t offset, const T& value)
{
	std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(T));
}

/// Collect the errors of many lookups, so that a mod can report all the missing names at one time.
class Binder
{
public:
	explicit Binder(const Managed& managed)
	    : _managed(managed)
	{
	}

	[[nodiscard]] const Managed& Get() const { return _managed; }

	Il2CppClass* Class(const char* space, const char* name);
	const MethodInfo* Method(const Il2CppClass* klass, const char* name, std::initializer_list<std::string_view> parameterTypes,
	                         std::string_view returnType, bool isStatic = false);
	size_t Field(Il2CppClass* klass, const char* name, std::string_view typeName);

	/// Return the errors, one on each line, or an empty text if all lookups found their names.
	[[nodiscard]] const std::string& Errors() const { return _errors; }

private:
	void AddError(const moa::Error& error);

	const Managed& _managed;
	std::string _errors;
};

} // namespace moa_mod

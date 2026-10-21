/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <string>
#include <vector>

#include <common/Result.h>

// The IL2CPP types are opaque. The mod reads them only through the il2cpp_* exports.
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppType;
struct Il2CppThread;
struct FieldInfo;
struct MethodInfo;

namespace moa_mod
{

// Each row: return type, name, parameter types. The table must match the exports of this Unity version.
#define MOA_IL2CPP_FUNCTIONS(X)                                                                     \
	X(Il2CppDomain*, il2cpp_domain_get, ())                                                         \
	X(const Il2CppAssembly**, il2cpp_domain_get_assemblies, (const Il2CppDomain*, size_t*))         \
	X(Il2CppThread*, il2cpp_thread_attach, (Il2CppDomain*))                                         \
	X(void, il2cpp_thread_detach, (Il2CppThread*))                                                  \
	X(const Il2CppImage*, il2cpp_get_corlib, ())                                                    \
	X(const Il2CppImage*, il2cpp_assembly_get_image, (const Il2CppAssembly*))                       \
	X(const char*, il2cpp_image_get_name, (const Il2CppImage*))                                     \
	X(size_t, il2cpp_image_get_class_count, (const Il2CppImage*))                                   \
	X(const Il2CppClass*, il2cpp_image_get_class, (const Il2CppImage*, size_t))                     \
	X(Il2CppClass*, il2cpp_class_from_name, (const Il2CppImage*, const char*, const char*))         \
	X(const char*, il2cpp_class_get_name, (const Il2CppClass*))                                     \
	X(const char*, il2cpp_class_get_namespace, (const Il2CppClass*))                                \
	X(Il2CppClass*, il2cpp_class_get_parent, (const Il2CppClass*))                                  \
	X(Il2CppClass*, il2cpp_class_get_declaring_type, (const Il2CppClass*))                          \
	X(int, il2cpp_class_get_flags, (const Il2CppClass*))                                            \
	X(bool, il2cpp_class_is_valuetype, (const Il2CppClass*))                                        \
	X(bool, il2cpp_class_is_enum, (const Il2CppClass*))                                             \
	X(bool, il2cpp_class_is_interface, (const Il2CppClass*))                                        \
	X(int32_t, il2cpp_class_instance_size, (const Il2CppClass*))                                    \
	X(Il2CppClass*, il2cpp_class_get_interfaces, (const Il2CppClass*, void**))                      \
	X(FieldInfo*, il2cpp_class_get_fields, (const Il2CppClass*, void**))                            \
	X(const MethodInfo*, il2cpp_class_get_methods, (const Il2CppClass*, void**))                    \
	X(const MethodInfo*, il2cpp_class_get_method_from_name, (const Il2CppClass*, const char*, int)) \
	X(const Il2CppType*, il2cpp_class_get_type, (const Il2CppClass*))                               \
	X(const char*, il2cpp_field_get_name, (FieldInfo*))                                             \
	X(int, il2cpp_field_get_flags, (FieldInfo*))                                                    \
	X(size_t, il2cpp_field_get_offset, (FieldInfo*))                                                \
	X(const Il2CppType*, il2cpp_field_get_type, (FieldInfo*))                                       \
	X(void, il2cpp_field_static_get_value, (FieldInfo*, void*))                                     \
	X(char*, il2cpp_type_get_name, (const Il2CppType*))                                             \
	X(Il2CppClass*, il2cpp_class_from_type, (const Il2CppType*))                                    \
	X(int, il2cpp_type_get_type, (const Il2CppType*))                                               \
	X(const char*, il2cpp_method_get_name, (const MethodInfo*))                                     \
	X(const Il2CppType*, il2cpp_method_get_return_type, (const MethodInfo*))                        \
	X(uint32_t, il2cpp_method_get_param_count, (const MethodInfo*))                                 \
	X(const Il2CppType*, il2cpp_method_get_param, (const MethodInfo*, uint32_t))                    \
	X(const char*, il2cpp_method_get_param_name, (const MethodInfo*, uint32_t))                     \
	X(uint32_t, il2cpp_method_get_flags, (const MethodInfo*, uint32_t*))                            \
	X(bool, il2cpp_field_has_attribute, (FieldInfo*, const Il2CppClass*))                           \
	X(void, il2cpp_free, (void*))

/// The il2cpp_* exports of GameAssembly.dll.
struct Il2CppApi
{
#define MOA_IL2CPP_MEMBER(ret, name, params) ret(*name) params = nullptr;
	MOA_IL2CPP_FUNCTIONS(MOA_IL2CPP_MEMBER)
#undef MOA_IL2CPP_MEMBER

	uintptr_t gameAssemblyBase = 0;

	/// Find every export in the loaded GameAssembly.dll. Return an error that names the missing exports.
	[[nodiscard]] static moa::Result<Il2CppApi> Load();

	/// Return the name of a type, as C# writes it, and free the memory that IL2CPP gives.
	[[nodiscard]] std::string TypeName(const Il2CppType* type) const;

	/// Return the RVA of the code of a method, or 0 for a method with no code (abstract or generic).
	[[nodiscard]] uint64_t MethodRva(const MethodInfo* method) const;

	/// Return "Namespace.Name", with the declaring types for nested classes ("Outer/Inner").
	[[nodiscard]] std::string FullName(const Il2CppClass* klass) const;
};

/// Attach the calling thread to IL2CPP for as long as the object lives. IL2CPP waits at shutdown for each thread
/// that stays attached, so attach only while the thread calls managed code.
class ThreadAttachment
{
public:
	explicit ThreadAttachment(const Il2CppApi& api)
	    : _api(api)
	    , _thread(api.il2cpp_thread_attach(api.il2cpp_domain_get()))
	{
	}
	~ThreadAttachment()
	{
		if (_thread != nullptr)
		{
			_api.il2cpp_thread_detach(_thread);
		}
	}
	ThreadAttachment(const ThreadAttachment&) = delete;
	ThreadAttachment& operator=(const ThreadAttachment&) = delete;

private:
	const Il2CppApi& _api;
	Il2CppThread* _thread;
};

/// One loaded assembly and its classes.
struct AssemblyClasses
{
	std::string name;
	std::vector<const Il2CppClass*> classes;
};

/// List every class of every loaded assembly.
[[nodiscard]] std::vector<AssemblyClasses> ListClasses(const Il2CppApi& api);

} // namespace moa_mod

/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <exception>
#include <format>
#include <utility>

#include <moa_mod/sdk/ModHost.h>

namespace moa_mod::sdk
{

namespace
{
void Give(MoaText* text, std::string_view value)
{
	if (text != nullptr && text->assign != nullptr)
	{
		text->assign(text->context, value.data(), value.size());
	}
}

[[nodiscard]] std::string Copy(const char* text)
{
	return text == nullptr ? std::string() : std::string(text);
}

[[nodiscard]] settings::FieldValue FromApi(MoaFieldKind kind, const MoaFieldValue& value)
{
	switch (kind)
	{
	case MOA_FIELD_TOGGLE:
		return value.toggle != 0;
	case MOA_FIELD_NUMBER:
		return value.number;
	case MOA_FIELD_CHOICE:
		return static_cast<size_t>(value.choice);
	}
	return false;
}

[[nodiscard]] MoaFieldValue ToApi(const settings::FieldValue& value)
{
	MoaFieldValue result {};
	if (const auto* toggle = std::get_if<bool>(&value))
	{
		result.toggle = *toggle ? 1u : 0u;
	}
	else if (const auto* number = std::get_if<double>(&value))
	{
		result.number = *number;
	}
	else
	{
		result.choice = static_cast<uint32_t>(std::get<size_t>(value));
	}
	return result;
}
} // namespace

/// One function that the mod gave, and its name for the log.
struct ModHost::Entry
{
	const ModHost* host = nullptr;
	std::string name;
	MoaFieldKind kind = MOA_FIELD_TOGGLE;
	std::function<void(const MoaCall&)> call;
	std::function<void()> task;
	std::function<moa::Result<void>(const nlohmann::json&)> load;
	FieldHandlers field;
	std::function<bool()> enabled;
	std::function<std::string()> read;
	std::function<nlohmann::json()> status;
	std::function<nlohmann::json(const nlohmann::json&)> query;

	/// Run `work` and write an exception to the log. Return false after an exception.
	template <typename Work>
	bool Guard(Work&& work) noexcept
	{
		try
		{
			work();
			return true;
		}
		catch (const std::exception& error)
		{
			host->Log(MOA_LOG_ERROR, std::format("{}: an exception: {}", name, error.what()));
		}
		catch (...)
		{
			host->Log(MOA_LOG_ERROR, std::format("{}: an unknown exception", name));
		}
		return false;
	}
};

ModHost::ModHost(const MoaHostApi& api, std::string modName)
    : _api(api)
    , _modName(std::move(modName))
{
}

ModHost::~ModHost() = default;

moa::Result<void> ModHost::Check(const MoaHostApi* api, uint32_t apiVersion)
{
	if (api == nullptr)
	{
		return moa::MakeError("the loader gave no table");
	}
	if (apiVersion != MOA_MOD_API_VERSION || api->apiVersion != MOA_MOD_API_VERSION)
	{
		return moa::MakeError("the loader uses the interface version {}; this mod uses {}", api->apiVersion,
		                      MOA_MOD_API_VERSION);
	}
	if (api->size < MOA_HOST_API_FIRST_SIZE)
	{
		return moa::MakeError("the table of the loader has {} bytes; this mod needs {}", api->size, MOA_HOST_API_FIRST_SIZE);
	}
	return {};
}

std::string ModHost::BuildGuid() const
{
	return Copy(_api.buildGuid);
}

std::string ModHost::LogFolder() const
{
	return Copy(_api.logFolder);
}

std::string ModHost::GameFolder() const
{
	return Copy(_api.gameFolder);
}

std::string ModHost::SettingsFolder() const
{
	return Copy(_api.settingsFolder);
}

void ModHost::Log(MoaLogLevel level, std::string_view text) const
{
	if (_api.log != nullptr)
	{
		const auto line = std::format("{}: {}", _modName, text);
		_api.log(_api.context, level, line.c_str());
	}
}

ModHost::Entry& ModHost::Keep(std::unique_ptr<Entry> entry)
{
	entry->host = this;
	_entries.push_back(std::move(entry));
	return *_entries.back();
}

bool ModHost::AddMethodCallback(const MethodRef& method, MoaHookPhase phase, std::string_view name,
                                std::function<void(const MoaCall&)> callback)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("{}.{} {}", method.className, method.method, name);
	entry.call = std::move(callback);
	std::vector<const char*> parameters;
	for (const auto& type : method.parameterTypes)
	{
		parameters.push_back(type.c_str());
	}
	const MoaMethodDesc desc {.space = method.space.c_str(),
	                          .className = method.className.c_str(),
	                          .method = method.method.c_str(),
	                          .parameterTypes = parameters.data(),
	                          .parameterCount = static_cast<uint32_t>(parameters.size())};
	const auto trampoline = [](void* user, const MoaCall* call) {
		auto& e = *static_cast<Entry*>(user);
		if (call != nullptr)
		{
			(void)e.Guard([&] { e.call(*call); });
		}
	};
	const auto name0 = std::string(name);
	return _api.addMethodCallback(_api.context, &desc, phase, name0.c_str(), trampoline, &entry) != 0;
}

bool ModHost::PostTask(std::function<void()> task)
{
	// The task deletes itself after it runs. A task that the loader never runs stays in memory until the process ends.
	auto entry = std::make_unique<Entry>();
	entry->host = this;
	entry->name = "task";
	entry->task = std::move(task);
	const auto trampoline = [](void* user) {
		std::unique_ptr<Entry> e(static_cast<Entry*>(user));
		(void)e->Guard([&] { e->task(); });
	};
	if (_api.postTask(_api.context, trampoline, entry.get()) == 0)
	{
		return false;
	}
	(void)entry.release();
	return true;
}

bool ModHost::AddSettings(std::string_view section, std::function<moa::Result<void>(const nlohmann::json&)> load)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("settings '{}'", section);
	entry.load = std::move(load);
	const auto trampoline = [](void* user, const char* json, MoaText* error) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		uint32_t accepted = 0;
		const bool ran = e.Guard([&] {
			const auto object = nlohmann::json::parse(json == nullptr ? "{}" : json, nullptr, false);
			if (object.is_discarded() || !object.is_object())
			{
				Give(error, std::format("the object '{}' is not a JSON object", e.name));
				return;
			}
			if (auto loaded = e.load(object); !loaded)
			{
				Give(error, loaded.GetError().message);
				return;
			}
			accepted = 1;
		});
		if (!ran)
		{
			Give(error, "an exception in the mod");
		}
		return accepted;
	};
	const auto section0 = std::string(section);
	return _api.addSettings(_api.context, section0.c_str(), trampoline, &entry) != 0;
}

bool ModHost::SaveSettings(const nlohmann::json& patch)
{
	const auto text = patch.dump();
	return _api.saveSettings(_api.context, text.c_str()) != 0;
}

bool ModHost::AddField(const MoaFieldDesc& field, FieldHandlers handlers)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("field {}.{}", Copy(field.section), Copy(field.key));
	entry.kind = field.kind;
	entry.field = std::move(handlers);
	const auto get = [](void* user, MoaFieldState* state) {
		auto& e = *static_cast<Entry*>(user);
		if (state == nullptr)
		{
			return;
		}
		(void)e.Guard([&] {
			const auto current = e.field.get();
			*state = MoaFieldState {.value = ToApi(current.value),
			                        .min = current.range.min,
			                        .max = current.range.max,
			                        .restartPending = current.restartPending ? 1u : 0u};
		});
	};
	const auto set = [](void* user, const MoaFieldValue* value, uint32_t save, MoaText* message) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		uint32_t accepted = 0;
		if (value == nullptr)
		{
			return 0;
		}
		const bool ran = e.Guard([&] {
			auto result = e.field.set(FromApi(e.kind, *value), save != 0);
			if (!result)
			{
				Give(message, result.GetError().message);
				return;
			}
			Give(message, *result);
			accepted = 1;
		});
		if (!ran)
		{
			Give(message, "an exception in the mod");
		}
		return accepted;
	};
	return _api.addField(_api.context, &field, get, set, &entry) != 0;
}

bool ModHost::AddAction(const MoaActionDesc& action, std::function<void()> run, std::function<bool()> enabled)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("action {}", Copy(action.hotkey != nullptr ? action.hotkey : action.label));
	entry.task = std::move(run);
	entry.enabled = std::move(enabled);
	const auto trampoline = [](void* user) {
		auto& e = *static_cast<Entry*>(user);
		(void)e.Guard([&] { e.task(); });
	};
	const auto isEnabled = [](void* user) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		bool result = true;
		if (e.enabled)
		{
			result = false;
			(void)e.Guard([&] { result = e.enabled(); });
		}
		return result ? 1u : 0u;
	};
	return _api.addAction(_api.context, &action, trampoline, isEnabled, &entry) != 0;
}

bool ModHost::AddReadout(const char* menu, const char* group, std::function<std::string()> read)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("readout {}", Copy(group));
	entry.read = std::move(read);
	const auto trampoline = [](void* user, MoaText* text) {
		auto& e = *static_cast<Entry*>(user);
		(void)e.Guard([&] { Give(text, e.read()); });
	};
	return _api.addReadout(_api.context, menu, group, trampoline, &entry) != 0;
}

bool ModHost::SetStatusReader(std::function<nlohmann::json()> read)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = "status";
	entry.status = std::move(read);
	const auto trampoline = [](void* user, MoaText* text) {
		auto& e = *static_cast<Entry*>(user);
		(void)e.Guard([&] { Give(text, e.status().dump()); });
	};
	return _api.setStatusReader(_api.context, trampoline, &entry) != 0;
}

void ModHost::ShowMessage(std::string_view text, double seconds) const
{
	const auto copy = std::string(text);
	_api.showMessage(_api.context, copy.c_str(), seconds);
}

bool ModHost::HasQueries() const
{
	return MOA_HOST_API_HAS(&_api, addQuery) != 0 && MOA_HOST_API_HAS(&_api, listQueries) != 0 &&
	       MOA_HOST_API_HAS(&_api, callQuery) != 0 && _api.addQuery != nullptr && _api.listQueries != nullptr &&
	       _api.callQuery != nullptr;
}

bool ModHost::AddQuery(std::string_view name, std::string_view help,
                       std::function<nlohmann::json(const nlohmann::json& params)> query)
{
	if (!HasQueries())
	{
		Log(MOA_LOG_WARN, std::format("the loader has no queries; the query '{}' is not available", name));
		return false;
	}
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("query {}", name);
	entry.query = std::move(query);
	const auto trampoline = [](void* user, const char* params, MoaText* result) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		// An exception is the normal error of a query, so the error goes to the inspector and not to the log.
		try
		{
			auto object = params == nullptr ? nlohmann::json::object() : nlohmann::json::parse(params, nullptr, false);
			if (object.is_null())
			{
				object = nlohmann::json::object();
			}
			if (object.is_discarded() || !object.is_object())
			{
				Give(result, "the parameters are not a JSON object");
				return 0;
			}
			const auto answer = e.query(object);
			Give(result, answer.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
			return 1;
		}
		catch (const std::exception& error)
		{
			Give(result, error.what());
		}
		catch (...)
		{
			Give(result, "an unknown exception in the query");
		}
		return 0;
	};
	const auto name0 = std::string(name);
	const auto help0 = std::string(help);
	return _api.addQuery(_api.context, name0.c_str(), help0.c_str(), trampoline, &entry) != 0;
}

moa::Result<nlohmann::json> ModHost::ListQueries() const
{
	if (!HasQueries())
	{
		return moa::MakeError("the loader has no queries");
	}
	std::string text;
	MoaText target {.context = &text, .assign = [](void* context, const char* utf8, size_t size) {
		                static_cast<std::string*>(context)->assign(utf8, size);
	                }};
	_api.listQueries(_api.context, &target);
	auto list = nlohmann::json::parse(text, nullptr, false);
	if (list.is_discarded() || !list.is_array())
	{
		return moa::MakeError("the list of the queries is not a JSON array");
	}
	return list;
}

moa::Result<nlohmann::json> ModHost::CallQuery(std::string_view name, const nlohmann::json& params) const
{
	if (!HasQueries())
	{
		return moa::MakeError("the loader has no queries");
	}
	std::string text;
	MoaText target {.context = &text, .assign = [](void* context, const char* utf8, size_t size) {
		                static_cast<std::string*>(context)->assign(utf8, size);
	                }};
	const auto name0 = std::string(name);
	const auto params0 = params.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	if (_api.callQuery(_api.context, name0.c_str(), params0.c_str(), &target) == 0)
	{
		return moa::MakeError("{}", text.empty() ? std::string("the query failed") : text);
	}
	auto answer = nlohmann::json::parse(text, nullptr, false);
	if (answer.is_discarded())
	{
		return moa::MakeError("the answer of the query '{}' is not JSON", name);
	}
	return answer;
}

} // namespace moa_mod::sdk

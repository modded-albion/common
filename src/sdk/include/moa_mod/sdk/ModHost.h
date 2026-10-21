/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <moa/common/Result.h>
#include <moa_mod/SettingsFields.h>
#include <moa_mod/api/ModApi.h>
#include <nlohmann/json.hpp>

namespace moa_mod::sdk
{

/// A managed method for `ModHost::AddMethodCallback`.
struct MethodRef
{
	std::string space;
	std::string className;
	std::string method;
	std::vector<std::string> parameterTypes;
};

/// The state of a menu field for the loader.
struct FieldState
{
	settings::FieldValue value {false};
	settings::FieldRange range {};
	bool restartPending = false;
};

/// What a menu field does: `get` gives its state; `set` changes it and gives a message for the log, or an error.
struct FieldHandlers
{
	std::function<FieldState()> get;
	std::function<moa::Result<std::string>(const settings::FieldValue& value, bool save)> set;
};

/// The C++ side of the table of the loader (`MoaHostApi`) in a mod DLL. It keeps each C++ function that the mod gives,
/// and gives the loader a plain function and a pointer to the kept function. No exception goes to the loader: a
/// function that throws writes the error to the log.
///
/// The mod makes one `ModHost` in `MoaModInit` and keeps it until `MoaModShutdown` returns.
class ModHost
{
public:
	explicit ModHost(const MoaHostApi& api, std::string modName);
	~ModHost();
	ModHost(const ModHost&) = delete;
	ModHost& operator=(const ModHost&) = delete;

	/// Return an error if the loader uses another interface version, or if its table is smaller than the table of the
	/// first loader of this version. A newer function of the table can be missing: see `HasQueries`.
	[[nodiscard]] static moa::Result<void> Check(const MoaHostApi* api, uint32_t apiVersion);

	[[nodiscard]] const MoaHostApi& Api() const { return _api; }
	[[nodiscard]] std::string BuildGuid() const;
	[[nodiscard]] std::string LogFolder() const;
	[[nodiscard]] std::string GameFolder() const;
	[[nodiscard]] std::string SettingsFolder() const;

	void Log(MoaLogLevel level, std::string_view text) const;

	[[nodiscard]] bool AddMethodCallback(const MethodRef& method, MoaHookPhase phase, std::string_view name,
	                                     std::function<void(const MoaCall&)> callback);
	/// Run `task` one time on the main thread, between two frames.
	bool PostTask(std::function<void()> task);
	/// Read the JSON object `section` of mods.json now and after each change of the file.
	[[nodiscard]] bool AddSettings(std::string_view section, std::function<moa::Result<void>(const nlohmann::json&)> load);
	/// Write a change of mods.json (a JSON merge patch).
	bool SaveSettings(const nlohmann::json& patch);
	[[nodiscard]] bool AddField(const MoaFieldDesc& field, FieldHandlers handlers);
	[[nodiscard]] bool AddAction(const MoaActionDesc& action, std::function<void()> run, std::function<bool()> enabled = {});
	[[nodiscard]] bool AddReadout(const char* menu, const char* group, std::function<std::string()> read);
	[[nodiscard]] bool SetStatusReader(std::function<nlohmann::json()> read);
	void ShowMessage(std::string_view text, double seconds) const;

	/// The table of the loader has the functions of the inspector queries.
	[[nodiscard]] bool HasQueries() const;
	/// Add a query that the inspector can call. `query` gets the JSON object of the parameters and runs on the main
	/// thread. An exception in `query` gives its message as the error of the query. Return false if the loader has no
	/// queries or refuses the query.
	[[nodiscard]] bool AddQuery(std::string_view name, std::string_view help,
	                            std::function<nlohmann::json(const nlohmann::json& params)> query);
	/// The queries of the loader and of all mods: a JSON array of objects with "name", "help" and "mod". Any thread.
	[[nodiscard]] moa::Result<nlohmann::json> ListQueries() const;
	/// Call a query of the loader or of a mod. Main thread only.
	[[nodiscard]] moa::Result<nlohmann::json> CallQuery(std::string_view name, const nlohmann::json& params) const;

private:
	struct Entry;

	Entry& Keep(std::unique_ptr<Entry> entry);

	MoaHostApi _api;
	std::string _modName;
	std::deque<std::unique_ptr<Entry>> _entries;
};

/// Register the fields of a table with the loader. `settings` gives the settings that the user asked for, `live`
/// gives the settings that the mod uses now, `range` gives the range of a number field for the current settings, and
/// `edit` changes one field (with `EditField`) and uses the new settings.
template <typename S>
struct FieldBinding
{
	std::function<const S&()> requested;
	std::function<const S&()> live;
	std::function<settings::FieldRange(const settings::Field<S>&, const S&)> range;
	std::function<moa::Result<std::string>(const settings::Field<S>&, const settings::FieldValue&, bool save)> edit;
};

/// Give the loader the description of a field, with texts that live as long as the table.
struct FieldTexts
{
	std::string menu;
	std::string group;
	std::string section;
	std::string key;
	std::string label;
	std::string help;
	std::string format;
	std::vector<std::string> choices;
	std::vector<const char*> choicePointers;
};

template <typename S>
[[nodiscard]] FieldTexts MakeFieldTexts(const settings::Field<S>& field, std::string_view menu)
{
	FieldTexts texts {.menu = std::string(menu),
	                  .group = std::string(field.group),
	                  .section = std::string(field.section),
	                  .key = std::string(field.key),
	                  .label = std::string(field.label),
	                  .help = std::string(field.help),
	                  .format = std::string(field.format)};
	for (const auto choice : field.choices)
	{
		texts.choices.emplace_back(choice);
	}
	for (const auto& choice : texts.choices)
	{
		texts.choicePointers.push_back(choice.c_str());
	}
	return texts;
}

[[nodiscard]] inline MoaFieldKind ToApiKind(settings::FieldKind kind)
{
	switch (kind)
	{
	case settings::FieldKind::Toggle:
		return MOA_FIELD_TOGGLE;
	case settings::FieldKind::Number:
		return MOA_FIELD_NUMBER;
	case settings::FieldKind::Choice:
		return MOA_FIELD_CHOICE;
	}
	return MOA_FIELD_TOGGLE;
}

/// Register each field of `fields` in the menu `menu` (null for the menu of the mod). Return the count of the fields
/// that the loader refused.
template <typename S>
size_t BindFields(ModHost& host, std::span<const settings::Field<S>> fields, const char* menu, FieldBinding<S> binding)
{
	size_t refused = 0;
	auto shared = std::make_shared<FieldBinding<S>>(std::move(binding));
	for (const auto& field : fields)
	{
		const auto texts = MakeFieldTexts(field, menu == nullptr ? "" : menu);
		const MoaFieldDesc desc {.menu = menu == nullptr ? nullptr : texts.menu.c_str(),
		                         .group = texts.group.c_str(),
		                         .section = texts.section.c_str(),
		                         .key = texts.key.c_str(),
		                         .label = texts.label.c_str(),
		                         .help = texts.help.c_str(),
		                         .kind = ToApiKind(field.kind),
		                         .min = field.min,
		                         .max = field.max,
		                         .step = field.step,
		                         .format = texts.format.c_str(),
		                         .logarithmic = field.logarithmic ? 1u : 0u,
		                         .restartOnly = field.restartOnly ? 1u : 0u,
		                         .choices = texts.choicePointers.data(),
		                         .choiceCount = static_cast<uint32_t>(texts.choicePointers.size())};
		const auto* pointer = &field;
		FieldHandlers handlers {
		    .get =
		        [shared, pointer] {
			        const auto& requested = shared->requested();
			        return FieldState {.value = settings::ValueOf(*pointer, requested),
			                           .range = shared->range(*pointer, requested),
			                           .restartPending =
			                               pointer->restartOnly && settings::ValueOf(*pointer, requested) !=
			                                                           settings::ValueOf(*pointer, shared->live())};
		        },
		    .set = [shared, pointer](const settings::FieldValue& value,
		                             bool save) { return shared->edit(*pointer, value, save); },
		};
		if (!host.AddField(desc, std::move(handlers)))
		{
			++refused;
		}
	}
	return refused;
}

} // namespace moa_mod::sdk

/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <mod_sdk/ModHost.h>

using namespace moa_mod;

namespace
{

/// A loader for the tests: it keeps each registration and lets the test call it.
struct FakeLoader
{
	struct Callback
	{
		std::string name;
		MoaCallFn call;
		void* user;
	};
	struct Settings
	{
		std::string section;
		MoaSettingsFn load;
		void* user;
	};
	struct FieldEntry
	{
		std::string key;
		MoaFieldKind kind;
		MoaFieldGetFn get;
		MoaFieldSetFn set;
		void* user;
	};

	std::vector<std::string> log;
	std::vector<Callback> callbacks;
	std::vector<std::pair<MoaTaskFn, void*>> tasks;
	std::vector<Settings> settings;
	std::vector<std::string> saved;
	std::vector<FieldEntry> fields;
	struct Query
	{
		std::string name;
		MoaQueryFn query;
		void* user;
	};
	std::vector<Query> queries;
	bool refuse = false;

	[[nodiscard]] MoaHostApi Api()
	{
		MoaHostApi api {};
		api.apiVersion = MOA_MOD_API_VERSION;
		api.size = sizeof(MoaHostApi);
		api.context = this;
		api.buildGuid = "build";
		api.log = [](void* c, MoaLogLevel /*level*/, const char* text) { static_cast<FakeLoader*>(c)->log.emplace_back(text); };
		api.addMethodCallback = [](void* c, const MoaMethodDesc* method, MoaHookPhase /*phase*/, const char* name,
		                           MoaCallFn call, void* user) -> uint32_t {
			auto& self = *static_cast<FakeLoader*>(c);
			if (self.refuse)
			{
				return 0;
			}
			self.callbacks.push_back({std::string(method->className) + "." + method->method + "/" + name, call, user});
			return 1;
		};
		api.postTask = [](void* c, MoaTaskFn task, void* user) -> uint32_t {
			static_cast<FakeLoader*>(c)->tasks.emplace_back(task, user);
			return 1;
		};
		api.addSettings = [](void* c, const char* section, MoaSettingsFn load, void* user) -> uint32_t {
			static_cast<FakeLoader*>(c)->settings.push_back({section, load, user});
			return 1;
		};
		api.saveSettings = [](void* c, const char* patch) -> uint32_t {
			static_cast<FakeLoader*>(c)->saved.emplace_back(patch);
			return 1;
		};
		api.addField = [](void* c, const MoaFieldDesc* field, MoaFieldGetFn get, MoaFieldSetFn set, void* user) -> uint32_t {
			static_cast<FakeLoader*>(c)->fields.push_back({field->key, field->kind, get, set, user});
			return 1;
		};
		api.addQuery = [](void* c, const char* name, const char* /*help*/, MoaQueryFn query, void* user) -> uint32_t {
			static_cast<FakeLoader*>(c)->queries.push_back({name, query, user});
			return 1;
		};
		api.listQueries = [](void* c, MoaText* list) {
			auto names = nlohmann::json::array();
			for (const auto& query : static_cast<FakeLoader*>(c)->queries)
			{
				names.push_back({{"name", query.name}});
			}
			const auto text = names.dump();
			list->assign(list->context, text.data(), text.size());
		};
		api.callQuery = [](void* c, const char* name, const char* params, MoaText* result) -> uint32_t {
			for (const auto& query : static_cast<FakeLoader*>(c)->queries)
			{
				if (query.name == name)
				{
					return query.query(query.user, params, result);
				}
			}
			const std::string error = "the query is not known";
			result->assign(result->context, error.data(), error.size());
			return 0;
		};
		return api;
	}
};

void AssignTo(void* context, const char* text, size_t size)
{
	static_cast<std::string*>(context)->assign(text, size);
}

struct Sample
{
	bool on = false;
	double speed = 1.0;
};

const std::array<settings::Field<Sample>, 2> k_SampleFields {
    settings::Field<Sample> {.group = "Main",
                             .section = "sample",
                             .key = "on",
                             .label = "On",
                             .getToggle = [](const Sample& s) { return s.on; },
                             .setToggle = [](Sample& s, bool v) { s.on = v; }},
    settings::Field<Sample> {.group = "Main",
                             .section = "sample",
                             .key = "speed",
                             .label = "Speed",
                             .kind = settings::FieldKind::Number,
                             .min = 0.0,
                             .max = 4.0,
                             .restartOnly = true,
                             .getNumber = [](const Sample& s) { return s.speed; },
                             .setNumber = [](Sample& s, double v) { s.speed = v; }},
};

} // namespace

TEST(ModHost, RefusesATableOfAnotherVersion)
{
	FakeLoader loader;
	auto api = loader.Api();
	EXPECT_TRUE(sdk::ModHost::Check(&api, MOA_MOD_API_VERSION));
	EXPECT_FALSE(sdk::ModHost::Check(nullptr, MOA_MOD_API_VERSION));
	EXPECT_FALSE(sdk::ModHost::Check(&api, MOA_MOD_API_VERSION + 1));
	api.size = 8;
	EXPECT_FALSE(sdk::ModHost::Check(&api, MOA_MOD_API_VERSION));
}

TEST(ModHost, AcceptsTheTableOfTheFirstLoaderWithNoQueries)
{
	FakeLoader loader;
	auto api = loader.Api();
	api.size = static_cast<uint32_t>(MOA_HOST_API_FIRST_SIZE);
	EXPECT_TRUE(sdk::ModHost::Check(&api, MOA_MOD_API_VERSION));
	sdk::ModHost host(api, "test");
	EXPECT_FALSE(host.HasQueries());
	EXPECT_FALSE(host.AddQuery("test.state", "", [](const nlohmann::json& /*params*/) { return nlohmann::json(); }));
	EXPECT_TRUE(loader.queries.empty());
	EXPECT_FALSE(host.ListQueries());
	EXPECT_FALSE(host.CallQuery("test.state", nlohmann::json::object()));
}

TEST(ModHost, GivesAQueryItsParametersAndItsAnswerOrError)
{
	FakeLoader loader;
	sdk::ModHost host(loader.Api(), "test");
	ASSERT_TRUE(host.HasQueries());
	ASSERT_TRUE(host.AddQuery("test.double", "Doubles a value.", [](const nlohmann::json& params) {
		if (!params.contains("value"))
		{
			throw std::runtime_error("no value");
		}
		return nlohmann::json {{"double", params.at("value").get<int>() * 2}};
	}));
	ASSERT_EQ(loader.queries.size(), 1u);
	auto& query = loader.queries[0];
	std::string answer;
	MoaText text {.context = &answer, .assign = AssignTo};
	EXPECT_EQ(query.query(query.user, R"({"value": 4})", &text), 1u);
	EXPECT_EQ(answer, R"({"double":8})");
	EXPECT_EQ(query.query(query.user, "{}", &text), 0u);
	EXPECT_EQ(answer, "no value");
	EXPECT_EQ(query.query(query.user, "[1]", &text), 0u);
	EXPECT_EQ(query.query(query.user, nullptr, &text), 0u);
	EXPECT_EQ(answer, "no value");
	// An exception of a query is its error, not an error of the mod.
	EXPECT_TRUE(loader.log.empty());

	const auto list = host.ListQueries();
	ASSERT_TRUE(list);
	EXPECT_EQ(list->at(0).at("name"), "test.double");
	const auto called = host.CallQuery("test.double", {{"value", 5}});
	ASSERT_TRUE(called);
	EXPECT_EQ(called->at("double"), 10);
	const auto failed = host.CallQuery("test.other", nlohmann::json::object());
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.GetError().message, "the query is not known");
}

TEST(ModHost, GivesTheCallOfAMethodToTheFunctionAndCatchesAnException)
{
	FakeLoader loader;
	sdk::ModHost host(loader.Api(), "test");
	void* seen = nullptr;
	const sdk::MethodRef update {.space = "", .className = "GameManager", .method = "Update", .parameterTypes = {}};
	ASSERT_TRUE(host.AddMethodCallback(update, MOA_HOOK_AFTER, "a", [&](const MoaCall& call) { seen = call.instance; }));
	ASSERT_TRUE(host.AddMethodCallback(update, MOA_HOOK_AFTER, "b",
	                                   [](const MoaCall& /*call*/) { throw std::runtime_error("bad frame"); }));
	ASSERT_EQ(loader.callbacks.size(), 2u);
	EXPECT_EQ(loader.callbacks[0].name, "GameManager.Update/a");
	int object = 0;
	const MoaCall call {.instance = &object, .arguments = {}};
	for (const auto& callback : loader.callbacks)
	{
		callback.call(callback.user, &call);
	}
	EXPECT_EQ(seen, &object);
	ASSERT_EQ(loader.log.size(), 1u);
	EXPECT_NE(loader.log[0].find("bad frame"), std::string::npos);
	loader.refuse = true;
	EXPECT_FALSE(host.AddMethodCallback(update, MOA_HOOK_BEFORE, "c", [](const MoaCall& /*call*/) {}));
}

TEST(ModHost, RunsATaskOneTime)
{
	FakeLoader loader;
	sdk::ModHost host(loader.Api(), "test");
	int runs = 0;
	ASSERT_TRUE(host.PostTask([&] { ++runs; }));
	ASSERT_EQ(loader.tasks.size(), 1u);
	loader.tasks[0].first(loader.tasks[0].second);
	EXPECT_EQ(runs, 1);
}

TEST(ModHost, GivesTheSettingsAndReturnsTheError)
{
	FakeLoader loader;
	sdk::ModHost host(loader.Api(), "test");
	double value = 0.0;
	ASSERT_TRUE(host.AddSettings("sample", [&](const nlohmann::json& object) -> moa::Result<void> {
		if (object.contains("bad"))
		{
			return moa::MakeError("bad value");
		}
		value = object.value("speed", 1.0);
		return {};
	}));
	ASSERT_EQ(loader.settings.size(), 1u);
	auto& entry = loader.settings[0];
	EXPECT_EQ(entry.section, "sample");
	std::string error;
	MoaText text {.context = &error, .assign = AssignTo};
	EXPECT_EQ(entry.load(entry.user, R"({"speed": 3})", &text), 1u);
	EXPECT_DOUBLE_EQ(value, 3.0);
	EXPECT_EQ(entry.load(entry.user, R"({"bad": 1})", &text), 0u);
	EXPECT_EQ(error, "bad value");
	EXPECT_EQ(entry.load(entry.user, "[1]", &text), 0u);
	EXPECT_TRUE(host.SaveSettings({{"sample", {{"speed", 2}}}}));
	EXPECT_EQ(loader.saved.back(), R"({"sample":{"speed":2}})");
}

TEST(ModHost, BindsTheFieldsOfATable)
{
	FakeLoader loader;
	sdk::ModHost host(loader.Api(), "test");
	Sample requested;
	const Sample live;
	const auto refused = sdk::BindFields<Sample>(
	    host, k_SampleFields, nullptr,
	    {.requested = [&]() -> const Sample& { return requested; },
	     .live = [&]() -> const Sample& { return live; },
	     .range = [](const settings::Field<Sample>& f,
	                 const Sample& /*settings*/) { return settings::FieldRange {.min = f.min, .max = f.max}; },
	     .edit = [&](const settings::Field<Sample>& f, const settings::FieldValue& value,
	                 bool /*save*/) -> moa::Result<std::string> {
		     auto edit = settings::EditField(requested, f, value, settings::FieldRange {.min = f.min, .max = f.max},
		                                     [](const Sample& /*next*/) -> moa::Result<void> { return {}; });
		     if (!edit)
		     {
			     return edit.GetError();
		     }
		     return edit->message;
	     }});
	EXPECT_EQ(refused, 0u);
	ASSERT_EQ(loader.fields.size(), 2u);
	auto& speed = loader.fields[1];
	EXPECT_EQ(speed.kind, MOA_FIELD_NUMBER);
	std::string message;
	MoaText text {.context = &message, .assign = AssignTo};
	MoaFieldValue value {};
	value.number = 9.0;
	EXPECT_EQ(speed.set(speed.user, &value, 1, &text), 1u);
	EXPECT_EQ(message, "Speed: 4.00");
	MoaFieldState state {};
	speed.get(speed.user, &state);
	EXPECT_DOUBLE_EQ(state.value.number, 4.0);
	EXPECT_DOUBLE_EQ(state.max, 4.0);
	EXPECT_EQ(state.restartPending, 1u);
}

/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/sdk/ModHost.h>

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

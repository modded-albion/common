/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>
#include <mod_core/FrameCallbacks.h>
#include <mod_core/MainThreadTasks.h>
#include <mod_core/SlotSwap.h>
#include <mod_core/UnityMath.h>
#include <nlohmann/json.hpp>

using namespace moa_mod;

namespace
{

void ExpectNear(WorldPoint actual, WorldPoint expected, float tolerance = 1e-4f)
{
	EXPECT_NEAR(actual.x, expected.x, tolerance);
	EXPECT_NEAR(actual.y, expected.y, tolerance);
	EXPECT_NEAR(actual.z, expected.z, tolerance);
}

/// A stand-in for the IL2CPP method record: the code pointer is the first field.
struct FakeMethod
{
	void* code;
	const char* name;
};

int s_calls = 0;

void OriginalCode()
{
	s_calls += 1;
}

void ReplacementCode()
{
	s_calls += 100;
}

void* Address(void (*function)())
{
	return reinterpret_cast<void*>(function);
}

void CallThrough(const FakeMethod& method)
{
	reinterpret_cast<void (*)()>(method.code)();
}

} // namespace

TEST(SlotSwap, PutsTheReplacementInAndTheOriginalBack)
{
	FakeMethod method {.code = Address(&OriginalCode), .name = "Update"};
	s_calls = 0;
	{
		auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
		ASSERT_TRUE(swap.Installed());
		EXPECT_EQ(swap.Original(), Address(&OriginalCode));
		CallThrough(method);
		EXPECT_EQ(s_calls, 100);
	}
	EXPECT_EQ(method.code, Address(&OriginalCode));
	CallThrough(method);
	EXPECT_EQ(s_calls, 101);
}

TEST(SlotSwap, DoesNothingIfTheSlotHoldsADifferentPointer)
{
	FakeMethod method {.code = Address(&ReplacementCode), .name = "Update"};
	const auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
	EXPECT_FALSE(swap.Installed());
	EXPECT_EQ(method.code, Address(&ReplacementCode));
	EXPECT_FALSE(SlotSwap::Install(nullptr, Address(&OriginalCode), Address(&ReplacementCode)).Installed());
}

TEST(SlotSwap, KeepsAPointerThatAnotherProgramPutInAfterTheInstall)
{
	FakeMethod method {.code = Address(&OriginalCode), .name = "Update"};
	auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
	ASSERT_TRUE(swap.Installed());
	int other = 0;
	method.code = &other; // Another mod changed the slot.
	EXPECT_FALSE(swap.Restore());
	EXPECT_EQ(method.code, &other);
	EXPECT_FALSE(swap.Installed());
	EXPECT_TRUE(swap.Restore()); // A second restore does nothing.
}

TEST(SlotSwap, MovesTheOwnership)
{
	FakeMethod method {.code = Address(&OriginalCode), .name = "Update"};
	SlotSwap kept;
	{
		auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
		kept = std::move(swap);
	}
	EXPECT_EQ(method.code, Address(&ReplacementCode));
	kept.Restore();
	EXPECT_EQ(method.code, Address(&OriginalCode));
}

TEST(FrameCallbacks, CallsEachFunctionOneTimeInEachFrame)
{
	FrameCallbacks callbacks;
	int first = 0;
	int second = 0;
	EXPECT_TRUE(callbacks.Add("first", [&] { ++first; }));
	EXPECT_TRUE(callbacks.Add("second", [&] { ++second; }));
	EXPECT_FALSE(callbacks.Add("first", [] {}));
	EXPECT_FALSE(callbacks.Add("empty", nullptr));
	EXPECT_TRUE(callbacks.Run());
	EXPECT_TRUE(callbacks.Run());
	EXPECT_EQ(first, 2);
	EXPECT_EQ(second, 2);
	EXPECT_EQ(callbacks.Frames(), 2u);

	EXPECT_TRUE(callbacks.SetEnabled("second", false));
	EXPECT_FALSE(callbacks.SetEnabled("missing", false));
	callbacks.Run();
	EXPECT_EQ(first, 3);
	EXPECT_EQ(second, 2);

	callbacks.Clear();
	callbacks.Run();
	EXPECT_EQ(first, 3);
	EXPECT_TRUE(callbacks.Statuses().empty());
}

TEST(FrameCallbacks, StopsAFunctionThatThrows)
{
	FrameCallbacks callbacks;
	int good = 0;
	int bad = 0;
	callbacks.Add("bad", [&] {
		++bad;
		throw std::runtime_error("the moon is missing");
	});
	callbacks.Add("good", [&] { ++good; });
	callbacks.Run();
	callbacks.Run();
	EXPECT_EQ(bad, 1);
	EXPECT_EQ(good, 2);
	const auto statuses = callbacks.Statuses();
	ASSERT_EQ(statuses.size(), 2u);
	EXPECT_FALSE(statuses[0].enabled);
	EXPECT_EQ(statuses[0].error, "the moon is missing");
	EXPECT_EQ(statuses[0].calls, 1u);

	callbacks.SetEnabled("bad", true);
	EXPECT_TRUE(callbacks.Statuses()[0].error.empty());
}

TEST(FrameCallbacks, RunsOnlyOnTheFirstThread)
{
	FrameCallbacks callbacks;
	int calls = 0;
	callbacks.Add("count", [&] { ++calls; });
	EXPECT_TRUE(callbacks.Run());
	bool otherResult = true;
	std::thread([&] { otherResult = callbacks.Run(); }).join();
	EXPECT_FALSE(otherResult);
	EXPECT_EQ(calls, 1);
}

// The menu bar publishes the state of the mods, which includes the statuses of the hooks, from a function of the
// list and from a task that the list runs. That must not lock the list a second time on the same thread.
TEST(FrameCallbacks, AFunctionCanReadTheStatusesOfItsOwnList)
{
	FrameCallbacks callbacks;
	MainThreadTasks tasks;
	size_t seen = 0;
	callbacks.Add("overlay", [&] { seen = callbacks.Statuses().size() + callbacks.Frames(); });
	callbacks.Add("tasks", [&] { tasks.RunPending(); });
	auto command = tasks.Post(MainThreadTasks::Task([&] { return nlohmann::json(callbacks.Statuses().size()); }));
	EXPECT_TRUE(callbacks.Run());
	ASSERT_EQ(command.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_EQ(command.get(), 2);
	EXPECT_EQ(seen, 3u);
	EXPECT_TRUE(callbacks.Run());
	for (const auto& status : callbacks.Statuses())
	{
		EXPECT_TRUE(status.enabled) << status.name;
		EXPECT_TRUE(status.error.empty()) << status.name << ": " << status.error;
		EXPECT_EQ(status.calls, 2u) << status.name;
	}
}

TEST(FrameCallbacks, AFunctionCanChangeTheListWhileItRuns)
{
	FrameCallbacks callbacks;
	int late = 0;
	int stopped = 0;
	bool nested = true;
	callbacks.Add("first", [&] {
		nested = callbacks.Run();
		callbacks.SetEnabled("stopped", false);
		callbacks.Add("late", [&] { ++late; });
	});
	callbacks.Add("stopped", [&] { ++stopped; });
	EXPECT_TRUE(callbacks.Run());
	EXPECT_FALSE(nested);
	EXPECT_EQ(stopped, 0);
	// A function that a function adds runs from the next frame.
	EXPECT_EQ(late, 0);
	callbacks.Run();
	EXPECT_EQ(late, 1);
}

TEST(FrameCallbacks, ClearDuringRunStopsTheRestOfTheFrame)
{
	FrameCallbacks callbacks;
	int after = 0;
	callbacks.Add("clear", [&] { callbacks.Clear(); });
	callbacks.Add("after", [&] { ++after; });
	EXPECT_TRUE(callbacks.Run());
	EXPECT_EQ(after, 0);
	EXPECT_TRUE(callbacks.Statuses().empty());
}

TEST(UnityMath, WrapsAngles)
{
	EXPECT_FLOAT_EQ(WrapDegrees(190.0f), -170.0f);
	EXPECT_FLOAT_EQ(WrapDegrees(-190.0f), 170.0f);
	EXPECT_NEAR(WrapDegrees(730.0f), 10.0f, 1e-4f);
}

TEST(UnityMath, GivesTheRollOfARotation)
{
	// A yaw and a pitch have no roll, also when the camera looks up or turns far.
	for (const auto pitch : {-80.0f, -10.0f, 0.0f, 20.0f, 85.0f})
	{
		for (const auto yaw : {-170.0f, -86.6f, 0.0f, 45.0f, 179.0f})
		{
			EXPECT_NEAR(RollDegrees(Multiply(YawRotation(yaw), PitchRotation(pitch))), 0.0f, 1e-3f);
		}
	}
	// Unity's Euler(10, 30, 12) applies the roll about z first.
	const auto half = 12.0f * k_DegreesToRadians * 0.5f;
	const Quaternion roll {.x = 0.0f, .y = 0.0f, .z = std::sin(half), .w = std::cos(half)};
	EXPECT_NEAR(RollDegrees(Multiply(Multiply(YawRotation(30.0f), PitchRotation(10.0f)), roll)), 12.0f, 1e-3f);
}

TEST(UnityMath, LooksAlongTheForwardDirection)
{
	const auto identity = LookRotation({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
	EXPECT_NEAR(identity.w, 1.0f, 1e-6f);
	for (const auto forward : {WorldPoint {1.0f, 2.0f, 3.0f}, WorldPoint {0.0f, 0.0f, -1.0f}, WorldPoint {-1.0f, -0.2f, 0.1f},
	                           WorldPoint {0.0f, 1.0f, 0.0f}})
	{
		const auto rotation = LookRotation(forward, {0.0f, 1.0f, 0.0f});
		ExpectNear(Rotate(rotation, {0.0f, 0.0f, 1.0f}), Normalize(forward));
		// The x axis stays horizontal if the forward direction is not vertical.
		if (std::abs(Normalize(forward).y) < 0.99f)
		{
			EXPECT_NEAR(Rotate(rotation, {1.0f, 0.0f, 0.0f}).y, 0.0f, 1e-5f);
		}
	}
}

TEST(MainThreadTasks, RunsATaskFromAnotherThreadAndGivesTheResult)
{
	MainThreadTasks tasks;
	std::future<nlohmann::json> result;
	std::thread([&] { result = tasks.Post(MainThreadTasks::Task([] { return nlohmann::json(42); })); }).join();
	EXPECT_EQ(tasks.Pending(), 1u);
	tasks.RunPending();
	EXPECT_EQ(tasks.Pending(), 0u);
	EXPECT_EQ(result.get(), 42);

	auto failed = tasks.Post(MainThreadTasks::Task([]() -> nlohmann::json { throw std::runtime_error("no sky"); }));
	tasks.RunPending();
	EXPECT_THROW(failed.get(), std::runtime_error);
}

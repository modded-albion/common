/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <chrono>
#include <numbers>

#include <gtest/gtest.h>

#include "moa/astro/Moon.h"

using namespace moa::astro;
using namespace std::chrono;

namespace
{

/// Make a UTC time from a date, hours and minutes.
system_clock::time_point Utc(year_month_day date, int hour, int minute)
{
	return sys_days(date) + hours(hour) + minutes(minute);
}

/// Return the smallest angle between two directions, in degrees.
double AngleBetween(double a, double b)
{
	const auto difference = std::fmod(std::abs(a - b), 360.0);
	return difference > 180.0 ? 360.0 - difference : difference;
}

/// Meeus uses Terrestrial Time in his examples. Change such a time to UTC for these functions.
double UtcFromDynamical(double julianDay)
{
	return julianDay - k_DeltaTSeconds / 86400.0;
}

struct PublishedPhase
{
	system_clock::time_point time;
	bool full;
};

} // namespace

TEST(Astro, GivesTheJulianDayOfAUtcTime)
{
	EXPECT_DOUBLE_EQ(JulianDay(Utc(2000y / January / 1d, 12, 0)), 2451545.0);
	EXPECT_DOUBLE_EQ(JulianDay(Utc(1987y / April / 10d, 19, 21)), 2446896.30625);
}

TEST(Astro, GivesTheSiderealTimeOfMeeusExample12a)
{
	// 1987 April 10 at 0h UT: apparent sidereal time 13h 10m 46.1351s.
	const auto expected = (13.0 + 10.0 / 60.0 + 46.1351 / 3600.0) * 15.0;
	EXPECT_NEAR(GreenwichSiderealTime(2446895.5), expected, 0.001);
}

TEST(Astro, GivesTheHorizontalDirectionOfMeeusExample13b)
{
	// Venus from the US Naval Observatory, 1987 April 10 at 19:21 UT.
	const Equatorial venus {.rightAscension = (23.0 + 9.0 / 60.0 + 16.641 / 3600.0) * 15.0,
	                        .declination = -(6.0 + 43.0 / 60.0 + 11.61 / 3600.0)};
	const Observer washington {.latitude = 38.0 + 55.0 / 60.0 + 17.0 / 3600.0,
	                           .longitude = -(77.0 + 3.0 / 60.0 + 56.0 / 3600.0)};
	const auto direction = ToHorizontal(venus, washington, 2446896.30625);
	EXPECT_NEAR(direction.altitude, 15.1249, 0.01);
	EXPECT_NEAR(direction.azimuth, 68.0337 + 180.0, 0.01); // Meeus gives the azimuth from the south.
}

TEST(Astro, GivesThePositionOfMeeusExample47a)
{
	const auto state = ComputeMoon(UtcFromDynamical(2448724.5)); // 1992 April 12 at 0h TD
	EXPECT_NEAR(state.moon.geometricLongitude, 133.162655, 0.02);
	EXPECT_NEAR(state.moon.longitude, 133.167265, 0.02);
	EXPECT_NEAR(state.moon.latitude, -3.229126, 0.01);
	EXPECT_NEAR(state.moon.distanceKm, 368409.7, 20.0);
	EXPECT_NEAR(state.moon.equatorial.rightAscension, 134.688470, 0.02);
	EXPECT_NEAR(state.moon.equatorial.declination, 13.768368, 0.01);
}

TEST(Astro, GivesThePhaseOfMeeusExample48a)
{
	const auto state = ComputeMoon(UtcFromDynamical(2448724.5));
	EXPECT_NEAR(state.phase.phaseAngle, 69.0756, 0.1);
	EXPECT_NEAR(state.phase.illuminatedFraction, 0.6786, 0.002);
	EXPECT_NEAR(state.phase.brightLimbAngle, 285.0, 1.0);
	EXPECT_TRUE(state.phase.waxing);
}

TEST(Astro, FindsThePublishedNewAndFullMoons)
{
	// Published times (UTC) of new and full moons, many of them eclipses.
	const PublishedPhase phases[] = {
	    {Utc(2024y / January / 11d, 11, 57), false},   {Utc(2024y / January / 25d, 17, 54), true},
	    {Utc(2024y / April / 8d, 18, 21), false},      {Utc(2024y / September / 18d, 2, 34), true},
	    {Utc(2024y / October / 2d, 18, 49), false},    {Utc(2025y / March / 14d, 6, 55), true},
	    {Utc(2025y / March / 29d, 10, 58), false},     {Utc(2025y / September / 7d, 18, 9), true},
	    {Utc(2025y / September / 21d, 19, 54), false}, {Utc(2026y / February / 17d, 12, 1), false},
	    {Utc(2026y / March / 3d, 11, 38), true},       {Utc(2026y / August / 12d, 17, 37), false},
	    {Utc(2026y / August / 28d, 4, 18), true},
	};
	for (const auto& published : phases)
	{
		const auto state = ComputeMoon(published.time);
		SCOPED_TRACE(state.julianDay);
		if (published.full)
		{
			EXPECT_NEAR(state.phase.elongation, 180.0, 0.3);
			EXPECT_GT(state.phase.illuminatedFraction, 0.995);
			EXPECT_EQ(PhaseFrame(state.phase), 4);
			EXPECT_NEAR(state.phase.ageDays, k_SynodicMonthDays / 2.0, 0.05);
		}
		else
		{
			EXPECT_LT(AngleBetween(state.phase.elongation, 0.0), 0.3);
			EXPECT_LT(state.phase.illuminatedFraction, 0.003);
			EXPECT_EQ(PhaseFrame(state.phase), 0);
		}
	}
}

TEST(Astro, TellsWaxingFromWaning)
{
	const auto newMoon = Utc(2026y / August / 12d, 17, 37);
	const auto fullMoon = Utc(2026y / August / 28d, 4, 18);

	const auto crescent = ComputeMoon(newMoon + days(3));
	EXPECT_TRUE(crescent.phase.waxing);
	EXPECT_NEAR(crescent.phase.ageDays, 3.0, 0.5);
	EXPECT_EQ(PhaseFrame(crescent.phase), 1);
	// The lit limb of a waxing moon points to the west of north-south, near the sun.
	EXPECT_GT(crescent.phase.brightLimbAngle, 180.0);

	const auto waning = ComputeMoon(fullMoon + days(4));
	EXPECT_FALSE(waning.phase.waxing);
	EXPECT_EQ(PhaseFrame(waning.phase), 5);
	EXPECT_LT(waning.phase.brightLimbAngle, 180.0);

	// Half way between new and full, the sun lights half of the disc (first quarter).
	const auto quarter = ComputeMoon(newMoon + (fullMoon - newMoon) / 2);
	EXPECT_NEAR(quarter.phase.illuminatedFraction, 0.5, 0.06);
	EXPECT_EQ(PhaseFrame(quarter.phase), 2);
}

TEST(Astro, PutsTheMoonAtTheZenithBelowIt)
{
	const auto state = ComputeMoon(Utc(2025y / March / 14d, 6, 55));
	const auto gst = GreenwichSiderealTime(state.julianDay);
	const Observer below {.latitude = state.moon.equatorial.declination,
	                      .longitude = std::remainder(state.moon.equatorial.rightAscension - gst, 360.0)};
	EXPECT_NEAR(MoonHorizontal(state, below).altitude, 90.0, 0.01);

	// On the other side of the Earth the moon is below the horizon.
	const Observer opposite {.latitude = -below.latitude, .longitude = std::remainder(below.longitude + 180.0, 360.0)};
	EXPECT_NEAR(MoonHorizontal(state, opposite).altitude, -90.0, 1.5);
}

TEST(Astro, AddsTheParallaxOfTheMoonNearTheHorizon)
{
	const auto state = ComputeMoon(Utc(2025y / March / 14d, 6, 55));
	const Observer london {.latitude = 51.48, .longitude = 0.0};
	const auto geocentric = ToHorizontal(state.moon.equatorial, london, state.julianDay);
	const auto topocentric = MoonHorizontal(state, london);
	const auto parallax = std::asin(6378.14 / state.moon.distanceKm) * 180.0 / 3.14159265358979;
	EXPECT_NEAR(geocentric.altitude - topocentric.altitude, parallax * std::cos(geocentric.altitude * 3.14159265358979 / 180.0),
	            1e-6);
	EXPECT_DOUBLE_EQ(geocentric.azimuth, topocentric.azimuth);
}

TEST(Astro, GivesEightFramesInTheOrderOfTheGame)
{
	MoonPhase phase;
	const int expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 0};
	for (int i = 0; i <= 8; ++i)
	{
		phase.cycleFraction = i / 8.0;
		EXPECT_EQ(PhaseFrame(phase), expected[i]);
	}
	phase.cycleFraction = 1.0 / 16.0 - 1e-6;
	EXPECT_EQ(PhaseFrame(phase), 0);
	phase.cycleFraction = 1.0 / 16.0 + 1e-6;
	EXPECT_EQ(PhaseFrame(phase), 1);
	EXPECT_EQ(PhaseFrame(phase, 0), 0);
}

TEST(Astro, GivesTheNorthAndEastOnTheSky)
{
	const auto expectNear = [](const LocalDirection& actual, const LocalDirection& expected) {
		EXPECT_NEAR(actual.north, expected.north, 1e-9);
		EXPECT_NEAR(actual.east, expected.east, 1e-9);
		EXPECT_NEAR(actual.up, expected.up, 1e-9);
	};
	const auto dot = [](const LocalDirection& a, const LocalDirection& b) {
		return a.north * b.north + a.east * b.east + a.up * b.up;
	};
	// At the equator, the zenith has the north toward the north on the horizon and the east toward the east.
	const auto zenith = SkyAxesAt({.altitude = 90.0, .azimuth = 0.0}, 0.0);
	expectNear(zenith.north, {1.0, 0.0, 0.0});
	expectNear(zenith.east, {0.0, 1.0, 0.0});
	// At the equator, a star that rises in the east goes up. Thus the east on the sky points down.
	const auto rising = SkyAxesAt({.altitude = 0.0, .azimuth = 90.0}, 0.0);
	expectNear(rising.north, {1.0, 0.0, 0.0});
	expectNear(rising.east, {0.0, 0.0, -1.0});
	// At the north pole, the north on the sky is toward the zenith in all directions.
	EXPECT_NEAR(SkyAxesAt({.altitude = 10.0, .azimuth = 200.0}, 90.0).north.up, std::cos(10.0 * std::numbers::pi / 180.0),
	            1e-9);
	// At the celestial pole, the axes are still at 90 degrees to each other and to the line of sight.
	const Horizontal pole {.altitude = 51.5, .azimuth = 0.0};
	const auto atPole = SkyAxesAt(pole, 51.5);
	EXPECT_NEAR(dot(atPole.north, ToLocalDirection(pole)), 0.0, 1e-9);
	EXPECT_NEAR(dot(atPole.north, atPole.east), 0.0, 1e-9);
	EXPECT_NEAR(dot(atPole.east, atPole.east), 1.0, 1e-9);
}

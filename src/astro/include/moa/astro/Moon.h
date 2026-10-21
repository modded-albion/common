/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <chrono>

namespace moa::astro
{

/// The mean length of a lunar month (new moon to new moon), in days.
constexpr double k_SynodicMonthDays = 29.530588853;

/// The difference between Terrestrial Time and UTC for the years 2020 to 2030, in seconds. The formulas use
/// Terrestrial Time. An error of one minute in this value moves the moon by less than one minute of arc.
constexpr double k_DeltaTSeconds = 69.2;

/// Return the Julian Day of a UTC time.
[[nodiscard]] double JulianDay(std::chrono::system_clock::time_point utc);

/// A position on the sky in degrees. The right ascension is in the range 0 to 360.
struct Equatorial
{
	double rightAscension = 0.0;
	double declination = 0.0;
};

/// A direction above the horizon of an observer, in degrees. The altitude is the angle above the horizon. The
/// azimuth is the angle from north to east, in the range 0 to 360.
struct Horizontal
{
	double altitude = 0.0;
	double azimuth = 0.0;
};

/// A point on the Earth in degrees: north latitude and east longitude are positive.
struct Observer
{
	double latitude = 0.0;
	double longitude = 0.0;
};

/// The apparent position of the sun.
struct SunPosition
{
	double longitude = 0.0; ///< Apparent ecliptic longitude, degrees.
	Equatorial equatorial;
	double distanceKm = 0.0;
};

/// The position of the moon from the centre of the Earth.
struct MoonPosition
{
	double geometricLongitude = 0.0; ///< Ecliptic longitude without nutation, degrees.
	double longitude = 0.0;          ///< Apparent ecliptic longitude, degrees.
	double latitude = 0.0;           ///< Ecliptic latitude, degrees.
	double distanceKm = 0.0;
	Equatorial equatorial; ///< Apparent right ascension and declination.
};

/// The phase of the moon.
struct MoonPhase
{
	double phaseAngle = 0.0;          ///< Angle sun-moon-Earth in degrees: 0 at full moon, 180 at new moon.
	double illuminatedFraction = 0.0; ///< The part of the disc that the sun lights, 0 to 1.
	double elongation = 0.0;          ///< Moon longitude less sun longitude, 0 to 360: 0 new, 90 first quarter.
	double cycleFraction = 0.0;       ///< The elongation divided by 360: the part of the lunar month that passed.
	double ageDays = 0.0;             ///< The approximate time since the new moon, in days.
	bool waxing = false;              ///< True from new moon to full moon.
	double brightLimbAngle = 0.0;     ///< Position angle of the lit limb from north to east, degrees.
};

/// The moon and the sun at one time.
struct MoonState
{
	double julianDay = 0.0; ///< UTC.
	MoonPosition moon;
	SunPosition sun;
	MoonPhase phase;
};

/// Calculate the moon at a UTC Julian Day. The method is the low precision series of Meeus, "Astronomical
/// Algorithms", chapters 25, 47 and 48 (the largest terms only). The error is approximately 0.05 degrees.
[[nodiscard]] MoonState ComputeMoon(double julianDay);

/// Calculate the moon at a UTC time.
[[nodiscard]] MoonState ComputeMoon(std::chrono::system_clock::time_point utc);

/// Return the apparent sidereal time at Greenwich in degrees, 0 to 360.
[[nodiscard]] double GreenwichSiderealTime(double julianDay);

/// Change an apparent position on the sky to the horizontal direction for an observer. There is no parallax and
/// no refraction.
[[nodiscard]] Horizontal ToHorizontal(Equatorial position, Observer observer, double julianDay);

/// Return the direction of the moon for an observer on the surface of the Earth. The altitude includes the
/// parallax of the moon. It does not include refraction.
[[nodiscard]] Horizontal MoonHorizontal(const MoonState& state, Observer observer);

/// A direction in the frame of an observer, with a length of 1: the parts to the north and to the east on the
/// horizon, and the part up.
struct LocalDirection
{
	double north = 0.0;
	double east = 0.0;
	double up = 0.0;
};

/// Return the direction of a horizontal position in the frame of the observer.
[[nodiscard]] LocalDirection ToLocalDirection(Horizontal direction);

/// Two directions on the sky at one point, both at 90 degrees to the line of sight.
struct SkyAxes
{
	LocalDirection north; ///< Toward the north celestial pole.
	LocalDirection east;  ///< Toward the east: the direction of a larger right ascension.
};

/// Return the north and east directions on the sky at `direction`, for an observer at `latitude`. A position
/// angle (for example the bright limb angle) is measured from `north` toward `east`. At a celestial pole the
/// directions are not defined. The function then uses the up direction, or at the zenith the north on the horizon.
[[nodiscard]] SkyAxes SkyAxesAt(Horizontal direction, double latitude);

/// Return the phase as one of eight frames: 0 new, 2 first quarter, 4 full, 6 last quarter. The game uses the
/// same order for its moon frames.
[[nodiscard]] int PhaseFrame(const MoonPhase& phase, int frameCount = 8);

} // namespace moa::astro

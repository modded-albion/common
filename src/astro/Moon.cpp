/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "astro/Moon.h"

#include <cmath>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <numbers>

namespace moa::astro
{

namespace
{
constexpr double k_DegreesToRadians = std::numbers::pi / 180.0;
constexpr double k_UnixEpochJulianDay = 2440587.5;
constexpr double k_J2000JulianDay = 2451545.0;
constexpr double k_DaysPerCentury = 36525.0;
constexpr double k_SecondsPerDay = 86400.0;
constexpr double k_AstronomicalUnitKm = 149597870.7;
constexpr double k_EarthRadiusKm = 6378.14;

[[nodiscard]] double Sin(double degrees)
{
	return std::sin(degrees * k_DegreesToRadians);
}

[[nodiscard]] double Cos(double degrees)
{
	return std::cos(degrees * k_DegreesToRadians);
}

[[nodiscard]] double Atan2Degrees(double y, double x)
{
	return std::atan2(y, x) / k_DegreesToRadians;
}

[[nodiscard]] double AsinDegrees(double value)
{
	return std::asin(std::clamp(value, -1.0, 1.0)) / k_DegreesToRadians;
}

[[nodiscard]] double Normalize(double degrees)
{
	const auto value = std::fmod(degrees, 360.0);
	return value < 0.0 ? value + 360.0 : value;
}

/// One periodic term of the moon: the multiples of D, M, M' and F, and the amplitude.
struct Term
{
	int d;
	int m;
	int mPrime;
	int f;
	double amplitude;
};

// The largest terms of Meeus table 47.A. Longitude amplitudes are in millionths of a degree, distance amplitudes in
// metres.
constexpr std::array<Term, 30> k_LongitudeTerms {{
    {0, 0, 1, 0, 6288774}, {2, 0, -1, 0, 1274027}, {2, 0, 0, 0, 658314},  {0, 0, 2, 0, 213618}, {0, 1, 0, 0, -185116},
    {0, 0, 0, 2, -114332}, {2, 0, -2, 0, 58793},   {2, -1, -1, 0, 57066}, {2, 0, 1, 0, 53322},  {2, -1, 0, 0, 45758},
    {0, 1, -1, 0, -40923}, {1, 0, 0, 0, -34720},   {0, 1, 1, 0, -30383},  {2, 0, 0, -2, 15327}, {0, 0, 1, 2, -12528},
    {0, 0, 1, -2, 10980},  {4, 0, -1, 0, 10675},   {0, 0, 3, 0, 10034},   {4, 0, -2, 0, 8548},  {2, 1, -1, 0, -7888},
    {2, 1, 0, 0, -6766},   {1, 0, -1, 0, -5163},   {1, 1, 0, 0, 4987},    {2, -1, 1, 0, 4036},  {2, 0, 2, 0, 3994},
    {4, 0, 0, 0, 3861},    {2, 0, -3, 0, 3665},    {0, 1, -2, 0, -2689},  {2, 0, -1, 2, -2602}, {2, -1, -2, 0, 2390},
}};

constexpr std::array<Term, 30> k_DistanceTerms {{
    {0, 0, 1, 0, -20905355}, {2, 0, -1, 0, -3699111}, {2, 0, 0, 0, -2955968},  {0, 0, 2, 0, -569925}, {0, 1, 0, 0, 48888},
    {0, 0, 0, 2, -3149},     {2, 0, -2, 0, 246158},   {2, -1, -1, 0, -152138}, {2, 0, 1, 0, -170733}, {2, -1, 0, 0, -204586},
    {0, 1, -1, 0, -129620},  {1, 0, 0, 0, 108743},    {0, 1, 1, 0, 104755},    {2, 0, 0, -2, 10321},  {0, 0, 1, -2, 79661},
    {4, 0, -1, 0, -34782},   {0, 0, 3, 0, -23210},    {4, 0, -2, 0, -21636},   {2, 1, -1, 0, 24208},  {2, 1, 0, 0, 30824},
    {1, 0, -1, 0, -8379},    {1, 1, 0, 0, -16675},    {2, -1, 1, 0, -12831},   {2, 0, 2, 0, -10445},  {4, 0, 0, 0, -11650},
    {2, 0, -3, 0, 14403},    {0, 1, -2, 0, -7003},    {2, -1, -2, 0, 10056},   {1, 0, 1, 0, 6322},    {2, -2, 0, 0, -9884},
}};

// The largest terms of Meeus table 47.B, in millionths of a degree.
constexpr std::array<Term, 20> k_LatitudeTerms {{
    {0, 0, 0, 1, 5128122}, {0, 0, 1, 1, 280602},  {0, 0, 1, -1, 277693}, {2, 0, 0, -1, 173237}, {2, 0, -1, 1, 55413},
    {2, 0, -1, -1, 46271}, {2, 0, 0, 1, 32573},   {0, 0, 2, 1, 17198},   {2, 0, 1, -1, 9266},   {0, 0, 2, -1, 8822},
    {2, -1, 0, -1, 8216},  {2, 0, -2, -1, 4324},  {2, 0, 1, 1, 4200},    {2, 1, 0, -1, -3359},  {2, -1, -1, 1, 2463},
    {2, -1, 0, 1, 2211},   {2, -1, -1, -1, 2065}, {0, 1, -1, -1, -1870}, {4, 0, -1, -1, 1828},  {0, 1, 0, 1, -1794},
}};

/// The arguments of the series at one time, in degrees.
struct Arguments
{
	double d;
	double m;
	double mPrime;
	double f;
	double e; ///< The factor for terms that use the anomaly of the sun.
};

/// Return the sum of the terms. The sine flag selects sine or cosine.
template <size_t N>
[[nodiscard]] double SumTerms(const std::array<Term, N>& terms, const Arguments& arguments, bool sine)
{
	double sum = 0.0;
	for (const auto& term : terms)
	{
		const auto angle = term.d * arguments.d + term.m * arguments.m + term.mPrime * arguments.mPrime + term.f * arguments.f;
		const auto factor = std::pow(arguments.e, std::abs(term.m));
		sum += term.amplitude * factor * (sine ? Sin(angle) : Cos(angle));
	}
	return sum;
}

/// The nutation in longitude and in obliquity, in degrees (Meeus chapter 22, low precision).
struct Nutation
{
	double longitude;
	double obliquity;
};

[[nodiscard]] Nutation ComputeNutation(double centuries)
{
	const auto node = 125.04452 - 1934.136261 * centuries;
	const auto sunLongitude = 280.4665 + 36000.7698 * centuries;
	const auto moonLongitude = 218.3165 + 481267.8813 * centuries;
	const auto longitude =
	    -17.20 * Sin(node) - 1.32 * Sin(2.0 * sunLongitude) - 0.23 * Sin(2.0 * moonLongitude) + 0.21 * Sin(2.0 * node);
	const auto obliquity =
	    9.20 * Cos(node) + 0.57 * Cos(2.0 * sunLongitude) + 0.10 * Cos(2.0 * moonLongitude) - 0.09 * Cos(2.0 * node);
	return {.longitude = longitude / 3600.0, .obliquity = obliquity / 3600.0};
}

/// Return the mean obliquity of the ecliptic in degrees (Meeus 22.2).
[[nodiscard]] double MeanObliquity(double centuries)
{
	return 23.0 + 26.0 / 60.0 + (21.448 - 46.8150 * centuries - 0.00059 * centuries * centuries) / 3600.0;
}

[[nodiscard]] Equatorial EclipticToEquatorial(double longitude, double latitude, double obliquity)
{
	const auto rightAscension = Atan2Degrees(
	    Sin(longitude) * Cos(obliquity) - std::tan(latitude * k_DegreesToRadians) * Sin(obliquity), Cos(longitude));
	const auto declination = AsinDegrees(Sin(latitude) * Cos(obliquity) + Cos(latitude) * Sin(obliquity) * Sin(longitude));
	return {.rightAscension = Normalize(rightAscension), .declination = declination};
}

/// Calculate the sun (Meeus chapter 25, low precision).
[[nodiscard]] SunPosition ComputeSun(double centuries, double obliquity)
{
	const auto t = centuries;
	const auto meanLongitude = 280.46646 + 36000.76983 * t + 0.0003032 * t * t;
	const auto anomaly = 357.52911 + 35999.05029 * t - 0.0001537 * t * t;
	const auto eccentricity = 0.016708634 - 0.000042037 * t - 0.0000001267 * t * t;
	const auto centre = (1.914602 - 0.004817 * t - 0.000014 * t * t) * Sin(anomaly) +
	                    (0.019993 - 0.000101 * t) * Sin(2.0 * anomaly) + 0.000289 * Sin(3.0 * anomaly);
	const auto trueLongitude = meanLongitude + centre;
	const auto trueAnomaly = anomaly + centre;
	const auto distanceAu = 1.000001018 * (1.0 - eccentricity * eccentricity) / (1.0 + eccentricity * Cos(trueAnomaly));
	const auto node = 125.04 - 1934.136 * t;
	const auto apparentLongitude = Normalize(trueLongitude - 0.00569 - 0.00478 * Sin(node));
	const auto apparentObliquity = obliquity + 0.00256 * Cos(node);
	return {
	    .longitude = apparentLongitude,
	    .equatorial = EclipticToEquatorial(apparentLongitude, 0.0, apparentObliquity),
	    .distanceKm = distanceAu * k_AstronomicalUnitKm,
	};
}

/// Calculate the moon from the centre of the Earth (Meeus chapter 47).
[[nodiscard]] MoonPosition ComputeMoonPosition(double centuries, const Nutation& nutation, double obliquity)
{
	const auto t = centuries;
	const auto t2 = t * t;
	const auto t3 = t2 * t;
	const auto t4 = t3 * t;
	const auto meanLongitude = 218.3164477 + 481267.88123421 * t - 0.0015786 * t2 + t3 / 538841.0 - t4 / 65194000.0;
	const Arguments arguments {
	    .d = 297.8501921 + 445267.1114034 * t - 0.0018819 * t2 + t3 / 545868.0 - t4 / 113065000.0,
	    .m = 357.5291092 + 35999.0502909 * t - 0.0001536 * t2 + t3 / 24490000.0,
	    .mPrime = 134.9633964 + 477198.8675055 * t + 0.0087414 * t2 + t3 / 69699.0 - t4 / 14712000.0,
	    .f = 93.2720950 + 483202.0175233 * t - 0.0036539 * t2 - t3 / 3526000.0 + t4 / 863310000.0,
	    .e = 1.0 - 0.002516 * t - 0.0000074 * t2,
	};
	const auto a1 = 119.75 + 131.849 * t;
	const auto a2 = 53.09 + 479264.290 * t;
	const auto a3 = 313.45 + 481266.484 * t;

	auto sumLongitude = SumTerms(k_LongitudeTerms, arguments, true);
	sumLongitude += 3958.0 * Sin(a1) + 1962.0 * Sin(meanLongitude - arguments.f) + 318.0 * Sin(a2);
	auto sumLatitude = SumTerms(k_LatitudeTerms, arguments, true);
	sumLatitude += -2235.0 * Sin(meanLongitude) + 382.0 * Sin(a3) + 175.0 * Sin(a1 - arguments.f) +
	               175.0 * Sin(a1 + arguments.f) + 127.0 * Sin(meanLongitude - arguments.mPrime) -
	               115.0 * Sin(meanLongitude + arguments.mPrime);
	const auto sumDistance = SumTerms(k_DistanceTerms, arguments, false);

	const auto geometricLongitude = Normalize(meanLongitude + sumLongitude / 1e6);
	const auto latitude = sumLatitude / 1e6;
	const auto longitude = Normalize(geometricLongitude + nutation.longitude);
	return {
	    .geometricLongitude = geometricLongitude,
	    .longitude = longitude,
	    .latitude = latitude,
	    .distanceKm = 385000.56 + sumDistance / 1000.0,
	    .equatorial = EclipticToEquatorial(longitude, latitude, obliquity + nutation.obliquity),
	};
}

/// Calculate the phase from the moon and the sun (Meeus chapter 48).
[[nodiscard]] MoonPhase ComputePhase(const MoonPosition& moon, const SunPosition& sun)
{
	const auto cosElongation = Cos(moon.latitude) * Cos(moon.longitude - sun.longitude);
	const auto geocentricElongation = std::acos(std::clamp(cosElongation, -1.0, 1.0));
	const auto phaseAngle = Atan2Degrees(sun.distanceKm * std::sin(geocentricElongation),
	                                     moon.distanceKm - sun.distanceKm * std::cos(geocentricElongation));
	const auto elongation = Normalize(moon.longitude - sun.longitude);

	const auto& m = moon.equatorial;
	const auto& s = sun.equatorial;
	const auto brightLimb =
	    Atan2Degrees(Cos(s.declination) * Sin(s.rightAscension - m.rightAscension),
	                 Sin(s.declination) * Cos(m.declination) -
	                     Cos(s.declination) * Sin(m.declination) * Cos(s.rightAscension - m.rightAscension));
	return {
	    .phaseAngle = phaseAngle,
	    .illuminatedFraction = (1.0 + Cos(phaseAngle)) / 2.0,
	    .elongation = elongation,
	    .cycleFraction = elongation / 360.0,
	    .ageDays = elongation / 360.0 * k_SynodicMonthDays,
	    .waxing = elongation < 180.0,
	    .brightLimbAngle = Normalize(brightLimb),
	};
}

} // namespace

double JulianDay(std::chrono::system_clock::time_point utc)
{
	const auto seconds = std::chrono::duration<double>(utc.time_since_epoch()).count();
	return k_UnixEpochJulianDay + seconds / k_SecondsPerDay;
}

MoonState ComputeMoon(double julianDay)
{
	const auto dynamicalDay = julianDay + k_DeltaTSeconds / k_SecondsPerDay;
	const auto centuries = (dynamicalDay - k_J2000JulianDay) / k_DaysPerCentury;
	const auto nutation = ComputeNutation(centuries);
	const auto obliquity = MeanObliquity(centuries);
	const auto sun = ComputeSun(centuries, obliquity);
	const auto moon = ComputeMoonPosition(centuries, nutation, obliquity);
	return {.julianDay = julianDay, .moon = moon, .sun = sun, .phase = ComputePhase(moon, sun)};
}

MoonState ComputeMoon(std::chrono::system_clock::time_point utc)
{
	return ComputeMoon(JulianDay(utc));
}

double GreenwichSiderealTime(double julianDay)
{
	const auto centuries = (julianDay - k_J2000JulianDay) / k_DaysPerCentury;
	const auto mean = 280.46061837 + 360.98564736629 * (julianDay - k_J2000JulianDay) + 0.000387933 * centuries * centuries -
	                  centuries * centuries * centuries / 38710000.0;
	const auto nutation = ComputeNutation(centuries);
	const auto obliquity = MeanObliquity(centuries) + nutation.obliquity;
	return Normalize(mean + nutation.longitude * Cos(obliquity));
}

Horizontal ToHorizontal(Equatorial position, Observer observer, double julianDay)
{
	const auto hourAngle = GreenwichSiderealTime(julianDay) + observer.longitude - position.rightAscension;
	const auto latitude = observer.latitude;
	const auto declination = position.declination;
	const auto altitude = AsinDegrees(Sin(latitude) * Sin(declination) + Cos(latitude) * Cos(declination) * Cos(hourAngle));
	// Meeus measures this angle from the south to the west. Add 180 degrees to measure from the north to the east.
	const auto fromSouth = Atan2Degrees(Sin(hourAngle), Cos(hourAngle) * Sin(latitude) -
	                                                        std::tan(declination * k_DegreesToRadians) * Cos(latitude));
	return {.altitude = altitude, .azimuth = Normalize(fromSouth + 180.0)};
}

Horizontal MoonHorizontal(const MoonState& state, Observer observer)
{
	auto direction = ToHorizontal(state.moon.equatorial, observer, state.julianDay);
	const auto parallax = AsinDegrees(k_EarthRadiusKm / state.moon.distanceKm);
	direction.altitude -= parallax * Cos(direction.altitude);
	return direction;
}

LocalDirection ToLocalDirection(Horizontal direction)
{
	return {
	    .north = Cos(direction.altitude) * Cos(direction.azimuth),
	    .east = Cos(direction.altitude) * Sin(direction.azimuth),
	    .up = Sin(direction.altitude),
	};
}

namespace
{
[[nodiscard]] double Dot(const LocalDirection& a, const LocalDirection& b)
{
	return a.north * b.north + a.east * b.east + a.up * b.up;
}

/// The cross product, with north, east and up in the place of x, y and z.
[[nodiscard]] LocalDirection Cross(const LocalDirection& a, const LocalDirection& b)
{
	return {
	    .north = a.east * b.up - a.up * b.east,
	    .east = a.up * b.north - a.north * b.up,
	    .up = a.north * b.east - a.east * b.north,
	};
}

[[nodiscard]] LocalDirection Unit(const LocalDirection& a)
{
	const auto length = std::sqrt(Dot(a, a));
	return length > 1e-12 ? LocalDirection {a.north / length, a.east / length, a.up / length} : LocalDirection {};
}
} // namespace

SkyAxes SkyAxesAt(Horizontal direction, double latitude)
{
	const auto d = ToLocalDirection(direction);
	auto pole = LocalDirection {.north = Cos(latitude), .east = 0.0, .up = Sin(latitude)};
	for (const auto fallback : {LocalDirection {0.0, 0.0, 1.0}, LocalDirection {1.0, 0.0, 0.0}})
	{
		if (std::abs(Dot(pole, d)) > 1.0 - 1e-9)
		{
			pole = fallback;
		}
	}
	const auto along = Dot(pole, d);
	const auto north = Unit({pole.north - along * d.north, pole.east - along * d.east, pole.up - along * d.up});
	// The sky turns to the west about the pole, so the east is the direction of d x pole.
	return {.north = north, .east = Unit(Cross(d, north))};
}

int PhaseFrame(const MoonPhase& phase, int frameCount)
{
	if (frameCount <= 0)
	{
		return 0;
	}
	const auto frame = static_cast<int>(std::floor(phase.cycleFraction * frameCount + 0.5));
	return frame % frameCount;
}

} // namespace moa::astro

/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "astro/Sky.h"

#include <cmath>

#include <algorithm>
#include <numbers>

namespace moa::astro
{

namespace
{
constexpr double k_DegreesToRadians = std::numbers::pi / 180.0;
constexpr double k_ArcsecondsToDegrees = 1.0 / 3600.0;
constexpr double k_J2000JulianDay = 2451545.0;
constexpr double k_DaysPerCentury = 36525.0;
constexpr double k_HighestKp = 9.0;
constexpr double k_QuietEdge = 68.0;
constexpr double k_EdgeStepPerKp = 2.0;
constexpr double k_QuietWidth = 4.0;
constexpr double k_WidthStepPerKp = 0.5;
constexpr double k_OverheadEdgeOffset = 1.5;
constexpr double k_OverheadWidth = 6.0;
constexpr double k_GreenLayerKm = 150.0;
constexpr double k_LowestVisibleAltitude = 10.0;

[[nodiscard]] double Sin(double degrees)
{
	return std::sin(degrees * k_DegreesToRadians);
}

[[nodiscard]] double Cos(double degrees)
{
	return std::cos(degrees * k_DegreesToRadians);
}

/// The turn of the axes about z by `degrees`: a vector keeps its place and gets the parts in the new axes.
[[nodiscard]] Matrix3 AxesAboutZ(double degrees)
{
	const auto c = Cos(degrees);
	const auto s = Sin(degrees);
	return {{Vec3 {c, s, 0.0}, Vec3 {-s, c, 0.0}, Vec3 {0.0, 0.0, 1.0}}};
}

/// The turn of the axes about y by `degrees`.
[[nodiscard]] Matrix3 AxesAboutY(double degrees)
{
	const auto c = Cos(degrees);
	const auto s = Sin(degrees);
	return {{Vec3 {c, 0.0, -s}, Vec3 {0.0, 1.0, 0.0}, Vec3 {s, 0.0, c}}};
}
} // namespace

Vec3 Normalize(Vec3 a)
{
	const auto length = std::sqrt(Dot(a, a));
	return length > 0.0 ? a * (1.0 / length) : Vec3 {0.0, 0.0, 1.0};
}

Matrix3 Matrix3::Transposed() const
{
	return {{Vec3 {rows[0].x, rows[1].x, rows[2].x}, Vec3 {rows[0].y, rows[1].y, rows[2].y},
	         Vec3 {rows[0].z, rows[1].z, rows[2].z}}};
}

Matrix3 operator*(const Matrix3& a, const Matrix3& b)
{
	const auto columns = b.Transposed();
	Matrix3 result;
	for (size_t row = 0; row < 3; ++row)
	{
		result.rows[row] = {Dot(a.rows[row], columns.rows[0]), Dot(a.rows[row], columns.rows[1]),
		                    Dot(a.rows[row], columns.rows[2])};
	}
	return result;
}

Vec3 UnitVector(Equatorial position)
{
	const auto cosDeclination = Cos(position.declination);
	return {cosDeclination * Cos(position.rightAscension), cosDeclination * Sin(position.rightAscension),
	        Sin(position.declination)};
}

Equatorial ToEquatorial(Vec3 direction)
{
	const auto unit = Normalize(direction);
	auto rightAscension = std::atan2(unit.y, unit.x) / k_DegreesToRadians;
	if (rightAscension < 0.0)
	{
		rightAscension += 360.0;
	}
	return {.rightAscension = rightAscension, .declination = std::asin(std::clamp(unit.z, -1.0, 1.0)) / k_DegreesToRadians};
}

Matrix3 PrecessionFromJ2000(double julianDay)
{
	const auto t = (julianDay - k_J2000JulianDay) / k_DaysPerCentury;
	const auto zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t * t * t) * k_ArcsecondsToDegrees;
	const auto z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t * t * t) * k_ArcsecondsToDegrees;
	const auto theta = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t * t * t) * k_ArcsecondsToDegrees;
	return AxesAboutZ(-z) * AxesAboutY(theta) * AxesAboutZ(-zeta);
}

Matrix3 EquatorialToLocal(double localSiderealTime, double latitude)
{
	const auto cosTime = Cos(localSiderealTime);
	const auto sinTime = Sin(localSiderealTime);
	const auto cosLatitude = Cos(latitude);
	const auto sinLatitude = Sin(latitude);
	return {{
	    Vec3 {-sinLatitude * cosTime, -sinLatitude * sinTime, cosLatitude},
	    Vec3 {-sinTime, cosTime, 0.0},
	    Vec3 {cosLatitude * cosTime, cosLatitude * sinTime, sinLatitude},
	}};
}

Matrix3 CatalogueToLocal(double julianDay, Observer observer)
{
	const auto localSiderealTime = GreenwichSiderealTime(julianDay) + observer.longitude;
	return EquatorialToLocal(localSiderealTime, observer.latitude) * PrecessionFromJ2000(julianDay);
}

double GeomagneticLatitude(Observer observer)
{
	const auto pole =
	    UnitVector({.rightAscension = k_GeomagneticNorthPole.longitude, .declination = k_GeomagneticNorthPole.latitude});
	const auto place = UnitVector({.rightAscension = observer.longitude, .declination = observer.latitude});
	return std::asin(std::clamp(Dot(pole, place), -1.0, 1.0)) / k_DegreesToRadians;
}

Matrix3 LocalToGeomagnetic(Observer observer)
{
	// The axes of the Earth: x to the longitude 0 on the equator, z to the north pole. The local axes of the
	// observer have the same form as the sky axes at a sidereal time equal to the longitude.
	const auto localToEarth = EquatorialToLocal(observer.longitude, observer.latitude).Transposed();
	const auto pole =
	    UnitVector({.rightAscension = k_GeomagneticNorthPole.longitude, .declination = k_GeomagneticNorthPole.latitude});
	const Vec3 earthNorth {0.0, 0.0, 1.0};
	const auto x = Normalize(earthNorth - pole * Dot(earthNorth, pole));
	const auto y = Cross(pole, x);
	const Matrix3 earthToGeomagnetic {{x, y, pole}};
	return earthToGeomagnetic * localToEarth;
}

AuroraOval AuroraOvalForKp(double kp)
{
	const auto index = std::clamp(kp, 0.0, k_HighestKp);
	return {.equatorwardEdge = k_QuietEdge - k_EdgeStepPerKp * index, .width = k_QuietWidth + k_WidthStepPerKp * index};
}

AuroraOval AuroraOvalOverhead(double observerGeomagneticLatitude)
{
	return {.equatorwardEdge = std::abs(observerGeomagneticLatitude) + k_OverheadEdgeOffset, .width = k_OverheadWidth};
}

double AltitudeOfAuroraPoint(double distanceDegrees, double heightKm)
{
	if (distanceDegrees <= 0.0)
	{
		return 90.0;
	}
	const auto radius = k_MeanEarthRadiusKm + heightKm;
	const auto across = radius * Sin(distanceDegrees);
	const auto up = radius * Cos(distanceDegrees) - k_MeanEarthRadiusKm;
	return std::atan2(up, across) / k_DegreesToRadians;
}

bool AuroraVisible(double observerGeomagneticLatitude, const AuroraOval& oval)
{
	const auto distance = oval.equatorwardEdge - std::abs(observerGeomagneticLatitude);
	return AltitudeOfAuroraPoint(distance, k_GreenLayerKm) > k_LowestVisibleAltitude;
}

} // namespace moa::astro

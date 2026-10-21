/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <array>

#include <astro/Moon.h>

namespace moa::astro
{

/// A direction or a point with three parts.
struct Vec3
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	friend constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
	friend constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
	friend constexpr Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
};

[[nodiscard]] constexpr double Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// The cross product of a right-handed frame.
[[nodiscard]] constexpr Vec3 Cross(Vec3 a, Vec3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] Vec3 Normalize(Vec3 a);

/// A 3 x 3 matrix, row by row. `Apply` multiplies a column vector.
struct Matrix3
{
	std::array<Vec3, 3> rows {Vec3 {1.0, 0.0, 0.0}, Vec3 {0.0, 1.0, 0.0}, Vec3 {0.0, 0.0, 1.0}};

	[[nodiscard]] constexpr Vec3 Apply(Vec3 v) const { return {Dot(rows[0], v), Dot(rows[1], v), Dot(rows[2], v)}; }
	[[nodiscard]] Matrix3 Transposed() const;
	[[nodiscard]] friend Matrix3 operator*(const Matrix3& a, const Matrix3& b);
};

/// Return the unit vector of a position on the sky: x to the right ascension 0, y to the right ascension 90 degrees,
/// z to the north pole of the sky.
[[nodiscard]] Vec3 UnitVector(Equatorial position);

/// Return the position on the sky of a unit vector in the axes of `UnitVector`.
[[nodiscard]] Equatorial ToEquatorial(Vec3 direction);

/// Return the precession matrix from the equator and equinox J2000 to the mean equator and equinox of `julianDay`.
/// The method is the rigorous method of Meeus, "Astronomical Algorithms", chapter 21.
[[nodiscard]] Matrix3 PrecessionFromJ2000(double julianDay);

/// Return the matrix from the equatorial axes of the date (as `UnitVector`) to the local axes of the observer: x to
/// the north on the horizon, y to the east on the horizon, z up. `localSiderealTime` is in degrees.
[[nodiscard]] Matrix3 EquatorialToLocal(double localSiderealTime, double latitude);

/// Return the matrix from the J2000 axes of a star catalogue to the local axes of the observer (as
/// `EquatorialToLocal`) at a UTC Julian Day. It includes the precession. It does not include nutation, aberration
/// and refraction: together they are less than 0.02 degrees above 10 degrees of altitude.
[[nodiscard]] Matrix3 CatalogueToLocal(double julianDay, Observer observer);

/// The north geomagnetic pole of the IGRF-14 model for the year 2025: 80.8 degrees north, 72.7 degrees west.
inline constexpr Observer k_GeomagneticNorthPole {.latitude = 80.8, .longitude = -72.7};

/// The mean radius of the Earth, in kilometres.
inline constexpr double k_MeanEarthRadiusKm = 6371.0;

/// Return the geomagnetic latitude of a point, in degrees, for a dipole field with the pole `k_GeomagneticNorthPole`.
[[nodiscard]] double GeomagneticLatitude(Observer observer);

/// Return the matrix from the local axes of the observer (as `EquatorialToLocal`) to the geomagnetic axes: z to the
/// north geomagnetic pole, x in the plane of the geomagnetic pole and the north pole of the Earth. The matrix turns
/// directions. A point at the surface of the Earth under the observer has the local direction (0, 0, 1).
[[nodiscard]] Matrix3 LocalToGeomagnetic(Observer observer);

/// The band of the aurora around the geomagnetic pole, in geomagnetic latitude degrees.
struct AuroraOval
{
	double equatorwardEdge = 0.0; ///< The latitude of the edge nearest to the equator.
	double width = 0.0;           ///< The width of the band toward the pole.
};

/// Return the band of the aurora on the night side for a planetary index Kp (0 to 9). A quiet sky (Kp 0) has its
/// edge at 68 degrees. Each Kp step moves the edge 2 degrees toward the equator and makes the band 0.5 degrees
/// wider. With `AuroraVisible`, an observer at London (geomagnetic latitude 53.4) sees the aurora from Kp 5.
[[nodiscard]] AuroraOval AuroraOvalForKp(double kp);

/// Return the band of the aurora for the test mode: the edge is 1.5 degrees (170 km) to the north of the observer, so
/// that the curtains stand high in the northern sky.
[[nodiscard]] AuroraOval AuroraOvalOverhead(double observerGeomagneticLatitude);

/// Return the altitude in degrees at which an observer sees a point at `heightKm` above the edge of the oval. The
/// edge is `distanceDegrees` of arc to the north on the surface of the Earth (zero or less: the point is overhead or
/// to the south, and the result is 90).
[[nodiscard]] double AltitudeOfAuroraPoint(double distanceDegrees, double heightKm);

/// Return true if the observer can see the aurora: the bright green layer (150 km) above the edge of the oval is
/// more than 10 degrees above the horizon. Lower aurora is too faint to see through the haze of the horizon.
[[nodiscard]] bool AuroraVisible(double observerGeomagneticLatitude, const AuroraOval& oval);

} // namespace moa::astro

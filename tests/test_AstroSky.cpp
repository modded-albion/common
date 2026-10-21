/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>
#include <cstdint>

#include <algorithm>
#include <iterator>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "astro/Moon.h"
#include "astro/Sky.h"
#include "astro/StarTable.h"

using namespace moa::astro;

namespace
{

constexpr double k_Degrees = 180.0 / std::numbers::pi;
constexpr Observer k_London {.latitude = 51.5, .longitude = -0.13};

/// Return the altitude and the azimuth of a direction in the local axes (north, east, up).
Horizontal FromLocal(Vec3 local)
{
	auto azimuth = std::atan2(local.y, local.x) * k_Degrees;
	if (azimuth < 0.0)
	{
		azimuth += 360.0;
	}
	return {.altitude = std::asin(local.z) * k_Degrees, .azimuth = azimuth};
}

/// Check that the rows have a length of 1 and are at 90 degrees. `handedness` is +1 for a turn, and -1 for a turn
/// with a mirror (the local axes north, east, up are a left-handed frame).
void ExpectOrthonormal(const Matrix3& m, double handedness)
{
	for (size_t a = 0; a < 3; ++a)
	{
		for (size_t b = 0; b < 3; ++b)
		{
			EXPECT_NEAR(Dot(m.rows[a], m.rows[b]), a == b ? 1.0 : 0.0, 1e-12);
		}
	}
	const auto z = Cross(m.rows[0], m.rows[1]);
	EXPECT_NEAR(Dot(z, m.rows[2]), handedness, 1e-12);
}

} // namespace

TEST(AstroSky, PrecessesMeeusExample21b)
{
	// Theta Persei: J2000 position (with its proper motion to the date), at 2028 November 13.19 TD.
	const auto j2000 = UnitVector({.rightAscension = 41.054063, .declination = 49.227750});
	const auto date = ToEquatorial(PrecessionFromJ2000(2462088.69).Apply(j2000));
	EXPECT_NEAR(date.rightAscension, 41.547214, 2e-5);
	EXPECT_NEAR(date.declination, 49.348483, 2e-5);
}

TEST(AstroSky, GivesTheSameDirectionAsToHorizontal)
{
	// Venus in Meeus example 13.b, as a J2000 catalogue position: the inverse precession of its position of the date.
	const Equatorial venus {.rightAscension = (23.0 + 9.0 / 60.0 + 16.641 / 3600.0) * 15.0,
	                        .declination = -(6.0 + 43.0 / 60.0 + 11.61 / 3600.0)};
	const Observer washington {.latitude = 38.0 + 55.0 / 60.0 + 17.0 / 3600.0,
	                           .longitude = -(77.0 + 3.0 / 60.0 + 56.0 / 3600.0)};
	const double julianDay = 2446896.30625;
	const auto catalogue = PrecessionFromJ2000(julianDay).Transposed().Apply(UnitVector(venus));
	const auto local = FromLocal(CatalogueToLocal(julianDay, washington).Apply(catalogue));
	const auto expected = ToHorizontal(venus, washington, julianDay);
	EXPECT_NEAR(local.altitude, expected.altitude, 1e-6);
	EXPECT_NEAR(local.azimuth, expected.azimuth, 1e-6);
	EXPECT_NEAR(local.altitude, 15.1249, 0.01);
}

TEST(AstroSky, PutsPolarisAtTheAltitudeOfTheLatitude)
{
	// Polaris (J2000) is 0.74 degrees from the pole of the sky, so its altitude is the latitude +- 0.74 degrees,
	// and it is near the north.
	const auto polaris = UnitVector({.rightAscension = 37.95456, .declination = 89.26411});
	for (const double hours : {0.0, 6.0, 12.0, 18.0})
	{
		const auto julianDay = 2461333.5 + hours / 24.0; // 2026 October 20
		const auto local = FromLocal(CatalogueToLocal(julianDay, k_London).Apply(polaris));
		EXPECT_NEAR(local.altitude, k_London.latitude, 0.8);
		EXPECT_TRUE(local.azimuth < 1.5 || local.azimuth > 358.5) << local.azimuth;
	}
}

TEST(AstroSky, MakesOrthonormalMatrices)
{
	ExpectOrthonormal(PrecessionFromJ2000(2461333.5), 1.0);
	ExpectOrthonormal(CatalogueToLocal(2461333.5, k_London), -1.0);
	ExpectOrthonormal(LocalToGeomagnetic(k_London), -1.0);
}

TEST(AstroSky, GivesTheGeomagneticLatitude)
{
	EXPECT_NEAR(GeomagneticLatitude(k_London), 53.4, 0.2);
	EXPECT_NEAR(GeomagneticLatitude(k_GeomagneticNorthPole), 90.0, 1e-9);
	// Tromso is under the quiet aurora.
	EXPECT_NEAR(GeomagneticLatitude({.latitude = 69.65, .longitude = 18.96}), 67.5, 0.2);
}

TEST(AstroSky, TurnsTheUpDirectionToTheGeomagneticLatitude)
{
	const auto up = LocalToGeomagnetic(k_London).Apply({0.0, 0.0, 1.0});
	EXPECT_NEAR(std::asin(up.z) * k_Degrees, GeomagneticLatitude(k_London), 1e-9);
	// The north on the horizon at London points nearly to the geomagnetic pole.
	const auto north = LocalToGeomagnetic(k_London).Apply({1.0, 0.0, 0.0});
	EXPECT_GT(north.z, 0.5);
}

TEST(AstroSky, MovesTheOvalTowardTheEquatorWithKp)
{
	EXPECT_DOUBLE_EQ(AuroraOvalForKp(0.0).equatorwardEdge, 68.0);
	EXPECT_DOUBLE_EQ(AuroraOvalForKp(5.0).equatorwardEdge, 58.0);
	EXPECT_DOUBLE_EQ(AuroraOvalForKp(5.0).width, 6.5);
	EXPECT_DOUBLE_EQ(AuroraOvalForKp(20.0).equatorwardEdge, AuroraOvalForKp(9.0).equatorwardEdge);
	EXPECT_DOUBLE_EQ(AuroraOvalForKp(-1.0).equatorwardEdge, 68.0);
}

TEST(AstroSky, ShowsTheAuroraAtLondonOnlyForAStrongStorm)
{
	const auto london = GeomagneticLatitude(k_London);
	EXPECT_FALSE(AuroraVisible(london, AuroraOvalForKp(3.0)));
	EXPECT_FALSE(AuroraVisible(london, AuroraOvalForKp(4.0)));
	EXPECT_TRUE(AuroraVisible(london, AuroraOvalForKp(5.0)));
	EXPECT_TRUE(AuroraVisible(london, AuroraOvalForKp(7.0)));
	const auto tromso = GeomagneticLatitude({.latitude = 69.65, .longitude = 18.96});
	EXPECT_TRUE(AuroraVisible(tromso, AuroraOvalForKp(1.0)));
	EXPECT_TRUE(AuroraVisible(london, AuroraOvalOverhead(london)));
}

TEST(AstroSky, GivesTheAltitudeOfAnAuroraPoint)
{
	EXPECT_DOUBLE_EQ(AltitudeOfAuroraPoint(0.0, 100.0), 90.0);
	// A point at 300 km is on the horizon at an arc of acos(R / (R + 300)), approximately 17.4 degrees.
	const auto arc = std::acos(k_MeanEarthRadiusKm / (k_MeanEarthRadiusKm + 300.0)) * k_Degrees;
	EXPECT_NEAR(AltitudeOfAuroraPoint(arc, 300.0), 0.0, 1e-9);
	EXPECT_GT(AltitudeOfAuroraPoint(1.0, 100.0), 40.0);
}

namespace
{

void PutLittle(std::vector<std::byte>& bytes, const void* value, size_t size)
{
	const auto* raw = static_cast<const std::byte*>(value);
	bytes.insert(bytes.end(), raw, raw + size);
}

std::vector<std::byte> MakeTable(uint32_t count, uint32_t version = 1)
{
	std::vector<std::byte> bytes;
	for (const char c : std::string_view("MOASTARS"))
	{
		bytes.push_back(static_cast<std::byte>(c));
	}
	PutLittle(bytes, &version, 4);
	PutLittle(bytes, &count, 4);
	for (uint32_t index = 0; index < count; ++index)
	{
		const float rightAscension = 101.287f + static_cast<float>(index);
		const float declination = -16.716f;
		const int16_t magnitude = -146;
		const int16_t colour = index == 0 ? int16_t {0} : std::numeric_limits<int16_t>::min();
		PutLittle(bytes, &rightAscension, 4);
		PutLittle(bytes, &declination, 4);
		PutLittle(bytes, &magnitude, 2);
		PutLittle(bytes, &colour, 2);
	}
	return bytes;
}

} // namespace

TEST(AstroSky, ReadsAStarTable)
{
	const auto stars = ReadStarTable(MakeTable(2));
	ASSERT_TRUE(stars.has_value());
	ASSERT_EQ(stars->size(), 2u);
	EXPECT_FLOAT_EQ(static_cast<float>((*stars)[0].position.rightAscension), 101.287f);
	EXPECT_FLOAT_EQ((*stars)[0].magnitude, -1.46f);
	EXPECT_EQ((*stars)[0].colourIndex, 0.0f);
	EXPECT_FALSE((*stars)[1].colourIndex.has_value());
}

TEST(AstroSky, RefusesABadStarTable)
{
	auto table = MakeTable(2);
	EXPECT_FALSE(ReadStarTable(std::span(table).first(table.size() - 1)).has_value());
	EXPECT_FALSE(ReadStarTable(MakeTable(1, 2)).has_value());
	table[0] = std::byte {'X'};
	EXPECT_FALSE(ReadStarTable(table).has_value());
	EXPECT_FALSE(ReadStarTable({}).has_value());
}

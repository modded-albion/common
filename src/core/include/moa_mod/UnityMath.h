/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <numbers>

namespace moa_mod
{

/// A point or a direction in the world of the game: x to the east, y up, z to the north (Unity axes). The layout
/// is the same as a Unity `Vector3`, so the mod can give a pointer to it to a Unity method.
struct WorldPoint
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

/// Four numbers with the layout of a Unity `Vector4` or `Color`.
struct Vector4
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 0.0f;

	friend bool operator==(const Vector4&, const Vector4&) = default;
};

/// Two numbers with the layout of a Unity `Vector2`.
struct Vector2
{
	float x = 0.0f;
	float y = 0.0f;
};

/// A rotation with the same layout and rules as a Unity `Quaternion`.
struct Quaternion
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;
};

constexpr float k_DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

[[nodiscard]] constexpr WorldPoint operator+(WorldPoint a, WorldPoint b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr WorldPoint operator-(WorldPoint a, WorldPoint b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] constexpr WorldPoint operator*(WorldPoint a, float scale)
{
	return {a.x * scale, a.y * scale, a.z * scale};
}

[[nodiscard]] constexpr float Dot(WorldPoint a, WorldPoint b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// The cross product with the same formula as Unity's `Vector3.Cross`.
[[nodiscard]] constexpr WorldPoint Cross(WorldPoint a, WorldPoint b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline float Length(WorldPoint a)
{
	return std::sqrt(Dot(a, a));
}

/// Return the direction of `a` with a length of 1, or a zero vector if `a` is too short.
[[nodiscard]] inline WorldPoint Normalize(WorldPoint a)
{
	const auto length = Length(a);
	return length > 1e-6f ? a * (1.0f / length) : WorldPoint {};
}

/// Return an angle in the range -180 to 180 degrees.
[[nodiscard]] inline float WrapDegrees(float degrees)
{
	auto value = std::fmod(degrees + 180.0f, 360.0f);
	if (value < 0.0f)
	{
		value += 360.0f;
	}
	return value - 180.0f;
}

/// Return the rotation of `degrees` about the y axis, as Unity's `Quaternion.Euler(0, degrees, 0)`.
[[nodiscard]] inline Quaternion YawRotation(float degrees)
{
	const auto half = degrees * k_DegreesToRadians * 0.5f;
	return {.x = 0.0f, .y = std::sin(half), .z = 0.0f, .w = std::cos(half)};
}

/// Return the rotation of `degrees` about the x axis, as Unity's `Quaternion.Euler(degrees, 0, 0)`. A positive angle
/// turns the view of a camera down.
[[nodiscard]] inline Quaternion PitchRotation(float degrees)
{
	const auto half = degrees * k_DegreesToRadians * 0.5f;
	return {.x = std::sin(half), .y = 0.0f, .z = 0.0f, .w = std::cos(half)};
}

/// Return the rotation that cancels a rotation with a length of 1, as Unity's `Quaternion.Inverse`.
[[nodiscard]] constexpr Quaternion Inverse(Quaternion q)
{
	return {.x = -q.x, .y = -q.y, .z = -q.z, .w = q.w};
}

/// Return the rotation that applies `b` first and then `a`, as Unity's `a * b`.
[[nodiscard]] constexpr Quaternion Multiply(Quaternion a, Quaternion b)
{
	return {
	    .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
	    .y = a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
	    .z = a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
	    .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	};
}

/// Return the roll of a rotation, degrees: the turn about the view direction, as the z part of Unity's
/// `eulerAngles` (from -180 to 180). A rotation that is a yaw and a pitch only has a roll of zero.
[[nodiscard]] inline float RollDegrees(Quaternion q)
{
	return std::atan2(2.0f * (q.x * q.y + q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z)) / k_DegreesToRadians;
}

/// Turn a direction by a rotation, as Unity's `rotation * vector`.
[[nodiscard]] constexpr WorldPoint Rotate(Quaternion q, WorldPoint v)
{
	const WorldPoint axis {q.x, q.y, q.z};
	const auto t = Cross(axis, v) * 2.0f;
	return v + t * q.w + Cross(axis, t);
}

/// Return the rotation that turns the z axis to `forward` and the y axis as near to `up` as possible, as
/// Unity's `Quaternion.LookRotation`. If `up` is parallel to `forward`, the function uses the z or x axis as `up`.
[[nodiscard]] inline Quaternion LookRotation(WorldPoint forward, WorldPoint up)
{
	const auto z = Normalize(forward);
	auto x = Normalize(Cross(up, z));
	if (Length(x) < 0.5f)
	{
		x = Normalize(Cross(std::abs(z.z) < 0.9f ? WorldPoint {0.0f, 0.0f, 1.0f} : WorldPoint {1.0f, 0.0f, 0.0f}, z));
	}
	const auto y = Cross(z, x);
	// The columns of the rotation matrix are x, y and z. Change the matrix to a quaternion.
	const auto trace = x.x + y.y + z.z;
	Quaternion q;
	if (trace > 0.0f)
	{
		const auto s = std::sqrt(trace + 1.0f) * 2.0f;
		q = {.x = (y.z - z.y) / s, .y = (z.x - x.z) / s, .z = (x.y - y.x) / s, .w = 0.25f * s};
	}
	else if (x.x > y.y && x.x > z.z)
	{
		const auto s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
		q = {.x = 0.25f * s, .y = (y.x + x.y) / s, .z = (z.x + x.z) / s, .w = (y.z - z.y) / s};
	}
	else if (y.y > z.z)
	{
		const auto s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
		q = {.x = (y.x + x.y) / s, .y = 0.25f * s, .z = (z.y + y.z) / s, .w = (z.x - x.z) / s};
	}
	else
	{
		const auto s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
		q = {.x = (z.x + x.z) / s, .y = (z.y + y.z) / s, .z = 0.25f * s, .w = (x.y - y.x) / s};
	}
	return q;
}

} // namespace moa_mod

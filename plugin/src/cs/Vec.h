// GothicCS - minimal 3D vector for the engine-independent CS code.
// CS space: Half-Life units (1 unit = 1 inch = 2.54 cm), Z-up, right-handed.
#pragma once
#include <cmath>

namespace cs
{
	struct Vec3
	{
		float x = 0, y = 0, z = 0;
		Vec3() = default;
		Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		Vec3 operator-() const { return { -x, -y, -z }; }
		Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
		Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
		float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
		float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
	};

	inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3 Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
	inline float Length2D(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
	inline Vec3 Normalized(const Vec3& v) { float l = Length(v); return l > 1e-12f ? v * (1.0f / l) : Vec3(); }

	constexpr float kPi = 3.14159265358979f;
	constexpr float kDegToRad = kPi / 180.0f;

	// Forward/right/up from pitch (positive = look down, like CS) and yaw (degrees).
	inline void AngleVectors(float pitchDeg, float yawDeg, Vec3* forward, Vec3* right, Vec3* up)
	{
		const float sp = std::sin(pitchDeg * kDegToRad), cp = std::cos(pitchDeg * kDegToRad);
		const float sy = std::sin(yawDeg * kDegToRad), cy = std::cos(yawDeg * kDegToRad);
		if (forward) *forward = { cp * cy, cp * sy, -sp };
		if (right) *right = { sy, -cy, 0 };
		if (up) *up = { sp * cy, sp * sy, cp };
	}
}

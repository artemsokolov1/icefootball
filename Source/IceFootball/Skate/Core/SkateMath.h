// Ice skating prototype - engine-independent math helpers.
// The skating/contact core uses these instead of FVector so it can be compiled and
// tested outside Unreal (see Tools/SkateSim). Axes match Unreal world space:
// X = forward, Y = right, Z = up, units = centimetres, seconds.
#pragma once

#include <cmath>

namespace SkateMath
{
	constexpr float Pi = 3.14159265358979f;
	constexpr float DegToRad = Pi / 180.f;
	constexpr float RadToDeg = 180.f / Pi;
	constexpr float SmallNumber = 1.e-4f;

	inline float Clamp(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	inline float Clamp01(float V) { return Clamp(V, 0.f, 1.f); }
	inline float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	inline float Min(float A, float B) { return A < B ? A : B; }
	inline float Max(float A, float B) { return A > B ? A : B; }
	inline float Abs(float A) { return A < 0.f ? -A : A; }
	inline float Sign(float A) { return A < 0.f ? -1.f : 1.f; }
	inline float SmoothStep01(float T) { T = Clamp01(T); return T * T * (3.f - 2.f * T); }

	// Wraps an angle to (-Pi, Pi].
	inline float WrapAngle(float A)
	{
		while (A > Pi) { A -= 2.f * Pi; }
		while (A <= -Pi) { A += 2.f * Pi; }
		return A;
	}

	// Exact fraction of a value removed by exponential decay with the given rate over Dt.
	// Frame-rate independent: applying it N times with Dt/N gives the same result.
	inline float DecayAlpha(float Rate, float Dt) { return 1.f - std::exp(-Rate * Dt); }
}

struct FSkateVec2
{
	float X = 0.f;
	float Y = 0.f;

	FSkateVec2() = default;
	FSkateVec2(float InX, float InY) : X(InX), Y(InY) {}

	static FSkateVec2 FromYaw(float YawRad) { return FSkateVec2(std::cos(YawRad), std::sin(YawRad)); }

	FSkateVec2 operator+(const FSkateVec2& O) const { return FSkateVec2(X + O.X, Y + O.Y); }
	FSkateVec2 operator-(const FSkateVec2& O) const { return FSkateVec2(X - O.X, Y - O.Y); }
	FSkateVec2 operator*(float S) const { return FSkateVec2(X * S, Y * S); }
	FSkateVec2 operator-() const { return FSkateVec2(-X, -Y); }
	FSkateVec2& operator+=(const FSkateVec2& O) { X += O.X; Y += O.Y; return *this; }
	FSkateVec2& operator-=(const FSkateVec2& O) { X -= O.X; Y -= O.Y; return *this; }
	FSkateVec2& operator*=(float S) { X *= S; Y *= S; return *this; }

	float Dot(const FSkateVec2& O) const { return X * O.X + Y * O.Y; }
	// Z of the 3D cross product. Positive when O is clockwise-to-the-right of this vector in UE (X fwd, Y right).
	float Cross(const FSkateVec2& O) const { return X * O.Y - Y * O.X; }
	float SizeSquared() const { return X * X + Y * Y; }
	float Size() const { return std::sqrt(SizeSquared()); }
	float Yaw() const { return std::atan2(Y, X); }

	// Right-hand perpendicular in UE axes: Forward (1,0) -> Right (0,1).
	FSkateVec2 Right() const { return FSkateVec2(-Y, X); }

	FSkateVec2 GetSafeNormal(const FSkateVec2& Fallback = FSkateVec2(1.f, 0.f)) const
	{
		const float S = Size();
		return S > SkateMath::SmallNumber ? FSkateVec2(X / S, Y / S) : Fallback;
	}

	FSkateVec2 Rotated(float AngleRad) const
	{
		const float C = std::cos(AngleRad);
		const float S = std::sin(AngleRad);
		return FSkateVec2(X * C - Y * S, X * S + Y * C);
	}

	// Signed angle that rotates this (unit) vector onto To (unit), in (-Pi, Pi].
	float SignedAngleTo(const FSkateVec2& To) const { return std::atan2(Cross(To), Dot(To)); }

	// Rotates this unit vector towards Target by at most MaxStepRad.
	FSkateVec2 RotatedTowards(const FSkateVec2& Target, float MaxStepRad) const
	{
		const float Angle = SignedAngleTo(Target);
		const float Step = SkateMath::Clamp(Angle, -MaxStepRad, MaxStepRad);
		return Rotated(Step);
	}
};

struct FSkateVec3
{
	float X = 0.f;
	float Y = 0.f;
	float Z = 0.f;

	FSkateVec3() = default;
	FSkateVec3(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}
	FSkateVec3(const FSkateVec2& V, float InZ) : X(V.X), Y(V.Y), Z(InZ) {}

	FSkateVec2 XY() const { return FSkateVec2(X, Y); }
	float Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	FSkateVec3 operator-(const FSkateVec3& O) const { return FSkateVec3(X - O.X, Y - O.Y, Z - O.Z); }
	FSkateVec3 operator+(const FSkateVec3& O) const { return FSkateVec3(X + O.X, Y + O.Y, Z + O.Z); }
	FSkateVec3 operator*(float S) const { return FSkateVec3(X * S, Y * S, Z * S); }
};

#include "SkateInput.h"

namespace SkateInput
{
	FSkateStickResult ShapeStick(float RawX, float RawY, const FSkateInputTuning& Tuning)
	{
		FSkateStickResult Result;
		const float RawMag = std::sqrt(RawX * RawX + RawY * RawY);
		const float Inner = SkateMath::Clamp(Tuning.StickDeadZoneInner, 0.f, 0.9f);
		const float Outer = SkateMath::Max(Tuning.StickDeadZoneOuter, Inner + 0.05f);
		if (RawMag <= Inner || RawMag < SkateMath::SmallNumber)
		{
			return Result;
		}
		Result.LinearMagnitude = SkateMath::Clamp01((RawMag - Inner) / (Outer - Inner));
		Result.Magnitude = std::pow(Result.LinearMagnitude, SkateMath::Max(Tuning.StickResponseExponent, 0.1f));
		// Camera-space direction: X = right, Y = up. Converted to world in ToWorld().
		Result.Direction = FSkateVec2(RawX / RawMag, RawY / RawMag);
		return Result;
	}

	FSkateStickResult ToWorld(const FSkateStickResult& CameraSpace, float CameraYawRad)
	{
		FSkateStickResult Result = CameraSpace;
		if (CameraSpace.Magnitude <= 0.f)
		{
			Result.Direction = FSkateVec2(0.f, 0.f);
			return Result;
		}
		// Camera forward projected on the ice (pitch is irrelevant for a fixed-yaw camera).
		const FSkateVec2 Forward = FSkateVec2::FromYaw(CameraYawRad);
		const FSkateVec2 Right = Forward.Right();
		Result.Direction = (Forward * CameraSpace.Direction.Y + Right * CameraSpace.Direction.X).GetSafeNormal(Forward);
		return Result;
	}

	FSkateStickResult ShapeStickWorld(float RawX, float RawY, float CameraYawRad, const FSkateInputTuning& Tuning)
	{
		return ToWorld(ShapeStick(RawX, RawY, Tuning), CameraYawRad);
	}

	float ShapeTrigger(float Raw, float DeadZone)
	{
		const float Dz = SkateMath::Clamp(DeadZone, 0.f, 0.9f);
		return SkateMath::Clamp01((Raw - Dz) / (1.f - Dz));
	}

	float ShapeBrake(float Raw, const FSkateInputTuning& Tuning)
	{
		const float T = ShapeTrigger(Raw, Tuning.TriggerDeadZone);
		return T > 0.f ? std::pow(T, SkateMath::Max(Tuning.BrakeInputExponent, 0.1f)) : 0.f;
	}
}

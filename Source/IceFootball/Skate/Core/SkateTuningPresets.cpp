#include "SkateTuningPresets.h"

namespace SkateTuningPresets
{
	FSkateTuning Make(ESkatePreset Preset)
	{
		FSkateTuning T; // defaults = Balanced

		switch (Preset)
		{
		case ESkatePreset::Responsive:
		{
			// Quicker to top speed, shorter glide, tighter arcs, harder stop.
			FSkateMovementTuning& M = T.Movement;
			M.MaxSpeed = 620.f;
			M.BoostMaxSpeed = 830.f;
			M.ThrustTimeConstant = 0.27f;  // ~0.8 s to 95%
			M.BoostTimeConstant = 0.34f;
			M.GlideFriction = 80.f;
			M.GlideDrag = 0.45f;
			M.LateralGrip = 13.f;
			M.MaxLateralAccel = 1800.f;
			M.CarveEfficiency = 0.94f;
			M.TurnRateLowSpeed = 800.f;
			M.TurnRateHighSpeed = 210.f;
			M.GlideAlignRate = 120.f;
			M.BrakeDecel = 1000.f;
			M.ReverseBrakeDecel = 850.f;
			T.Camera.LookAheadSmoothTime = 0.4f;
			T.Anim.PoseSmoothTime = 0.06f;
			break;
		}
		case ESkatePreset::Inertial:
		{
			// Longer build-up, long glide, wider arcs, longer stop. Still never uncontrolled.
			FSkateMovementTuning& M = T.Movement;
			M.MaxSpeed = 560.f;
			M.BoostMaxSpeed = 780.f;
			M.ThrustTimeConstant = 0.43f;  // ~1.3 s to 95%
			M.BoostTimeConstant = 0.52f;
			M.GlideFriction = 38.f;
			M.GlideDrag = 0.2f;
			M.LateralGrip = 7.5f;
			M.MaxLateralAccel = 1150.f;
			M.CarveEfficiency = 0.9f;
			M.TurnRateLowSpeed = 600.f;
			M.TurnRateHighSpeed = 135.f;
			M.GlideAlignRate = 70.f;
			M.BrakeDecel = 700.f;
			M.ReverseBrakeDecel = 600.f;
			T.Camera.LookAheadSmoothTime = 0.6f;
			T.Anim.PoseSmoothTime = 0.09f;
			break;
		}
		case ESkatePreset::Balanced:
		default:
			break;
		}
		return T;
	}

	const char* Name(ESkatePreset Preset)
	{
		switch (Preset)
		{
		case ESkatePreset::Responsive: return "1 Responsive";
		case ESkatePreset::Balanced: return "2 Balanced";
		case ESkatePreset::Inertial: return "3 Inertial";
		}
		return "?";
	}
}

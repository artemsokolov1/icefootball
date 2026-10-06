// Ice skating prototype - stick / trigger shaping and camera-relative projection.
#pragma once

#include "SkateMath.h"
#include "SkateTuning.h"

struct FSkateStickResult
{
	/** World-space unit direction on the ice plane (zero when the stick is inside the dead zone). */
	FSkateVec2 Direction;
	/** Shaped magnitude 0..1 (radial dead zone remap + response curve). Continuous, no steps. */
	float Magnitude = 0.f;
	/** Magnitude after the dead zone remap, before the response curve (for debug). */
	float LinearMagnitude = 0.f;
};

namespace SkateInput
{
	/**
	 * Radial dead zone with continuous rescale: |raw| <= Inner -> 0, |raw| >= Outer -> 1,
	 * linear in between, direction preserved. No smoothing / no added latency.
	 * RawX = stick right, RawY = stick up.
	 */
	FSkateStickResult ShapeStick(float RawX, float RawY, const FSkateInputTuning& Tuning);

	/** Rotates a shaped stick from camera space (up = camera forward on the ice) into world space. */
	FSkateStickResult ToWorld(const FSkateStickResult& CameraSpace, float CameraYawRad);

	/** Convenience: shape + project in one call. */
	FSkateStickResult ShapeStickWorld(float RawX, float RawY, float CameraYawRad, const FSkateInputTuning& Tuning);

	/** Trigger dead zone remap (0..1). */
	float ShapeTrigger(float Raw, float DeadZone);

	/** Brake trigger: dead zone + curve. */
	float ShapeBrake(float Raw, const FSkateInputTuning& Tuning);
}

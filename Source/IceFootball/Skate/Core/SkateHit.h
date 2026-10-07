// Ice skating prototype - body checks (engine independent).
//
// Two skaters of different teams collide. The one skating into the other (the larger closing speed,
// moving fast enough and roughly towards the other) is the hitter: the victim is shoved along the
// contact direction and stunned for a moment (no stick, no ball control -> a carried ball is knocked
// loose), the hitter loses part of its speed. Contact alone decides, like an arcade NHL check: there is
// no check button.
#pragma once

#include "SkateMath.h"
#include "SkateTuning.h"

struct FSkateHitResult
{
	bool bHit = false;
	/** 0 = skater A delivered the check, 1 = skater B. */
	int Hitter = 0;
	FSkateVec2 HitterVelocity;
	FSkateVec2 VictimVelocity;
};

class FSkateHit
{
public:
	/** Resolves a contact between A and B (positions and velocities before the contact). */
	static FSkateHitResult Resolve(const FSkateHitTuning& Tuning, const FSkateVec2& PosA, const FSkateVec2& VelA, const FSkateVec2& PosB, const FSkateVec2& VelB);
};

#include "SkateHit.h"

#include <cmath>

FSkateHitResult FSkateHit::Resolve(const FSkateHitTuning& Tuning, const FSkateVec2& PosA, const FSkateVec2& VelA, const FSkateVec2& PosB, const FSkateVec2& VelB)
{
	FSkateHitResult Result;
	const FSkateVec2 AtoB = (PosB - PosA).GetSafeNormal(FSkateVec2(1.f, 0.f));
	// Closing speed of each towards the other along the contact line.
	const float ClosingA = VelA.Dot(AtoB);
	const float ClosingB = -VelB.Dot(AtoB);
	if (ClosingA + ClosingB < Tuning.MinClosingSpeed)
	{
		return Result;
	}
	Result.Hitter = ClosingA >= ClosingB ? 0 : 1;
	const FSkateVec2& HitterVel = Result.Hitter == 0 ? VelA : VelB;
	const FSkateVec2& VictimVel = Result.Hitter == 0 ? VelB : VelA;
	const FSkateVec2 ToVictim = Result.Hitter == 0 ? AtoB : AtoB * -1.f;
	const float HitterSpeed = HitterVel.Size();
	// The hitter must be skating at the victim, not just be bumped sideways.
	if (HitterSpeed < Tuning.MinHitterSpeed || HitterVel.Dot(ToVictim) < HitterSpeed * std::cos(Tuning.FrontConeDeg * SkateMath::DegToRad))
	{
		return Result;
	}
	Result.bHit = true;
	Result.VictimVelocity = VictimVel + ToVictim * Tuning.Push;
	Result.HitterVelocity = HitterVel * Tuning.HitterKeepsSpeed;
	return Result;
}

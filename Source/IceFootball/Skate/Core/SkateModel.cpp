#include "SkateModel.h"

namespace SkateModelDetail
{
	// Moves V towards zero by at most Amount (never crosses zero).
	inline float MoveTowardZero(float V, float Amount)
	{
		if (V > 0.f) { return SkateMath::Max(0.f, V - Amount); }
		return SkateMath::Min(0.f, V + Amount);
	}

	// Blade axis (heading or its reverse) closest to the travel direction: skaters may glide backwards.
	inline FSkateVec2 AxisAlignedWith(const FSkateVec2& Heading, const FSkateVec2& TravelDir)
	{
		return Heading.Dot(TravelDir) >= 0.f ? TravelDir : -TravelDir;
	}
}

const char* SkatePhaseName(ESkateMovePhase Phase)
{
	switch (Phase)
	{
	case ESkateMovePhase::Idle: return "Idle";
	case ESkateMovePhase::Push: return "Push";
	case ESkateMovePhase::Glide: return "Glide";
	case ESkateMovePhase::Carve: return "Carve";
	case ESkateMovePhase::Brake: return "Brake";
	case ESkateMovePhase::ReverseStop: return "ReverseStop";
	}
	return "?";
}

float FSkateModel::TurnRateLimit(const FSkateMovementTuning& Tuning, float Speed, float Brake)
{
	const float Blend = SkateMath::SmoothStep01(Speed / SkateMath::Max(Tuning.TurnRateSpeedRef, 1.f));
	const float RateDeg = SkateMath::Lerp(Tuning.TurnRateLowSpeed, Tuning.TurnRateHighSpeed, Blend)
		* SkateMath::Lerp(1.f, Tuning.BrakeTurnRateScale, SkateMath::Clamp01(Brake));
	return RateDeg * SkateMath::DegToRad;
}

void FSkateModel::Reset(FSkateMoveState& State, const FSkateVec2& Heading)
{
	State = FSkateMoveState();
	State.Heading = Heading.GetSafeNormal();
}

void FSkateModel::Step(const FSkateMovementTuning& Tuning, const FSkateMoveInput& Input, float Dt, FSkateMoveState& State)
{
	if (Dt <= 0.f)
	{
		return;
	}

	const float MaxH = SkateMath::Clamp(Tuning.MaxSubstep, 0.0005f, 0.05f);
	int NumSteps = static_cast<int>(std::ceil(Dt / MaxH - 1.e-4f));
	NumSteps = NumSteps < 1 ? 1 : (NumSteps > 256 ? 256 : NumSteps);
	const float H = Dt / static_cast<float>(NumSteps);

	FAccum Acc;
	State.bBrakeReversalFault = false;
	for (int Index = 0; Index < NumSteps; ++Index)
	{
		SubStep(Tuning, Input, H, State, Acc);
	}

	const float InvDt = 1.f / Dt;
	State.PushAmount = Acc.Push / static_cast<float>(NumSteps);
	State.ThrustAccel = Acc.Thrust * InvDt;
	State.BrakeDecel = Acc.Brake * InvDt;
	State.ScrubDecel = Acc.Scrub * InvDt;
	State.LateralAccel = Acc.Lateral * InvDt;
	State.SlipAngleDeg = Acc.Slip / static_cast<float>(NumSteps);

	const float Speed = State.Velocity.Size();
	const float Mag = Input.Direction.SizeSquared() > 0.25f ? SkateMath::Clamp01(Input.Magnitude) : 0.f;
	const float Boost = SkateMath::Clamp01(Input.Boost);
	State.TargetSpeed = Mag * SkateMath::Lerp(Tuning.MaxSpeed, Tuning.BoostMaxSpeed, Boost);
	State.TurnRateLimitDeg = TurnRateLimit(Tuning, Speed, Input.Brake) * SkateMath::RadToDeg;

	if (State.bReverseStop)
	{
		State.Phase = ESkateMovePhase::ReverseStop;
	}
	else if (Input.Brake > 0.1f && Speed > 1.f)
	{
		State.Phase = ESkateMovePhase::Brake;
	}
	else if (State.PushAmount > 0.05f && State.ThrustAccel > 20.f)
	{
		State.Phase = ESkateMovePhase::Push;
	}
	else if (Speed < 1.f && Mag <= 0.f)
	{
		State.Phase = ESkateMovePhase::Idle;
	}
	else if (SkateMath::Abs(State.LateralAccel) > 250.f)
	{
		State.Phase = ESkateMovePhase::Carve;
	}
	else
	{
		State.Phase = ESkateMovePhase::Glide;
	}
}

void FSkateModel::SubStep(const FSkateMovementTuning& Tuning, const FSkateMoveInput& Input, float H, FSkateMoveState& State, FAccum& Acc)
{
	using namespace SkateModelDetail;

	FSkateVec2 Vel = State.Velocity;
	const float Speed = Vel.Size();
	const FSkateVec2 TravelDir = Speed > SkateMath::SmallNumber ? Vel * (1.f / Speed) : State.Heading;

	const bool bHasStick = Input.Magnitude > 0.f && Input.Direction.SizeSquared() > 0.25f;
	const FSkateVec2 StickDir = bHasStick ? Input.Direction.GetSafeNormal() : FSkateVec2();
	const float Mag = bHasStick ? SkateMath::Clamp01(Input.Magnitude) : 0.f;
	const float Brake = SkateMath::Clamp01(Input.Brake);
	const float Boost = SkateMath::Clamp01(Input.Boost);

	// ---- 1. Reverse-stop intent (stick against travel at speed). Hysteresis avoids flicker. ----
	const float StickVsTravel = bHasStick ? StickDir.Dot(TravelDir) : 1.f;
	if (!State.bReverseStop)
	{
		State.bReverseStop = bHasStick && Speed > Tuning.ReverseMinSpeed && StickVsTravel < Tuning.ReverseIntentDot;
	}
	else
	{
		const bool bExit = !bHasStick
			|| Speed < Tuning.ReverseMinSpeed * 0.5f
			|| StickVsTravel > Tuning.ReverseIntentDot + 0.3f;
		State.bReverseStop = !bExit;
	}

	// ---- 2. Heading (blades) turns towards the target, rate limited ----
	FSkateVec2 Heading = State.Heading;
	{
		float Rate = TurnRateLimit(Tuning, Speed, Brake);
		FSkateVec2 Target = Heading;
		if (State.bReverseStop)
		{
			// Keep the blades along the travel line; the pose shows the hockey stop.
			Target = AxisAlignedWith(Heading, TravelDir);
			Rate = SkateMath::Min(Rate, Tuning.GlideAlignRate * SkateMath::DegToRad);
		}
		else if (bHasStick)
		{
			Target = StickDir;
		}
		else if (Speed > Tuning.StopSnapSpeed)
		{
			Target = AxisAlignedWith(Heading, TravelDir);
			Rate = SkateMath::Min(Rate, Tuning.GlideAlignRate * SkateMath::DegToRad);
		}
		Heading = Heading.RotatedTowards(Target, Rate * H).GetSafeNormal();
		State.Heading = Heading;
	}

	// ---- 3. Decompose velocity relative to the blades ----
	const FSkateVec2 Right = Heading.Right();
	float VLong = Vel.Dot(Heading);
	float VLat = Vel.Dot(Right);
	const float SpeedBefore = Speed;

	// ---- 4. Lateral grip: remove across-blade speed, redirect most of it along the blade ----
	if (SkateMath::Abs(VLat) > 0.f)
	{
		const float SlipRad = std::atan2(SkateMath::Abs(VLat), SkateMath::Abs(VLong));
		const float Desired = VLat * SkateMath::DecayAlpha(Tuning.LateralGrip, H);
		const float MaxRemove = Tuning.MaxLateralAccel * H;
		const float Removed = SkateMath::Clamp(Desired, -MaxRemove, MaxRemove);
		const float NewLat = VLat - Removed;

		const float SkidAngle = SkateMath::Max(Tuning.SkidSlipAngle, 1.f) * SkateMath::DegToRad;
		const float Efficiency = Tuning.CarveEfficiency * (1.f - SkateMath::SmoothStep01(SlipRad / SkidAngle));
		const float Redirected = (VLat * VLat - NewLat * NewLat) * Efficiency;
		const float LongSign = VLong >= 0.f ? 1.f : -1.f;
		VLong = LongSign * std::sqrt(VLong * VLong + SkateMath::Max(Redirected, 0.f));
		VLat = NewLat;

		Acc.Lateral += -Removed;
		Acc.Slip += SlipRad * SkateMath::RadToDeg;
		const float SpeedAfterGrip = std::sqrt(VLong * VLong + VLat * VLat);
		Acc.Scrub += SkateMath::Max(0.f, SpeedBefore - SpeedAfterGrip);
	}

	// ---- 5. Thrust along the blade / glide friction ----
	float Pushing = 0.f;
	bool bOverspeed = false;
	if (bHasStick && !State.bReverseStop)
	{
		const float Align = Heading.Dot(StickDir);
		const float MinDot = SkateMath::Min(Tuning.ThrustAlignMinDot, 0.95f);
		const float AlignScale = SkateMath::Clamp01((Align - MinDot) / (1.f - MinDot));
		const float VMax = SkateMath::Lerp(Tuning.MaxSpeed, Tuning.BoostMaxSpeed, Boost);
		const float Tau = SkateMath::Max(SkateMath::Lerp(Tuning.ThrustTimeConstant, Tuning.BoostTimeConstant, Boost), 0.01f);
		const float VTarget = VMax * Mag;
		if (VLong < VTarget)
		{
			Pushing = AlignScale * (1.f - Brake);
			const float Dv = (VTarget - VLong) * SkateMath::DecayAlpha(1.f / Tau, H) * Pushing;
			VLong += Dv;
			Acc.Thrust += Dv;
		}
		else
		{
			bOverspeed = VLong > VTarget + 1.f;
		}
	}
	{
		float FrictionDecel = (Tuning.GlideFriction + Tuning.GlideDrag * SkateMath::Abs(VLong)) * (1.f - Pushing);
		if (bOverspeed)
		{
			FrictionDecel = SkateMath::Max(FrictionDecel, Tuning.OverspeedDecel);
		}
		VLong = MoveTowardZero(VLong, FrictionDecel * H);
	}
	Acc.Push += Pushing;

	Vel = Heading * VLong + Right * VLat;

	// ---- 6. Braking: reduces speed magnitude, never reverses direction ----
	const float BrakeDecel = Tuning.BrakeDecel * Brake + (State.bReverseStop ? Tuning.ReverseBrakeDecel * Mag : 0.f);
	if (BrakeDecel > 0.f)
	{
		const FSkateVec2 Before = Vel;
		const float S = Vel.Size();
		if (S > SkateMath::SmallNumber)
		{
			const float NewS = SkateMath::Max(0.f, S - BrakeDecel * H);
			Vel *= NewS / S;
			Acc.Brake += S - NewS;
		}
		if (Vel.Dot(Before) < 0.f)
		{
			State.bBrakeReversalFault = true;
		}
	}

	// ---- 7. Full stop near zero (only when not pushing) ----
	if (Pushing <= 0.f && Vel.Size() < Tuning.StopSnapSpeed)
	{
		Vel = FSkateVec2();
	}

	State.Velocity = Vel;
}

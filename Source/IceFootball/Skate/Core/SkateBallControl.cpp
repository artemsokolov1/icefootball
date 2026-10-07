#include "SkateBallControl.h"

namespace SkateBallControlDetail
{
	// The working foot sits slightly in front of the skater centre.
	constexpr float FootForward = 15.f;
	// Ball lower than this above the ice can hit the legs/body.
	constexpr float BodyBlockMaxHeight = 120.f;
}

const char* SkateContactReasonName(ESkateContactReason Reason)
{
	switch (Reason)
	{
	case ESkateContactReason::NoBall: return "No ball";
	case ESkateContactReason::Disabled: return "Ball interaction OFF";
	case ESkateContactReason::Reachable: return "Reachable";
	case ESkateContactReason::ActionReachOnly: return "Reachable for A/X only";
	case ESkateContactReason::TooFar: return "Too far";
	case ESkateContactReason::OutsideAngle: return "Beside/behind skater";
	case ESkateContactReason::Airborne: return "Ball airborne";
	case ESkateContactReason::BlockedByBoard: return "Blocked (wall/obstacle)";
	case ESkateContactReason::TooFast: return "Ball too fast";
	}
	return "?";
}

const char* SkatePossessionLossName(ESkatePossessionLoss Loss)
{
	switch (Loss)
	{
	case ESkatePossessionLoss::None: return "-";
	case ESkatePossessionLoss::Kick: return "Kick";
	case ESkatePossessionLoss::Push: return "Push";
	case ESkatePossessionLoss::Blocked: return "Blocked (wall/obstacle)";
	case ESkatePossessionLoss::Airborne: return "Airborne";
	case ESkatePossessionLoss::Disabled: return "Disabled";
	}
	return "?";
}

const char* SkateImpulseKindName(ESkateImpulseKind Kind)
{
	switch (Kind)
	{
	case ESkateImpulseKind::None: return "None";
	case ESkateImpulseKind::Touch: return "Touch";
	case ESkateImpulseKind::Push: return "Push";
	case ESkateImpulseKind::Kick: return "Kick";
	case ESkateImpulseKind::BodyBlock: return "BodyBlock";
	}
	return "?";
}

void FSkateBallControl::Reset(FSkateBallControlState& State)
{
	State = FSkateBallControlState();
}

float FSkateBallControl::PassChargeFraction(const FSkateBallControlTuning& Tuning, const FSkateBallControlState& State)
{
	return State.bChargingPass ? SkateMath::Clamp01(State.PassChargeTime / SkateMath::Max(Tuning.PassMaxChargeTime, 0.01f)) : 0.f;
}

float FSkateBallControl::ChargeFraction(const FSkateBallControlTuning& Tuning, const FSkateBallControlState& State)
{
	return State.bCharging ? SkateMath::Clamp01(State.ChargeTime / SkateMath::Max(Tuning.KickMaxChargeTime, 0.01f)) : 0.f;
}

FSkateVec2 FSkateBallControl::ContactNormal(const FSkateContactQuery& Query)
{
	const FSkateVec2 Foot = Query.SkaterPos + Query.Heading * SkateBallControlDetail::FootForward;
	return (Query.BallPos.XY() - Foot).GetSafeNormal(Query.Heading);
}

FSkateVec2 FSkateBallControl::LimitDeviation(const FSkateVec2& From, const FSkateVec2& To, float MaxDeg)
{
	return From.RotatedTowards(To, SkateMath::Max(MaxDeg, 0.f) * SkateMath::DegToRad).GetSafeNormal(From);
}

FSkateVec2 FSkateBallControl::DesiredDirection(const FSkateContactQuery& Query, float MinStick)
{
	if (Query.StickMag >= MinStick && Query.StickDir.SizeSquared() > 0.25f)
	{
		return Query.StickDir.GetSafeNormal();
	}
	if (Query.SkaterVel.Size() > 50.f)
	{
		return Query.SkaterVel.GetSafeNormal();
	}
	return Query.Heading;
}

FSkateContactReport FSkateBallControl::Evaluate(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query)
{
	FSkateContactReport Report;
	if (!Query.bHasBall)
	{
		Report.Reason = ESkateContactReason::NoBall;
		return Report;
	}

	const FSkateVec2 ToBall = Query.BallPos.XY() - Query.SkaterPos;
	Report.Distance = ToBall.Size();
	const FSkateVec2 ToBallDir = ToBall.GetSafeNormal(Query.Heading);
	Report.AngleFromHeadingDeg = std::acos(SkateMath::Clamp(ToBallDir.Dot(Query.Heading), -1.f, 1.f)) * SkateMath::RadToDeg;
	const FSkateVec2 ReachCentre = Query.SkaterPos + Query.Heading * Tuning.ReachForward;
	Report.DistanceToReachCentre = (Query.BallPos.XY() - ReachCentre).Size();
	Report.BallHeight = Query.BallPos.Z - Query.BallRadius - Query.IceZ;
	const FSkateVec2 BallVel2 = Query.BallVel.XY();
	Report.RelativeSpeed = (BallVel2 - Query.SkaterVel).Size();
	Report.ClosingSpeed = (Query.SkaterVel - BallVel2).Dot(ToBallDir);

	const bool bAngleOk = Report.AngleFromHeadingDeg <= Tuning.ReachHalfAngle;
	const bool bInTouchZone = bAngleOk && Report.DistanceToReachCentre <= Tuning.ReachRadius;
	const bool bInActionZone = bAngleOk && Report.DistanceToReachCentre <= Tuning.ReachRadius + Tuning.ActionReachBonus;

	if (!Query.bInteractionEnabled)
	{
		Report.Reason = ESkateContactReason::Disabled;
	}
	else if (Report.BallHeight > Tuning.MaxTouchHeight)
	{
		Report.Reason = ESkateContactReason::Airborne;
	}
	else if (!bInActionZone)
	{
		const float ZoneOuter = Tuning.ReachForward + Tuning.ReachRadius + Tuning.ActionReachBonus;
		Report.Reason = (!bAngleOk && Report.Distance <= ZoneOuter) ? ESkateContactReason::OutsideAngle : ESkateContactReason::TooFar;
	}
	else if (!Query.bLineOfSightClear)
	{
		Report.Reason = ESkateContactReason::BlockedByBoard;
	}
	else if (Report.RelativeSpeed > Tuning.MaxControllableRelSpeed)
	{
		Report.Reason = ESkateContactReason::TooFast;
	}
	else
	{
		Report.Reason = bInTouchZone ? ESkateContactReason::Reachable : ESkateContactReason::ActionReachOnly;
	}

	Report.bInTrapZone = Report.Distance <= Tuning.Possession.TrapDistance
		&& Report.AngleFromHeadingDeg <= Tuning.Possession.TrapHalfAngle
		&& Report.BallHeight <= Tuning.MaxTouchHeight
		&& Query.bLineOfSightClear;

	Report.bHasDribbleIntent = Query.StickMag >= Tuning.DribbleMinStick || Query.SkaterVel.Size() >= Tuning.DribbleMinSpeedNoStick;
	Report.bTouchAllowed = Report.Reason == ESkateContactReason::Reachable
		&& Report.bHasDribbleIntent
		&& Report.ClosingSpeed > Tuning.DribbleMinClosingSpeed;
	return Report;
}

void FSkateBallControl::ReleasePossession(FSkateBallControlState& State, ESkatePossessionLoss Reason)
{
	FSkatePossessionState& Poss = State.Possession;
	if (!Poss.bPossessed)
	{
		return;
	}
	Poss.bPossessed = false;
	Poss.LastLoss = Reason;
	Poss.TimeSinceLost = 0.f;
	Poss.BlockedTime = 0.f;
	Poss.bHasPrevTarget = false;
}

bool FSkateBallControl::CanAcquire(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, const FSkateContactReport& Report, const FSkateBallControlState& State)
{
	const FSkatePossessionTuning& PT = Tuning.Possession;
	return PT.bEnabled
		&& Query.bHasBall
		&& Query.bInteractionEnabled
		&& Report.bInTrapZone
		&& Report.RelativeSpeed <= PT.AcquireMaxRelSpeed
		&& State.TimeSinceAction >= PT.AcquireCooldownAfterAction
		&& State.Possession.TimeSinceLost >= PT.AcquireCooldownAfterLoss;
}

FSkateBallCarry FSkateBallControl::ComputeCarry(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Dt, FSkateBallControlState& State)
{
	const FSkatePossessionTuning& PT = Tuning.Possession;
	FSkatePossessionState& Poss = State.Possession;
	FSkateBallCarry Carry;
	Carry.bActive = true;

	// ---- Dribble rhythm: tap the ball a little ahead, reel it back in ----
	const float SpeedRatio = SkateMath::Clamp01(Query.SkaterVel.Size() / SkateMath::Max(Query.SkaterMaxSpeed, 1.f));
	const float Cadence = SkateMath::Lerp(PT.DribbleCadenceSlow, PT.DribbleCadenceFast, SpeedRatio);
	const float Amplitude = PT.DribbleAmplitude * SpeedRatio;
	Poss.DribblePhase += Dt * Cadence;
	if (Poss.DribblePhase >= 1.f)
	{
		Poss.DribblePhase -= std::floor(Poss.DribblePhase);
		if (Amplitude > 3.f)
		{
			++Poss.TouchPulseCount;
		}
	}
	const float Phase = Poss.DribblePhase;
	const float PushOut = Phase < 0.25f
		? 1.f - (1.f - Phase / 0.25f) * (1.f - Phase / 0.25f)
		: 1.f - SkateMath::SmoothStep01((Phase - 0.25f) / 0.75f);

	// ---- Carry point on an orbit around the skater ----
	const float Distance = SkateMath::Lerp(PT.CarryDistanceSlow, PT.CarryDistanceFast, SpeedRatio) + Amplitude * PushOut;
	const float TargetAngle = Query.Heading.Yaw() + std::atan2(PT.CarrySideOffset, Distance);
	const float MaxSwing = PT.OrbitRate * SkateMath::DegToRad * Dt;
	Poss.OrbitAngle = SkateMath::WrapAngle(Poss.OrbitAngle + SkateMath::Clamp(SkateMath::WrapAngle(TargetAngle - Poss.OrbitAngle), -MaxSwing, MaxSwing));
	const float MinDistance = Tuning.BodyRadius + Query.BallRadius * 0.5f; // never inside the legs
	Poss.OrbitDistance = SkateMath::Max(Poss.OrbitDistance + (Distance - Poss.OrbitDistance) * SkateMath::DecayAlpha(12.f, Dt), MinDistance);
	const FSkateVec2 Target = Query.SkaterPos + FSkateVec2::FromYaw(Poss.OrbitAngle) * Poss.OrbitDistance;

	// ---- Steering: follow the carry point's own motion + bounded correction of the error ----
	const FSkateVec2 TargetVel = Poss.bHasPrevTarget ? (Target - Poss.CarryTarget) * (1.f / SkateMath::Max(Dt, 1.e-4f)) : Query.SkaterVel;
	const FSkateVec2 Error = Target - Query.BallPos.XY();
	Poss.CarryError = Error.Size();
	Poss.BlockedTime = Poss.CarryError > PT.LoseDistance ? Poss.BlockedTime + Dt : 0.f;
	FSkateVec2 Correction = Error * (SkateMath::DecayAlpha(1.f / SkateMath::Max(PT.FollowTime, 0.005f), Dt) / SkateMath::Max(Dt, 1.e-4f));
	const float CorrectionSpeed = Correction.Size();
	if (CorrectionSpeed > PT.MaxCorrectionSpeed)
	{
		Correction *= PT.MaxCorrectionSpeed / CorrectionSpeed;
	}

	Poss.CarryTarget = Target;
	Poss.bHasPrevTarget = true;
	Carry.Target = Target;
	// Vertical velocity stays whatever physics says (the ball rolls on the ice, never pinned).
	Carry.Velocity = FSkateVec3(TargetVel + Correction, Query.BallVel.Z);
	return Carry;
}

FSkateBallImpulse FSkateBallControl::Update(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query,
	const FSkateBallActionInput& Actions, float Dt, FSkateBallControlState& State, FSkateContactReport& OutReport,
	FSkateBallCarry* OutCarry)
{
	if (OutCarry)
	{
		*OutCarry = FSkateBallCarry();
	}
	State.Possession.TimeSinceLost += Dt;
	State.TimeSinceImpulse += Dt;
	State.TimeSinceAction += Dt;
	State.TimeSinceFail += Dt;

	// ---- Command input: charge, buffers ----
	if (Actions.bKickPressed)
	{
		State.bCharging = true;
		State.ChargeTime = 0.f;
	}
	else if (State.bCharging)
	{
		State.ChargeTime = SkateMath::Min(State.ChargeTime + Dt, Tuning.KickMaxChargeTime);
	}
	if (Actions.bKickReleased && State.bCharging)
	{
		State.bCharging = false;
		State.PendingKickPower = SkateMath::Clamp01(State.ChargeTime / SkateMath::Max(Tuning.KickMaxChargeTime, 0.01f));
		State.KickBuffer = Tuning.KickBufferTime;
		State.PushBuffer = -1.f; // the kick supersedes a pending push
	}
	// Pass: hold A to charge, release to pass (a quick tap = PushSpeed).
	if (Actions.bPushPressed)
	{
		State.bChargingPass = true;
		State.PassChargeTime = 0.f;
	}
	else if (State.bChargingPass)
	{
		State.PassChargeTime = SkateMath::Min(State.PassChargeTime + Dt, Tuning.PassMaxChargeTime);
	}
	if (Actions.bPushReleased && State.bChargingPass)
	{
		State.bChargingPass = false;
		if (State.KickBuffer < 0.f)
		{
			State.PendingPassPower = SkateMath::Clamp01(State.PassChargeTime / SkateMath::Max(Tuning.PassMaxChargeTime, 0.01f));
			State.PushBuffer = Tuning.PushBufferTime;
		}
	}

	OutReport = Evaluate(Tuning, Query);
	OutReport.bOnCooldown = State.TimeSinceImpulse < Tuning.TouchCooldown || State.TimeSinceAction < Tuning.NoTouchAfterAction;

	// ---- Possession: lose it when the ball is knocked loose ----
	FSkatePossessionState& Poss = State.Possession;
	const FSkatePossessionTuning& PT = Tuning.Possession;
	if (Poss.bPossessed)
	{
		Poss.TimeHeld += Dt;
		if (!PT.bEnabled || !Query.bHasBall || !Query.bInteractionEnabled)
		{
			ReleasePossession(State, ESkatePossessionLoss::Disabled);
		}
		else if (OutReport.BallHeight > PT.LoseHeight)
		{
			ReleasePossession(State, ESkatePossessionLoss::Airborne);
		}
		else if (Poss.BlockedTime > PT.LoseTime || Poss.CarryError > PT.LoseDistanceInstant)
		{
			ReleasePossession(State, ESkatePossessionLoss::Blocked);
		}
	}

	// A carried ball is at the feet by definition: push / kick are always in reach.
	const bool bActionReach = Poss.bPossessed || OutReport.Reason == ESkateContactReason::Reachable || OutReport.Reason == ESkateContactReason::ActionReachOnly;
	const bool bGapOk = State.TimeSinceImpulse >= MinImpulseGap;
	FSkateBallImpulse Impulse;

	// Consumes a command buffer; records a whiff when it expires.
	auto TickBuffer = [&](float& Buffer, ESkateImpulseKind Kind)
	{
		Buffer -= Dt;
		if (Buffer < 0.f)
		{
			Buffer = -1.f;
			State.LastFailedAction = Kind;
			State.LastFailReason = OutReport.Reason;
			State.TimeSinceFail = 0.f;
		}
	};

	if (State.KickBuffer >= 0.f)
	{
		if (bActionReach && bGapOk)
		{
			Impulse = MakeKick(Tuning, Query, State.PendingKickPower);
			State.KickBuffer = -1.f;
		}
		else
		{
			TickBuffer(State.KickBuffer, ESkateImpulseKind::Kick);
		}
	}
	else if (State.PushBuffer >= 0.f)
	{
		if (bActionReach && bGapOk)
		{
			Impulse = MakePush(Tuning, Query, State.PendingPassPower);
			State.PushBuffer = -1.f;
		}
		else
		{
			TickBuffer(State.PushBuffer, ESkateImpulseKind::Push);
		}
	}

	if (Impulse.IsValid())
	{
		// Push / kick release a carried ball.
		ReleasePossession(State, Impulse.Kind == ESkateImpulseKind::Kick ? ESkatePossessionLoss::Kick : ESkatePossessionLoss::Push);
	}
	else if (Poss.bPossessed || CanAcquire(Tuning, Query, OutReport, State))
	{
		if (!Poss.bPossessed)
		{
			// Trap: start the orbit where the ball actually is, so the first frames are continuous.
			const FSkateVec2 Rel = Query.BallPos.XY() - Query.SkaterPos;
			Poss.bPossessed = true;
			Poss.OrbitAngle = Rel.SizeSquared() > 1.f ? Rel.Yaw() : Query.Heading.Yaw();
			Poss.OrbitDistance = Rel.Size();
			Poss.DribblePhase = 0.5f;
			Poss.BlockedTime = 0.f;
			Poss.TimeHeld = 0.f;
			Poss.bHasPrevTarget = false;
			++Poss.AcquireCount;
		}
		const FSkateBallCarry Carry = ComputeCarry(Tuning, Query, Dt, State);
		if (OutCarry)
		{
			*OutCarry = Carry;
		}
	}
	else if (Query.bHasBall && Query.bInteractionEnabled && bGapOk)
	{
		// Loose ball: dribble touches only in the non-possession mode; the body always blocks.
		if (!PT.bEnabled && OutReport.bTouchAllowed && !OutReport.bOnCooldown)
		{
			Impulse = MakeTouch(Tuning, Query);
		}
		else
		{
			Impulse = MakeBodyBlock(Tuning, Query);
		}
	}

	if (Impulse.IsValid())
	{
		Impulse.DeltaV = Impulse.NewBallVelocity - Query.BallVel;
		State.TimeSinceImpulse = 0.f;
		if (Impulse.Kind == ESkateImpulseKind::Kick || Impulse.Kind == ESkateImpulseKind::Push)
		{
			State.TimeSinceAction = 0.f;
		}
		if (Impulse.Kind == ESkateImpulseKind::BodyBlock)
		{
			// A ball that bounced off the legs is not trapped right away: it must actually bounce.
			Poss.TimeSinceLost = 0.f;
		}
		State.LastKind = Impulse.Kind;
		State.LastDeltaV = Impulse.DeltaV.Size();
		State.LastPower = Impulse.Power;
		++State.ImpulseCount;
	}
	return Impulse;
}

FSkateBallImpulse FSkateBallControl::MakeTouch(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query)
{
	FSkateBallImpulse Impulse;
	Impulse.Kind = ESkateImpulseKind::Touch;
	const FSkateVec2 Normal = ContactNormal(Query);
	const FSkateVec2 Desired = DesiredDirection(Query, Tuning.DribbleMinStick);
	const FSkateVec2 Dir = LimitDeviation(Normal, Desired, Tuning.TouchAssistMaxAngle);

	const float Along = SkateMath::Max(0.f, Query.SkaterVel.Dot(Dir));
	// Quadratic in speed: the ball stays close through the slow / medium range and is released
	// noticeably further only when skating fast.
	const float SpeedRatio = SkateMath::Clamp01(Query.SkaterVel.Size() / SkateMath::Max(Query.SkaterMaxSpeed, 1.f));
	const float Extra = SkateMath::Lerp(Tuning.TouchExtraSpeedSlow, Tuning.TouchExtraSpeedFast, SpeedRatio * SpeedRatio);
	const float Speed = Along * Tuning.TouchCarry + Extra;

	Impulse.Direction = Dir;
	Impulse.NewBallVelocity = FSkateVec3(Dir * Speed, Query.BallVel.Z);
	return Impulse;
}

FSkateBallImpulse FSkateBallControl::MakePush(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Power)
{
	FSkateBallImpulse Impulse;
	Impulse.Kind = ESkateImpulseKind::Push;
	Impulse.Power = SkateMath::Clamp01(Power);
	const FSkateVec2 Normal = ContactNormal(Query);
	const FSkateVec2 Desired = DesiredDirection(Query, 0.2f);
	const FSkateVec2 Dir = LimitDeviation(Normal, Desired, Tuning.PushMaxDeviation);
	const float Speed = SkateMath::Lerp(Tuning.PushSpeed, SkateMath::Max(Tuning.PassMaxSpeed, Tuning.PushSpeed), Impulse.Power)
		+ Tuning.PushCarry * SkateMath::Max(0.f, Query.SkaterVel.Dot(Dir));

	Impulse.Direction = Dir;
	Impulse.NewBallVelocity = FSkateVec3(Dir * Speed, Query.BallVel.Z);
	return Impulse;
}

FSkateBallImpulse FSkateBallControl::MakeKick(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Power)
{
	FSkateBallImpulse Impulse;
	Impulse.Kind = ESkateImpulseKind::Kick;
	Impulse.Power = SkateMath::Clamp01(Power);
	const FSkateVec2 Normal = ContactNormal(Query);
	const FSkateVec2 Desired = DesiredDirection(Query, 0.2f);
	const FSkateVec2 Dir = LimitDeviation(Normal, Desired, Tuning.KickMaxDeviation);
	const float Speed = SkateMath::Lerp(Tuning.KickMinSpeed, Tuning.KickMaxSpeed, Impulse.Power)
		+ Tuning.KickCarry * SkateMath::Max(0.f, Query.SkaterVel.Dot(Dir));
	// Keep any existing upward motion; add a small hop for strong low shots.
	const float Vz = SkateMath::Max(Query.BallVel.Z, Tuning.KickLiftAtFullCharge * Impulse.Power);

	Impulse.Direction = Dir;
	Impulse.NewBallVelocity = FSkateVec3(Dir * Speed, Vz);
	return Impulse;
}

FSkateBallImpulse FSkateBallControl::MakeBodyBlock(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query)
{
	FSkateBallImpulse Impulse;
	const FSkateVec2 ToBall = Query.BallPos.XY() - Query.SkaterPos;
	const float Dist = ToBall.Size();
	const float Height = Query.BallPos.Z - Query.BallRadius - Query.IceZ;
	if (Dist > Tuning.BodyRadius + Query.BallRadius || Height > SkateBallControlDetail::BodyBlockMaxHeight)
	{
		return Impulse;
	}
	const FSkateVec2 Normal = ToBall.GetSafeNormal(Query.Heading);
	const FSkateVec2 BallVel2 = Query.BallVel.XY();
	const float RelNormal = (BallVel2 - Query.SkaterVel).Dot(Normal);
	if (RelNormal >= 0.f)
	{
		return Impulse; // already separating
	}
	Impulse.Kind = ESkateImpulseKind::BodyBlock;
	Impulse.Direction = Normal;
	const FSkateVec2 NewVel = BallVel2 - Normal * (RelNormal * (1.f + Tuning.BodyRestitution));
	Impulse.NewBallVelocity = FSkateVec3(NewVel, Query.BallVel.Z);
	return Impulse;
}

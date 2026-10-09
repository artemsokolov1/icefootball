#include "SkateKeeper.h"

namespace SkateKeeperDetail
{
	// Shots are tracked when they will reach the keeper's line within this time (s).
	constexpr float ThreatHorizon = 1.6f;
	// The ball must come towards the goal at least this fast (cm/s) to count as a shot.
	constexpr float ThreatMinSpeed = 250.f;
	// Depth (cm) behind the keeper's line still covered by the body (a ball that sneaks past slowly).
	constexpr float BodyDepth = 30.f;
	// No save / catch right after the keeper's own throw-out.
	constexpr float IgnoreAfterRelease = 0.6f;
	constexpr float NoSmotherAfterRelease = 1.5f;
	// Hands height while holding the ball, ball distance in front of the body.
	constexpr float HoldHeight = 105.f;
	constexpr float HoldForward = 25.f;
	// Throw-out starts this far in front of the keeper.
	constexpr float ThrowStart = 70.f;
}

const char* SkateKeeperActionName(ESkateKeeperAction Action)
{
	switch (Action)
	{
	case ESkateKeeperAction::None: return "-";
	case ESkateKeeperAction::Parry: return "Parry";
	case ESkateKeeperAction::Catch: return "Catch";
	case ESkateKeeperAction::Release: return "Throw-out";
	}
	return "?";
}

void FSkateKeeper::Reset(FSkateKeeperState& State)
{
	const int Saves = State.Saves;
	const int Catches = State.Catches;
	State = FSkateKeeperState();
	State.Saves = Saves;
	State.Catches = Catches;
}

float FSkateKeeper::DiveAlpha(const FSkateKeeperTuning& Tuning, const FSkateKeeperState& State, float TimeOffset)
{
	const float Time = State.DiveTime + TimeOffset;
	if (State.DiveTime < 0.f || Time < 0.f)
	{
		return 0.f;
	}
	if (Time <= Tuning.DiveTime)
	{
		return SkateMath::SmoothStep01(Time / SkateMath::Max(Tuning.DiveTime, 0.01f));
	}
	// On the ice for the first half of the recovery, then getting up.
	const float U = (Time - Tuning.DiveTime) / SkateMath::Max(Tuning.DiveRecoverTime, 0.01f);
	return U < 0.5f ? 1.f : 1.f - SkateMath::SmoothStep01((U - 0.5f) / 0.5f);
}

float FSkateKeeper::PositionTarget(const FSkateKeeperTuning& Tuning, const FSkateGoalFrame& Goal, const FSkateVec2& BallPos, float ExtraDepth)
{
	const float H = Goal.HalfWidth;
	const float Limit = SkateMath::Max(H - 25.f, 0.f);
	const float Along = Goal.Along(BallPos);
	const float Lateral = Goal.Lateral(BallPos);
	const float L = Tuning.LineOffset + ExtraDepth;
	if (Along < L + 20.f)
	{
		return SkateMath::Clamp(Lateral, -Limit, Limit);
	}
	// The bisector of the angle ball -> posts meets the goal line at P (angle bisector theorem);
	// the keeper stands where the line ball -> P crosses its own line.
	const float ToNeg = (FSkateVec2(Along, Lateral) - FSkateVec2(0.f, -H)).Size();
	const float ToPos = (FSkateVec2(Along, Lateral) - FSkateVec2(0.f, H)).Size();
	const float P = -H + 2.f * H * ToNeg / SkateMath::Max(ToNeg + ToPos, 1.f);
	const float OnLine = Lateral + (P - Lateral) * (Along - L) / Along;
	return SkateMath::Clamp(OnLine, -Limit, Limit);
}

bool FSkateKeeper::InReach(const FSkateKeeperTuning& Tuning, const FSkateKeeperState& State, float BallLateral, float BallBottom, float BallRadius,
	float TimeOffset)
{
	const float Alpha = DiveAlpha(Tuning, State, TimeOffset);
	const float Ext = Tuning.DiveReach * Alpha;
	// Diving stretches one side; the other side is left with the body only.
	float ReachPos = Tuning.StandReach;
	float ReachNeg = Tuning.StandReach;
	if (State.DiveSign > 0.f)
	{
		ReachPos = Tuning.StandReach + Ext;
		ReachNeg = SkateMath::Lerp(Tuning.StandReach, Tuning.BodyHalfWidth, Alpha);
	}
	else if (State.DiveSign < 0.f)
	{
		ReachNeg = Tuning.StandReach + Ext;
		ReachPos = SkateMath::Lerp(Tuning.StandReach, Tuning.BodyHalfWidth, Alpha);
	}
	// Butterfly: low balls are covered wide by the pads without a dive.
	if (State.bButterfly && BallBottom <= Tuning.ButterflyMaxHeight)
	{
		ReachPos = SkateMath::Max(ReachPos, Tuning.ButterflyReach);
		ReachNeg = SkateMath::Max(ReachNeg, Tuning.ButterflyReach);
	}
	if (BallLateral > ReachPos + BallRadius || BallLateral < -ReachNeg - BallRadius)
	{
		return false;
	}
	// A diving keeper is low: high balls go over the stretched body.
	const float MaxHeight = SkateMath::Lerp(Tuning.StandReachHeight, Tuning.DiveLowHeight, Alpha);
	return BallBottom <= MaxHeight;
}

FSkateKeeperOutput FSkateKeeper::Update(const FSkateKeeperTuning& Tuning, const FSkateGoalFrame& Goal, const FSkateKeeperBall& Ball,
	const FSkateVec2& ThrowTarget, float Dt, FSkateKeeperState& State)
{
	using namespace SkateKeeperDetail;
	FSkateKeeperOutput Out;
	if (Dt <= 0.f)
	{
		return Out;
	}
	// Challenge: come out towards a ball in front of the goal; hold the depth while a shot is under way or diving.
	float DepthTarget = 0.f;
	if (Ball.bValid && !State.bHolding)
	{
		const float AlongRaw = Goal.Along(Ball.Pos.XY());
		if (AlongRaw > Tuning.LineOffset && SkateMath::Abs(Goal.Lateral(Ball.Pos.XY())) < Goal.HalfWidth + 150.f && AlongRaw < Tuning.ChallengeFar)
		{
			DepthTarget = Tuning.ChallengeDepth * SkateMath::Clamp01((Tuning.ChallengeFar - AlongRaw) / SkateMath::Max(Tuning.ChallengeFar - Tuning.ChallengeNear, 1.f));
		}
	}
	if (!State.bThreat && State.DiveTime < 0.f && !(State.bHolding && State.bPlayerHeld))
	{
		State.Depth += SkateMath::Clamp(DepthTarget - State.Depth, -Tuning.ChallengeSpeed * Dt, Tuning.ChallengeSpeed * Dt);
	}
	const float L = Tuning.LineOffset + State.Depth;
	const float LateralLimit = SkateMath::Max(Goal.HalfWidth - 10.f, 0.f);
	State.TimeSinceRelease += Dt;
	State.TimeSinceAction += Dt;
	if (State.DiveTime >= 0.f)
	{
		State.DiveTime += Dt;
		if (State.DiveTime > Tuning.DiveTime + Tuning.DiveRecoverTime)
		{
			State.DiveTime = -1.f;
			State.DiveSign = 0.f;
		}
	}

	auto Shuffle = [&](float Target)
	{
		if (State.DiveTime >= 0.f)
		{
			State.LateralVel *= std::exp(-6.f * Dt); // sliding out of the dive
		}
		else
		{
			const float Desired = SkateMath::Clamp((Target - State.Lateral) * 8.f, -Tuning.MaxShuffleSpeed, Tuning.MaxShuffleSpeed);
			const float MaxStep = Tuning.ShuffleAccel * Dt;
			State.LateralVel += SkateMath::Clamp(Desired - State.LateralVel, -MaxStep, MaxStep);
		}
		State.Lateral = SkateMath::Clamp(State.Lateral + State.LateralVel * Dt, -LateralLimit, LateralLimit);
	};
	auto KeeperBase = [&]() { return Goal.ToWorld(L, State.Lateral); };

	// ---- Ball in hands: hold, then roll it out to the controlled skater ----
	if (State.bHolding)
	{
		State.HoldTimer += Dt;
		if (State.bPlayerHeld)
		{
			State.Depth = SkateMath::Clamp(State.Depth + State.HoldMove.X * Dt, 0.f, 500.f);
			State.LateralVel = State.HoldMove.Y;
			State.Lateral = SkateMath::Clamp(State.Lateral + State.LateralVel * Dt, -LateralLimit, LateralLimit);
		}
		else
		{
			Shuffle(0.f);
		}
		if (State.HoldTimer >= Tuning.HoldTime)
		{
			const FSkateVec2 Start = Goal.ToWorld(L + ThrowStart, State.Lateral);
			FSkateVec2 Dir = (ThrowTarget - Start).GetSafeNormal(Goal.Normal);
			if (Dir.Dot(Goal.Normal) < 0.2f)
			{
				Dir = (Dir + Goal.Normal).GetSafeNormal(Goal.Normal); // never back into the goal
			}
			State.bHolding = false;
			State.TimeSinceRelease = 0.f;
			State.LastAction = ESkateKeeperAction::Release;
			State.TimeSinceAction = 0.f;
			Out.Action = ESkateKeeperAction::Release;
			Out.BallPosition = FSkateVec3(Start, Goal.IceZ + Ball.Radius + 0.5f);
			Out.BallVelocity = FSkateVec3(Dir * Tuning.ThrowSpeed, 0.f);
			return Out;
		}
		Out.bHolding = true;
		Out.HoldPosition = FSkateVec3(KeeperBase() + Goal.Normal * HoldForward, Goal.IceZ + HoldHeight);
		return Out;
	}

	if (!Ball.bValid)
	{
		State.bThreat = false;
		State.bButterfly = false;
		Shuffle(0.f);
		return Out;
	}

	const FSkateVec2 BallXY = Ball.Pos.XY();
	const FSkateVec2 VelXY = Ball.Vel.XY();
	const float Along = Goal.Along(BallXY);
	const float Lateral = Goal.Lateral(BallXY);
	const float VAlong = VelXY.Dot(Goal.Normal);
	const float VLat = VelXY.Dot(Goal.Right());
	const float Bottom = Ball.Pos.Z - Ball.Radius - Goal.IceZ;

	// Butterfly: the ball is close in front -> drop and cover low.
	State.bButterfly = State.DiveTime < 0.f && Along > L - BodyDepth && Along - L < Tuning.ButterflyRange
		&& SkateMath::Abs(Lateral - State.Lateral) < Goal.HalfWidth + 100.f;

	// ---- Shot tracking: where and when will the ball cross the keeper's line? ----
	bool bThreatNow = false;
	if (!Ball.bHeldBySkater && VAlong < -ThreatMinSpeed && Along > L - 10.f)
	{
		const float T = SkateMath::Max(Along - L, 0.f) / -VAlong;
		if (T < ThreatHorizon)
		{
			const float PredLat = Lateral + VLat * T;
			float PredBottom = 0.f;
			if (Bottom > 1.f || Ball.Vel.Z > 1.f)
			{
				PredBottom = SkateMath::Max(Bottom + Ball.Vel.Z * T - 0.5f * Tuning.Gravity * T * T, 0.f);
			}
			if (SkateMath::Abs(PredLat) < Goal.HalfWidth + 60.f && PredBottom < Goal.Height + 30.f)
			{
				bThreatNow = true;
				State.PredLateral = PredLat;
				State.PredHeight = PredBottom;
				State.TimeToLine = T;
			}
		}
	}
	State.ThreatTime = bThreatNow && State.bThreat ? State.ThreatTime + Dt : 0.f;
	State.bThreat = bThreatNow;
	const bool bReacting = State.bThreat && State.ThreatTime >= Tuning.ReactionTime;

	// ---- Move: cut the angle, or go for the shot ----
	float Target = PositionTarget(Tuning, Goal, BallXY, State.Depth);
	if (bReacting)
	{
		Target = SkateMath::Clamp(State.PredLateral, -LateralLimit, LateralLimit);
		if (State.DiveTime < 0.f && SkateMath::Abs(State.PredLateral) < Goal.HalfWidth + 40.f)
		{
			// Dive only when shuffling cannot get the hands there in time.
			const float Gap = State.PredLateral - State.Lateral;
			const float Need = SkateMath::Abs(Gap) - (Tuning.StandReach - 10.f);
			if (Need > Tuning.MaxShuffleSpeed * State.TimeToLine * 0.6f)
			{
				State.DiveSign = SkateMath::Sign(Gap);
				State.DiveTime = 0.f;
			}
		}
	}
	Shuffle(Target);

	// Right after the keeper's own throw-out, or another impulse this very frame: no contact yet.
	if (State.TimeSinceRelease < IgnoreAfterRelease || Ball.TimeSinceImpulse < FSkateBallControl::MinImpulseGap)
	{
		return Out;
	}

	auto Catch = [&]()
	{
		State.bHolding = true;
		State.HoldTimer = 0.f;
		++State.Saves;
		++State.Catches;
		State.LastAction = ESkateKeeperAction::Catch;
		State.TimeSinceAction = 0.f;
		State.bThreat = false;
		Out.Action = ESkateKeeperAction::Catch;
		Out.bHolding = true;
		Out.HoldPosition = FSkateVec3(KeeperBase() + Goal.Normal * HoldForward, Goal.IceZ + HoldHeight);
	};

	// ---- Smother: a slow ball (or one dribbled right into the keeper) just in front of the body ----
	const float KeeperLat = Lateral - State.Lateral;
	if (State.DiveTime < 0.f && State.TimeSinceRelease > NoSmotherAfterRelease && Bottom < 40.f && SkateMath::Abs(KeeperLat) < 60.f)
	{
		const float Speed = Ball.Vel.Size();
		// A slow loose ball in front, not rolling away (a fresh rebound is left for the shooters: no smother for 1 s after a save).
		const bool bLooseSlow = !Ball.bHeldBySkater && Along > L - BodyDepth && Along < L + 110.f && Speed < 350.f && VAlong < 50.f && State.TimeSinceAction > 1.f;
		const bool bDribbledIn = Ball.bHeldBySkater && Along > L - BodyDepth && Along < L + 60.f;
		if (bLooseSlow || bDribbledIn)
		{
			Catch();
			return Out;
		}
	}
	if (Ball.bHeldBySkater)
	{
		return Out;
	}

	// ---- Save: sweep this frame's ball path against the keeper's reach ----
	const FSkateVec3 P0 = Ball.Pos;
	const FSkateVec3 P1 = Ball.Pos + Ball.Vel * Dt - FSkateVec3(0.f, 0.f, 0.5f * Tuning.Gravity * Dt * Dt);
	const float A0 = Goal.Along(P0.XY());
	const float A1 = Goal.Along(P1.XY());
	bool bContact = false;
	FSkateVec3 Pc = P0;
	// The state's clock is at the end of this frame's step; the ball reaches the line at fraction S of it.
	float ContactTimeOffset = -Dt;
	if (A0 >= L && A1 <= L && A0 > A1)
	{
		const float S = (A0 - L) / (A0 - A1);
		Pc = FSkateVec3::Lerp(P0, P1, S);
		ContactTimeOffset = -(1.f - S) * Dt;
		bContact = true;
	}
	else if (A0 < L && A0 > L - BodyDepth && VAlong < 50.f && VAlong > -ThreatMinSpeed)
	{
		bContact = true; // a slow ball already inside the body's depth (fast ones are judged at the line only)
	}
	if (!bContact)
	{
		return Out;
	}
	const float ContactLat = Goal.Lateral(Pc.XY()) - State.Lateral;
	const float ContactBottom = SkateMath::Max(Pc.Z - Ball.Radius - Goal.IceZ, 0.f);
	if (!InReach(Tuning, State, ContactLat, ContactBottom, Ball.Radius, ContactTimeOffset))
	{
		return Out;
	}

	const float Speed = Ball.Vel.Size();
	if (Speed < Tuning.CatchMaxSpeed && SkateMath::Abs(ContactLat) <= Tuning.CatchHalfWidth && ContactBottom <= Tuning.CatchMaxHeight
		&& DiveAlpha(Tuning, State, ContactTimeOffset) < 0.3f)
	{
		Catch();
		return Out;
	}

	// Parry: back out of the goal and wide, away from the goal centre; high balls are tipped up.
	const float GoalLat = Goal.Lateral(Pc.XY());
	const float Side = SkateMath::Abs(GoalLat) > 20.f ? SkateMath::Sign(GoalLat)
		: (State.DiveSign != 0.f ? State.DiveSign : (SkateMath::Abs(ContactLat) > 1.f ? SkateMath::Sign(ContactLat) : 1.f));
	const float OutAlong = SkateMath::Max(-VAlong * Tuning.ParryRestitution, 350.f);
	const float OutLat = Side * Tuning.ParryWideSpeed + VLat * 0.25f;
	const float OutZ = ContactBottom > 120.f ? 2.f * Tuning.ParryLift : (ContactBottom > 40.f ? Tuning.ParryLift : 0.4f * Tuning.ParryLift);
	++State.Saves;
	State.LastAction = ESkateKeeperAction::Parry;
	State.TimeSinceAction = 0.f;
	State.bThreat = false;
	Out.Action = ESkateKeeperAction::Parry;
	Out.BallVelocity = FSkateVec3(Goal.Normal * OutAlong + Goal.Right() * OutLat, OutZ);
	return Out;
}

FSkateKeeperPose FSkateKeeper::Pose(const FSkateKeeperTuning& Tuning, const FSkateGoalFrame& Goal, const FSkateKeeperState& State)
{
	FSkateKeeperPose Pose;
	Pose.Position = Goal.ToWorld(Tuning.LineOffset + State.Depth, State.Lateral);
	Pose.DiveAlpha = DiveAlpha(Tuning, State);
	Pose.ButterflyAlpha = State.bButterfly ? 1.f : 0.f;
	Pose.DiveSign = State.DiveSign;
	Pose.bHolding = State.bHolding;
	if (State.bThreat && State.ThreatTime >= Tuning.ReactionTime)
	{
		Pose.ReachLateral = SkateMath::Clamp(State.PredLateral - State.Lateral, -Tuning.StandReach, Tuning.StandReach);
		Pose.ReachHeight = SkateMath::Clamp(State.PredHeight + 11.f, 20.f, Tuning.StandReachHeight);
	}
	return Pose;
}

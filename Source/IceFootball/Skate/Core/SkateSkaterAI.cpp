#include "SkateSkaterAI.h"

#include <cmath>

namespace SkateSkaterAIDetail
{
	// Close enough to the spot: stop there.
	constexpr float ArriveRadius = 40.f;
	// Distance over which the stick eases off when arriving.
	constexpr float EaseDistance = 300.f;
	// Typical braking deceleration (cm/s^2), used to start braking in time.
	constexpr float ExpectedBrakeDecel = 850.f;
	// A tiny stick deflection only turns the skates (no real thrust) while braking.
	constexpr float FacingStick = 0.06f;
	constexpr float FacingDot = 0.94f; // ~20 deg

	FSkateVec2 ClampToRink(const FSkateVec2& P, const FSkateVec2& Half, float CornerRadius)
	{
		constexpr float Margin = 150.f;
		FSkateVec2 Q(SkateMath::Clamp(P.X, -Half.X + Margin, Half.X - Margin), SkateMath::Clamp(P.Y, -Half.Y + Margin, Half.Y - Margin));
		// Rounded corners: pull a point in the corner square onto the arc.
		const FSkateVec2 CornerCentre(SkateMath::Sign(Q.X) * (Half.X - CornerRadius), SkateMath::Sign(Q.Y) * (Half.Y - CornerRadius));
		if (SkateMath::Abs(Q.X) > SkateMath::Abs(CornerCentre.X) && SkateMath::Abs(Q.Y) > SkateMath::Abs(CornerCentre.Y))
		{
			const FSkateVec2 FromCorner = Q - CornerCentre;
			const float MaxR = CornerRadius - Margin;
			if (FromCorner.Size() > MaxR)
			{
				Q = CornerCentre + FromCorner.GetSafeNormal() * MaxR;
			}
		}
		return Q;
	}

	// Full stick towards Target, boosting when far. Arrives without stopping (play on).
	FSkateMoveInput Towards(const FSkateVec2& Pos, const FSkateVec2& Target, float Boost)
	{
		FSkateMoveInput In;
		const FSkateVec2 Delta = Target - Pos;
		const float Dist = Delta.Size();
		In.Direction = Delta.GetSafeNormal(FSkateVec2(1.f, 0.f));
		In.Magnitude = 1.f;
		In.Boost = Dist > FSkateSkaterAI::BoostDistance ? Boost : 0.f;
		return In;
	}
}

FSkateMoveInput SkateSteer::Face(const FSkateVec2& Pos, const FSkateVec2& Heading, const FSkateVec2& Target)
{
	using namespace SkateSkaterAIDetail;
	FSkateMoveInput In;
	In.Brake = 1.f;
	const FSkateVec2 Dir = (Target - Pos).GetSafeNormal(Heading);
	if (Heading.Dot(Dir) < FacingDot)
	{
		In.Direction = Dir;
		In.Magnitude = FacingStick;
	}
	return In;
}

FSkateMoveInput SkateSteer::GoTo(const FSkateVec2& Pos, const FSkateVec2& Vel, const FSkateVec2& Heading, const FSkateVec2& Point, const FSkateVec2& LookAt)
{
	using namespace SkateSkaterAIDetail;
	const FSkateVec2 Delta = Point - Pos;
	const float Dist = Delta.Size();
	if (Dist < ArriveRadius)
	{
		return Face(Pos, Heading, LookAt);
	}
	FSkateMoveInput In;
	In.Direction = Delta * (1.f / Dist);
	In.Magnitude = SkateMath::Clamp(Dist / EaseDistance, 0.25f, 1.f);
	const float Speed = Vel.Size();
	if (Speed * Speed / (2.f * ExpectedBrakeDecel) > Dist)
	{
		In.Brake = 1.f; // would overshoot: brake while still steering
	}
	return In;
}

float FSkateSkaterBrain::NextSigned()
{
	Seed = Seed * 1664525u + 1013904223u; // ponytail: LCG, plenty for aim noise
	return static_cast<float>((Seed >> 8) & 0xFFFFu) / 32768.f - 1.f;
}

const char* SkateSkaterModeName(ESkateSkaterMode Mode)
{
	switch (Mode)
	{
	case ESkateSkaterMode::Wait: return "Wait";
	case ESkateSkaterMode::Receive: return "Receive pass";
	case ESkateSkaterMode::Chase: return "Chase";
	case ESkateSkaterMode::Press: return "Press";
	case ESkateSkaterMode::Defend: return "Defend";
	case ESkateSkaterMode::Support: return "Support";
	case ESkateSkaterMode::Attack: return "Attack";
	case ESkateSkaterMode::Shoot: return "Shoot";
	case ESkateSkaterMode::Pass: return "Pass";
	}
	return "?";
}

FSkateSkaterDecision FSkateSkaterAI::Think(const FSkateSkaterView& View, const FSkateAITuning& Tuning, FSkateSkaterBrain& Brain, float Dt)
{
	using namespace SkateSkaterAIDetail;
	FSkateSkaterDecision D;
	auto Clamp = [&](const FSkateVec2& P) { return ClampToRink(P, View.RinkHalf, View.CornerRadius); };
	auto Towards = [&](const FSkateVec2& Target) { return SkateSkaterAIDetail::Towards(View.Pos, Target, Tuning.BoostAmount); };
	auto GoTo = [&](const FSkateVec2& Point, const FSkateVec2& LookAt)
	{
		D.Move = SkateSteer::GoTo(View.Pos, View.Vel, View.Heading, Clamp(Point), LookAt);
		if ((Point - View.Pos).Size() > BoostDistance)
		{
			D.Move.Boost = Tuning.BoostAmount;
		}
		// Retreating from the ball: skate backwards, never turn the back on the play.
		const FSkateVec2 ToBall = (View.BallPos - View.Pos).GetSafeNormal(View.Heading);
		if (D.Move.Magnitude > 0.1f && D.Move.Direction.Dot(ToBall) < -0.2f)
		{
			D.Move.bBackward = true;
		}
	};

	if (!View.bBallValid)
	{
		D.Move.Brake = 1.f;
		return D;
	}

	// A button being held: keep aiming, release when the charge is done. Lost the ball: let go at once.
	if (Brain.ChargeLeft >= 0.f)
	{
		Brain.ChargeLeft -= Dt;
		D.Move = Towards(Brain.Aim);
		D.Move.Boost = 0.f;
		D.Mode = Brain.bChargingShot ? ESkateSkaterMode::Shoot : ESkateSkaterMode::Pass;
		if (Brain.ChargeLeft < 0.f || View.BallOwner != ESkateBallOwner::Me)
		{
			(Brain.bChargingShot ? D.Actions.bKickReleased : D.Actions.bPushReleased) = true;
			Brain.ChargeLeft = -1.f;
		}
		return D;
	}

	const FSkateVec2 ToGoal = View.AttackGoal - View.Pos;
	const float GoalDist = ToGoal.Size();
	const FSkateVec2 GoalDir = ToGoal.GetSafeNormal(View.Heading);

	if (View.BallOwner == ESkateBallOwner::Me)
	{
		// Shoot: close enough and facing the goal. Aim at the far corner (the post away from my side).
		if (GoalDist < Tuning.ShootDistance && View.Heading.Dot(GoalDir) > std::cos(ShootFacingDeg * SkateMath::DegToRad))
		{
			const FSkateVec2 Right = GoalDir.Right();
			const float MySide = (View.Pos - View.AttackGoal).Dot(Right) >= 0.f ? 1.f : -1.f;
			Brain.Aim = View.AttackGoal - Right * (MySide * View.GoalHalfWidth * 0.6f + Brain.NextSigned() * Tuning.AimError);
			Brain.ChargeLeft = Tuning.ShotCharge;
			Brain.bChargingShot = true;
			D.Actions.bKickPressed = true;
			D.Move = Towards(Brain.Aim);
			D.Move.Boost = 0.f;
			D.Mode = ESkateSkaterMode::Shoot;
			return D;
		}
		// Pass: the teammate is clearly nearer the goal and not behind me, or an opponent is on me and the teammate is
		// clear of it (any direction but straight back). Never to a teammate right next to me or too far.
		if (View.bMateValid)
		{
			const FSkateVec2 ToMate = View.MatePos - View.Pos;
			const float MateDist = ToMate.Size();
			const float MateGoalDist = (View.AttackGoal - View.MatePos).Size();
			const float MateDot = View.Heading.Dot(ToMate.GetSafeNormal(View.Heading));
			const bool bMateAhead = MateGoalDist < GoalDist - PassAdvantage && MateDot > 0.3f;
			const bool bPressed = View.bThreatValid && (View.ThreatPos - View.Pos).Size() < PressDistance
				&& (View.ThreatPos - View.MatePos).Size() > PassMateClear && MateDot > -0.5f;
			// The lane: no opponent standing on the line to the teammate.
			bool bLaneClear = true;
			if (View.bThreatValid && MateDist > 1.f)
			{
				const FSkateVec2 Lane = ToMate * (1.f / MateDist);
				const FSkateVec2 ToThreat = View.ThreatPos - View.Pos;
				const float Along = ToThreat.Dot(Lane);
				bLaneClear = Along < 0.f || Along > MateDist || SkateMath::Abs(ToThreat.Cross(Lane)) > PassLaneClear;
			}
			if (MateDist < PassMaxDistance && MateDist > PassMinDistance && bLaneClear && (bMateAhead || bPressed))
			{
				Brain.Aim = View.MatePos + View.MateVel * 0.4f;
				Brain.ChargeLeft = PassCharge;
				Brain.bChargingShot = false;
				D.Actions.bPushPressed = true;
				D.Move = Towards(Brain.Aim);
				D.Move.Boost = 0.f;
				D.Mode = ESkateSkaterMode::Pass;
				return D;
			}
		}
		// Carry the ball at the goal: aim for a point in front of it so the final approach is straight.
		const FSkateVec2 ApproachDir = (View.AttackGoal - View.OwnGoal).GetSafeNormal(View.Heading);
		D.Move = Towards(Clamp(View.AttackGoal - ApproachDir * 700.f));
		// An opponent right ahead: swerve around it instead of skating into it (and losing the ball).
		if (View.bThreatValid)
		{
			const FSkateVec2 ToThreat = View.ThreatPos - View.Pos;
			const float ThreatDist = ToThreat.Size();
			const FSkateVec2 ThreatDir = ToThreat.GetSafeNormal(D.Move.Direction);
			if (ThreatDist < AvoidDistance && D.Move.Direction.Dot(ThreatDir) > std::cos(AvoidConeDeg * SkateMath::DegToRad))
			{
				const float Side = D.Move.Direction.Cross(ThreatDir) >= 0.f ? -1.f : 1.f; // away from the threat's side
				D.Move.Direction = D.Move.Direction.Rotated(Side * AvoidTurnDeg * SkateMath::DegToRad);
				D.Move.Boost = 0.f;
			}
		}
		D.Mode = ESkateSkaterMode::Attack;
		return D;
	}

	if (View.BallOwner == ESkateBallOwner::Keeper)
	{
		D.Move = SkateSteer::Face(View.Pos, View.Heading, View.BallPos);
		D.Mode = ESkateSkaterMode::Wait;
		return D;
	}

	if (View.BallOwner == ESkateBallOwner::Teammate)
	{
		// Get open ahead of the carrier, on the side away from where I am now.
		const FSkateVec2 Ahead = (View.AttackGoal - View.BallPos).GetSafeNormal(View.Heading);
		const float Side = (View.Pos - View.BallPos).Dot(Ahead.Right()) >= 0.f ? 1.f : -1.f;
		GoTo(View.BallPos + Ahead * SupportAhead + Ahead.Right() * (Side * SupportSide), View.BallPos);
		D.Mode = ESkateSkaterMode::Support;
		return D;
	}

	// A pass for me: meet it, facing the goal (a pass is trapped from any side, and the first touch is then a shot
	// or a pass forward, not a turn). Coming past me: wait on its line. Elsewhere (a through ball, a stray pass): the
	// earliest point I can reach before the ball does; none in time: run after it.
	if (View.BallOwner == ESkateBallOwner::Nobody && View.bBallIsPassToMe && View.BallVel.Size() <= ReceiveMinBallSpeed)
	{
		// The pass has (nearly) stopped short: go and get it.
		D.Move = Towards(Clamp(View.BallPos));
		D.Mode = ESkateSkaterMode::Receive;
		return D;
	}
	if (View.BallOwner == ESkateBallOwner::Nobody && View.bBallIsPassToMe)
	{
		FSkateVec2 Target;
		float MeetTime = 0.f;
		bool bLed = false;
		bool bFound = false;
		for (float T = 0.1f; !bLed && !bFound && T <= ReceiveHorizon; T += 0.1f)
		{
			const FSkateVec2 At = SkateBallFlight::PositionAt(View.BallPos, View.BallVel, T, View.BallDamping, View.BallRollingResistance);
			// Led to me: the ball meets my own course if I keep going (steered a little, so a stick I eased off does not spoil it).
			bLed = (At - (View.Pos + View.Vel * T)).Size() <= ReceiveLedRadius;
			if (bLed || (At - View.Pos).Size() <= ReceiveMeetSpeed * T + ReceiveReach)
			{
				Target = At;
				MeetTime = T;
				bFound = !bLed;
			}
		}
		const FSkateVec2 ToMeet = Target - View.Pos;
		if ((bLed || bFound) && ToMeet.Size() > 40.f)
		{
			// Be at the meeting point when the ball is: the velocity that gets me there, as a stick (no stopping at
			// the point: momentum is part of the plan, a GoTo would brake and miss a short pass).
			const FSkateVec2 Needed = ToMeet * (1.f / MeetTime);
			D.Move.Direction = Needed.GetSafeNormal(View.Heading);
			D.Move.Magnitude = SkateMath::Clamp(Needed.Size() / ReceiveCruiseSpeed, 0.15f, 1.f);
			D.Move.Boost = Needed.Size() > ReceiveCruiseSpeed ? Tuning.BoostAmount : 0.f;
		}
		else if (bLed || bFound)
		{
			D.Move = SkateSteer::Face(View.Pos, View.Heading, View.AttackGoal);
		}
		else
		{
			D.Move = Towards(Clamp(SkateBallFlight::PositionAt(View.BallPos, View.BallVel, ReceiveHorizon, View.BallDamping, View.BallRollingResistance)));
		}
		D.Mode = ESkateSkaterMode::Receive;
		return D;
	}

	if (View.bChaser && !(View.BallOwner == ESkateBallOwner::Nobody && View.bBallIsMyPass))
	{
		// Loose ball: meet it where it will be; carried by an opponent: go at the ball itself (body check).
		const float BallSpeed = View.BallVel.Size();
		const float Lead = SkateMath::Clamp((View.BallPos - View.Pos).Size() / 700.f, 0.f, 1.f);
		const FSkateVec2 Target = View.BallOwner == ESkateBallOwner::Nobody && BallSpeed > 50.f ? View.BallPos + View.BallVel * Lead : View.BallPos;
		D.Move = Towards(Clamp(Target));
		D.Mode = View.BallOwner == ESkateBallOwner::Opponent ? ESkateSkaterMode::Press : ESkateSkaterMode::Chase;
		// Pressing: the carrier is close and ahead -> body check.
		const FSkateVec2 ToBall = View.BallPos - View.Pos;
		D.bCheck = View.BallOwner == ESkateBallOwner::Opponent && ToBall.Size() < Tuning.CheckRange && View.Heading.Dot(ToBall.GetSafeNormal(View.Heading)) > 0.7f;
		return D;
	}

	// Defender: between the ball and the own goal, out of the keeper's way.
	const FSkateVec2 FromGoal = View.BallPos - View.OwnGoal;
	const float Along = SkateMath::Max(FromGoal.Size() * DefendFraction, DefendMinFromGoal);
	GoTo(View.OwnGoal + FromGoal.GetSafeNormal(View.Heading) * Along, View.BallPos);
	D.Mode = ESkateSkaterMode::Defend;
	return D;
}

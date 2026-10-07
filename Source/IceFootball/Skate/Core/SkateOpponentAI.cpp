#include "SkateOpponentAI.h"

#include "SkateTeamAI.h"

#include <cmath>

const char* SkateOpponentModeName(ESkateOpponentMode Mode)
{
	switch (Mode)
	{
	case ESkateOpponentMode::Wait: return "Wait";
	case ESkateOpponentMode::Chase: return "Chase";
	case ESkateOpponentMode::Press: return "Press";
	case ESkateOpponentMode::Defend: return "Defend";
	case ESkateOpponentMode::Support: return "Support";
	case ESkateOpponentMode::Attack: return "Attack";
	case ESkateOpponentMode::Shoot: return "Shoot";
	case ESkateOpponentMode::Pass: return "Pass";
	}
	return "?";
}

namespace SkateOpponentAIDetail
{
	FSkateVec2 ClampToRink(const FSkateVec2& P, const FSkateVec2& Half)
	{
		constexpr float Margin = 150.f;
		return FSkateVec2(SkateMath::Clamp(P.X, -Half.X + Margin, Half.X - Margin), SkateMath::Clamp(P.Y, -Half.Y + Margin, Half.Y - Margin));
	}

	// Full stick towards Target, boosting when far. Arrives without stopping (play on).
	FSkateMoveInput Towards(const FSkateVec2& Pos, const FSkateVec2& Target)
	{
		FSkateMoveInput In;
		const FSkateVec2 Delta = Target - Pos;
		const float Dist = Delta.Size();
		In.Direction = Delta.GetSafeNormal(FSkateVec2(1.f, 0.f));
		In.Magnitude = 1.f;
		In.Boost = Dist > FSkateOpponentAI::BoostDistance ? FSkateOpponentAI::BoostAmount : 0.f;
		return In;
	}
}

FSkateOpponentDecision FSkateOpponentAI::Think(const FSkateOpponentView& View, FSkateOpponentBrain& Brain, float Dt)
{
	using namespace SkateOpponentAIDetail;
	FSkateOpponentDecision D;
	auto GoTo = [&](const FSkateVec2& Point, const FSkateVec2& LookAt)
	{
		D.Move = SkateSteer::GoTo(View.Pos, View.Vel, View.Heading, ClampToRink(Point, View.RinkHalf), LookAt);
		if ((Point - View.Pos).Size() > BoostDistance)
		{
			D.Move.Boost = BoostAmount;
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
		D.Move = Towards(View.Pos, Brain.Aim);
		D.Move.Boost = 0.f;
		D.Mode = Brain.bChargingShot ? ESkateOpponentMode::Shoot : ESkateOpponentMode::Pass;
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
		if (GoalDist < ShootDistance && View.Heading.Dot(GoalDir) > std::cos(ShootFacingDeg * 3.14159265f / 180.f))
		{
			const FSkateVec2 Right = GoalDir.Right();
			const float MySide = (View.Pos - View.AttackGoal).Dot(Right) >= 0.f ? 1.f : -1.f;
			Brain.Aim = View.AttackGoal - Right * (MySide * View.GoalHalfWidth * 0.7f);
			Brain.ChargeLeft = ShotCharge;
			Brain.bChargingShot = true;
			D.Actions.bKickPressed = true;
			D.Move = Towards(View.Pos, Brain.Aim);
			D.Move.Boost = 0.f;
			D.Mode = ESkateOpponentMode::Shoot;
			return D;
		}
		// Pass: the teammate is clearly nearer the goal, not too far, and not behind me.
		if (View.bMateValid)
		{
			const FSkateVec2 ToMate = View.MatePos - View.Pos;
			const float MateDist = ToMate.Size();
			const float MateGoalDist = (View.AttackGoal - View.MatePos).Size();
			if (MateDist < PassMaxDistance && MateGoalDist < GoalDist - PassAdvantage && View.Heading.Dot(ToMate.GetSafeNormal(View.Heading)) > 0.3f)
			{
				Brain.Aim = View.MatePos + View.MateVel * 0.4f;
				Brain.ChargeLeft = PassCharge;
				Brain.bChargingShot = false;
				D.Actions.bPushPressed = true;
				D.Move = Towards(View.Pos, Brain.Aim);
				D.Move.Boost = 0.f;
				D.Mode = ESkateOpponentMode::Pass;
				return D;
			}
		}
		// Carry the ball at the goal: aim for a point in front of it so the final approach is straight.
		const FSkateVec2 ApproachDir = (View.AttackGoal - View.OwnGoal).GetSafeNormal(View.Heading);
		D.Move = Towards(View.Pos, ClampToRink(View.AttackGoal - ApproachDir * 700.f, View.RinkHalf));
		// An opponent right ahead: swerve around it instead of skating into it (and losing the ball).
		if (View.bThreatValid)
		{
			const FSkateVec2 ToThreat = View.ThreatPos - View.Pos;
			const float ThreatDist = ToThreat.Size();
			const FSkateVec2 ThreatDir = ToThreat.GetSafeNormal(D.Move.Direction);
			if (ThreatDist < AvoidDistance && D.Move.Direction.Dot(ThreatDir) > std::cos(AvoidConeDeg * 3.14159265f / 180.f))
			{
				const float Side = D.Move.Direction.Cross(ThreatDir) >= 0.f ? -1.f : 1.f; // away from the threat's side
				D.Move.Direction = D.Move.Direction.Rotated(Side * AvoidTurnDeg * 3.14159265f / 180.f);
				D.Move.Boost = 0.f;
			}
		}
		D.Mode = ESkateOpponentMode::Attack;
		return D;
	}

	if (View.BallOwner == ESkateBallOwner::Keeper)
	{
		D.Move = SkateSteer::Face(View.Pos, View.Heading, View.BallPos);
		D.Mode = ESkateOpponentMode::Wait;
		return D;
	}

	if (View.BallOwner == ESkateBallOwner::Teammate)
	{
		// Get open ahead of the carrier, on the side away from where I am now.
		const FSkateVec2 Ahead = (View.AttackGoal - View.BallPos).GetSafeNormal(View.Heading);
		const float Side = (View.Pos - View.BallPos).Dot(Ahead.Right()) >= 0.f ? 1.f : -1.f;
		GoTo(View.BallPos + Ahead * SupportAhead + Ahead.Right() * (Side * SupportSide), View.BallPos);
		D.Mode = ESkateOpponentMode::Support;
		return D;
	}

	if (View.bChaser)
	{
		// Loose ball: meet it where it will be; carried by an opponent: go at the ball itself.
		const float BallSpeed = View.BallVel.Size();
		const float Lead = SkateMath::Clamp((View.BallPos - View.Pos).Size() / 700.f, 0.f, 1.f);
		const FSkateVec2 Target = View.BallOwner == ESkateBallOwner::Nobody && BallSpeed > 50.f ? View.BallPos + View.BallVel * Lead : View.BallPos;
		D.Move = Towards(View.Pos, ClampToRink(Target, View.RinkHalf));
		D.Mode = View.BallOwner == ESkateBallOwner::Opponent ? ESkateOpponentMode::Press : ESkateOpponentMode::Chase;
		return D;
	}

	// Defender: between the ball and the own goal, out of the keeper's way.
	const FSkateVec2 FromGoal = View.BallPos - View.OwnGoal;
	const float Along = SkateMath::Max(FromGoal.Size() * DefendFraction, DefendMinFromGoal);
	GoTo(View.OwnGoal + FromGoal.GetSafeNormal(View.Heading) * Along, View.BallPos);
	D.Mode = ESkateOpponentMode::Defend;
	return D;
}

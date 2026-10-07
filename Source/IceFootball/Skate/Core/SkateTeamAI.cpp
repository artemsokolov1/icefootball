#include "SkateTeamAI.h"

namespace SkateTeamAIDetail
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
}

const char* SkateTeammateModeName(ESkateTeammateMode Mode)
{
	switch (Mode)
	{
	case ESkateTeammateMode::Wait: return "Wait";
	case ESkateTeammateMode::HoldBall: return "Hold ball";
	case ESkateTeammateMode::Receive: return "Receive pass";
	case ESkateTeammateMode::Fetch: return "Fetch ball";
	}
	return "?";
}

FSkateMoveInput FSkateTeammateAI::Think(const FSkateTeammateView& View, ESkateTeammateMode* OutMode)
{
	using namespace SkateTeamAIDetail;
	FSkateMoveInput In;
	ESkateTeammateMode Mode = ESkateTeammateMode::Wait;

	// Stop and turn the skates towards Target.
	auto Face = [&](const FSkateVec2& Target)
	{
		In.Brake = 1.f;
		const FSkateVec2 Dir = (Target - View.Pos).GetSafeNormal(View.Heading);
		if (View.Heading.Dot(Dir) < FacingDot)
		{
			In.Direction = Dir;
			In.Magnitude = FacingStick;
		}
	};
	// Skate to Point and stop there facing LookAt.
	auto GoTo = [&](const FSkateVec2& Point, const FSkateVec2& LookAt)
	{
		const FSkateVec2 Delta = Point - View.Pos;
		const float Dist = Delta.Size();
		if (Dist < ArriveRadius)
		{
			Face(LookAt);
			return;
		}
		In.Direction = Delta * (1.f / Dist);
		In.Magnitude = SkateMath::Clamp(Dist / EaseDistance, 0.25f, 1.f);
		const float Speed = View.Vel.Size();
		if (Speed * Speed / (2.f * ExpectedBrakeDecel) > Dist)
		{
			In.Brake = 1.f; // would overshoot: brake while still steering
		}
	};

	if (View.bHasBall)
	{
		In.Brake = 1.f;
		Mode = ESkateTeammateMode::HoldBall;
	}
	else if (!View.bBallValid || View.bBallHeld)
	{
		if (View.bBallValid)
		{
			Face(View.BallPos);
		}
		else
		{
			In.Brake = 1.f;
		}
	}
	else
	{
		const FSkateVec2 Rel = View.BallPos - View.Pos;
		const float BallSpeed = View.BallVel.Size();
		bool bDone = false;
		if (BallSpeed > FetchMaxBallSpeed)
		{
			// Closest approach of the ball's path to me.
			const float T = -Rel.Dot(View.BallVel) / (BallSpeed * BallSpeed);
			if (T > 0.f && T < ReceiveHorizon)
			{
				const FSkateVec2 Closest = View.BallPos + View.BallVel * T;
				if ((Closest - View.Pos).Size() < ReceiveRadius)
				{
					GoTo(Closest, View.BallPos);
					Mode = ESkateTeammateMode::Receive;
					bDone = true;
				}
			}
		}
		if (!bDone && BallSpeed <= FetchMaxBallSpeed && Rel.Size() < FetchRadius)
		{
			GoTo(View.BallPos, View.BallPos);
			Mode = ESkateTeammateMode::Fetch;
			bDone = true;
		}
		if (!bDone)
		{
			Face(View.BallPos);
		}
	}
	if (OutMode)
	{
		*OutMode = Mode;
	}
	return In;
}

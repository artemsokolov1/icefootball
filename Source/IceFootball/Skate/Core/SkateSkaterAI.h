// Ice skating prototype - AI for every skater the player is not controlling (engine independent).
//
// Used for the player's teammate and for both opponents. Two roles, decided by the caller: the
// "chaser" (nearest to the ball on its team) goes for the ball or presses the carrier; the other one
// supports an attacking teammate or drops back between the ball and the own goal. With the ball: skate
// at the goal, pass to a teammate that is clearly ahead, shoot at the far corner once close enough and
// facing the goal. (The player's teammate hands control to the player as soon as it traps the ball, so
// for it only the off-ball roles matter.) Shots and passes use the same charge-and-release buttons as
// the player, so the ball control code does not know who is pressing them.
#pragma once

#include "SkateBallControl.h"
#include "SkateMath.h"
#include "SkateModel.h"
#include "SkateTuning.h"

/** Shared steering primitives. */
namespace SkateSteer
{
	/** Stop and turn the skates towards Target. */
	FSkateMoveInput Face(const FSkateVec2& Pos, const FSkateVec2& Heading, const FSkateVec2& Target);
	/** Skate to Point and stop there facing LookAt (brakes in time, eases off on arrival). */
	FSkateMoveInput GoTo(const FSkateVec2& Pos, const FSkateVec2& Vel, const FSkateVec2& Heading, const FSkateVec2& Point, const FSkateVec2& LookAt);
}

enum class ESkateBallOwner : unsigned char
{
	Nobody,
	Me,
	Teammate,
	Opponent,
	Keeper,
};

struct FSkateSkaterView
{
	FSkateVec2 Pos;
	FSkateVec2 Vel;
	FSkateVec2 Heading = FSkateVec2(1.f, 0.f);

	bool bBallValid = false;
	FSkateVec2 BallPos;
	FSkateVec2 BallVel;
	ESkateBallOwner BallOwner = ESkateBallOwner::Nobody;
	/** The loose ball is my own pass: let the receiver have it (no chasing it down). */
	bool bBallIsMyPass = false;
	/** The loose ball is a pass from someone else (teammate, keeper): settle where it comes past. */
	bool bBallIsPassToMe = false;

	/** Goal line centres on the ice and the mouth half width. */
	FSkateVec2 AttackGoal;
	FSkateVec2 OwnGoal;
	float GoalHalfWidth = 260.f;
	/** Half size of the rink and its corner radius: targets are kept inside. */
	FSkateVec2 RinkHalf = FSkateVec2(3000.f, 1500.f);
	float CornerRadius = 850.f;

	/** This skater is the one of its team nearest to the ball. */
	bool bChaser = true;
	bool bMateValid = false;
	FSkateVec2 MatePos;
	FSkateVec2 MateVel;
	/** Nearest skater of the other team (the carrier steers around it). */
	bool bThreatValid = false;
	FSkateVec2 ThreatPos;
};

enum class ESkateSkaterMode : unsigned char
{
	Wait,
	Receive,
	Chase,
	Press,
	Defend,
	Support,
	Attack,
	Shoot,
	Pass,
};

const char* SkateSkaterModeName(ESkateSkaterMode Mode);

/** Per-skater memory: a button held for a charge. */
struct FSkateSkaterBrain
{
	/** Seconds left on the button being held (shot or pass); < 0 = not charging. */
	float ChargeLeft = -1.f;
	bool bChargingShot = false;
	FSkateVec2 Aim;
	/** Deterministic random stream for the shot error (seed it per skater for variety). */
	unsigned int Seed = 12345u;
	/** Next value in [-1, 1). */
	float NextSigned();
};

struct FSkateSkaterDecision
{
	FSkateMoveInput Move;
	FSkateBallActionInput Actions;
	/** Press the check button this frame (pressing the carrier). */
	bool bCheck = false;
	ESkateSkaterMode Mode = ESkateSkaterMode::Wait;
};

class FSkateSkaterAI
{
public:
	static FSkateSkaterDecision Think(const FSkateSkaterView& View, const FSkateAITuning& Tuning, FSkateSkaterBrain& Brain, float Dt);

	/** Shoots (from Tuning.ShootDistance) while heading within this angle (deg) of the goal. */
	static constexpr float ShootFacingDeg = 30.f;
	static constexpr float PassCharge = 0.3f;
	/** Pass when the teammate is this much nearer the goal (cm) and not farther than PassMaxDistance. */
	static constexpr float PassAdvantage = 500.f;
	static constexpr float PassMaxDistance = 1800.f;
	/** The defender sits this fraction of the way from the own goal to the ball, never nearer the goal than DefendMinFromGoal. */
	static constexpr float DefendFraction = 0.4f;
	static constexpr float DefendMinFromGoal = 600.f;
	/** Support position: this far ahead of the carrier towards the goal, this far to the side. */
	static constexpr float SupportAhead = 600.f;
	static constexpr float SupportSide = 700.f;
	/** Carrying the ball: an opponent closer than this (cm) and within AvoidConeDeg ahead is skated around. */
	static constexpr float AvoidDistance = 350.f;
	static constexpr float AvoidConeDeg = 50.f;
	static constexpr float AvoidTurnDeg = 65.f;
	/** A pass is received where its path passes within this distance (cm) and within this time (s). */
	static constexpr float ReceiveRadius = 500.f;
	static constexpr float ReceiveHorizon = 3.f;
	static constexpr float ReceiveMinBallSpeed = 250.f;
	/** Skate with boost (Tuning.BoostAmount) when farther than this (cm) from the target. */
	static constexpr float BoostDistance = 800.f;
};

// Ice skating prototype - the opposing team's skaters (engine independent).
//
// Two roles, decided by the caller: the "chaser" (nearest to the ball) goes for the ball or presses
// the carrier; the other one supports an attacking teammate or drops back between the ball and the
// own goal. With the ball: skate at the goal, pass to a teammate that is clearly ahead, shoot at the
// far corner once close enough and facing the goal. Shots and passes use the same charge-and-release
// buttons as the player, so the ball control code does not know who is pressing them.
#pragma once

#include "SkateBallControl.h"
#include "SkateMath.h"
#include "SkateModel.h"

enum class ESkateBallOwner : unsigned char
{
	Nobody,
	Me,
	Teammate,
	Opponent,
	Keeper,
};

struct FSkateOpponentView
{
	FSkateVec2 Pos;
	FSkateVec2 Vel;
	FSkateVec2 Heading = FSkateVec2(1.f, 0.f);

	bool bBallValid = false;
	FSkateVec2 BallPos;
	FSkateVec2 BallVel;
	ESkateBallOwner BallOwner = ESkateBallOwner::Nobody;

	/** Goal line centres on the ice and the mouth half width. */
	FSkateVec2 AttackGoal;
	FSkateVec2 OwnGoal;
	float GoalHalfWidth = 260.f;
	/** Half size of the rink: targets are kept inside. */
	FSkateVec2 RinkHalf = FSkateVec2(2500.f, 1600.f);

	/** This skater is the one of its team nearest to the ball. */
	bool bChaser = true;
	bool bMateValid = false;
	FSkateVec2 MatePos;
	FSkateVec2 MateVel;
	/** Nearest skater of the other team (the carrier steers around it). */
	bool bThreatValid = false;
	FSkateVec2 ThreatPos;
};

enum class ESkateOpponentMode : unsigned char
{
	Wait,
	Chase,
	Press,
	Defend,
	Support,
	Attack,
	Shoot,
	Pass,
};

const char* SkateOpponentModeName(ESkateOpponentMode Mode);

/** Per-skater memory: a button held for a charge. */
struct FSkateOpponentBrain
{
	/** Seconds left on the button being held (shot or pass); < 0 = not charging. */
	float ChargeLeft = -1.f;
	bool bChargingShot = false;
	FSkateVec2 Aim;
};

struct FSkateOpponentDecision
{
	FSkateMoveInput Move;
	FSkateBallActionInput Actions;
	ESkateOpponentMode Mode = ESkateOpponentMode::Wait;
};

class FSkateOpponentAI
{
public:
	static FSkateOpponentDecision Think(const FSkateOpponentView& View, FSkateOpponentBrain& Brain, float Dt);

	/** Shoots from closer than this (cm to the goal line centre) ... */
	static constexpr float ShootDistance = 1100.f;
	/** ... while heading within this angle (deg) of the goal. */
	static constexpr float ShootFacingDeg = 30.f;
	/** Hold the kick button this long (s): a full shot (>= KickMaxChargeTime), the only kind that beats a keeper. */
	static constexpr float ShotCharge = 0.85f;
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
	/** Skate with boost when farther than this (cm) from the target. */
	static constexpr float BoostDistance = 800.f;
	static constexpr float BoostAmount = 0.5f;
};

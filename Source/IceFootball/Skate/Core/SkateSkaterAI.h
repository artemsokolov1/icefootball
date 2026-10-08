// Ice skating prototype - AI for every skater the player is not controlling (engine independent).
//
// Used for the player's teammates and for the opponents. Roles, decided by the caller: the "chaser"
// (nearest to the ball on its team) goes for the ball or presses the carrier; the one told to hold back
// (nearest to the own goal) drops between the ball and the own goal; anyone else supports an attacking
// teammate or marks an opponent. With the ball: skate at the goal, pass to the best placed teammate
// (clearly ahead, or clear when pressed), shoot at the far corner once close enough and facing the goal. (The player's teammate hands control to the player as soon as it traps the ball, so
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
	/** The loose ball is a teammate's (or own keeper's) pass: meet it. */
	bool bBallIsPassToMe = false;
	/** Ball physics (damping 1/s, rolling resistance cm/s^2): where the ball will be. */
	float BallDamping = 0.35f;
	float BallRollingResistance = 50.f;

	/** Goal line centres on the ice and the mouth half width. */
	FSkateVec2 AttackGoal;
	FSkateVec2 OwnGoal;
	float GoalHalfWidth = 260.f;
	/** Half size of the rink and its corner radius: targets are kept inside. */
	FSkateVec2 RinkHalf = FSkateVec2(3000.f, 1500.f);
	float CornerRadius = 850.f;

	/** This skater is the one of its team nearest to the ball. */
	bool bChaser = true;
	/** The last skater back: defends instead of supporting / marking. */
	bool bHoldBack = false;
	/** Teammates (pass options). */
	static constexpr int MaxMates = 4;
	int MateCount = 0;
	FSkateVec2 MatePos[MaxMates];
	FSkateVec2 MateVel[MaxMates];
	/** Nearest skater of the other team (the carrier steers around it). */
	bool bThreatValid = false;
	FSkateVec2 ThreatPos;
	/** The opponent to mark when off the ball (the caller picks it; not the carrier). */
	bool bMarkValid = false;
	FSkateVec2 MarkPos;
};

enum class ESkateSkaterMode : unsigned char
{
	Wait,
	Receive,
	Chase,
	Press,
	Defend,
	Mark,
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
	static constexpr float PassCharge = 0.1f;
	/** Pass when the teammate is this much nearer the goal (cm), or when an opponent is within PressDistance and the
	 *  teammate is clear of it (PassMateClear); never farther than PassMaxDistance or nearer than PassMinDistance. */
	static constexpr float PassAdvantage = 500.f;
	static constexpr float PassMaxDistance = 1800.f;
	static constexpr float PassMinDistance = 300.f;
	static constexpr float PressDistance = 400.f;
	static constexpr float PassMateClear = 500.f;
	/** No pass when an opponent stands closer than this (cm) to the line to the teammate. */
	static constexpr float PassLaneClear = 150.f;
	/** The defender sits this fraction of the way from the own goal to the ball, never nearer the goal than DefendMinFromGoal. */
	static constexpr float DefendFraction = 0.4f;
	static constexpr float DefendMinFromGoal = 600.f;
	/** Support position: this far ahead of the carrier towards the goal, this far to the side. */
	static constexpr float SupportAhead = 600.f;
	static constexpr float SupportSide = 700.f;
	/** Marking: this far (cm) goal-side of the marked opponent. */
	static constexpr float MarkDistance = 250.f;
	/** Carrying the ball: an opponent closer than this (cm) and within AvoidConeDeg ahead is skated around. */
	static constexpr float AvoidDistance = 350.f;
	static constexpr float AvoidConeDeg = 50.f;
	static constexpr float AvoidTurnDeg = 65.f;
	/** A pass led to where the receiver is going (its path comes within ReceiveLedRadius cm of the receiver's own
	 *  course) is received by holding that course; otherwise the receiver goes to the earliest point it can reach
	 *  (at ReceiveMeetSpeed cm/s, trapping within ReceiveReach cm) before the ball does, looking up to ReceiveHorizon (s)
	 *  ahead; a ball it cannot reach in time is run after. */
	static constexpr float ReceiveLedRadius = 90.f;
	static constexpr float ReceiveCruiseSpeed = 550.f;
	static constexpr float ReceiveMeetSpeed = 400.f;
	static constexpr float ReceiveReach = 80.f;
	static constexpr float ReceiveHorizon = 3.f;
	static constexpr float ReceiveMinBallSpeed = 250.f;
	/** Skate with boost (Tuning.BoostAmount) when farther than this (cm) from the target. */
	static constexpr float BoostDistance = 800.f;
};

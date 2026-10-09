// Ice skating prototype - skater <-> ball contact rules (engine independent).
//
// The ball is a free rigid body. This code is the ONLY source of skater-to-ball impulses:
// the skater capsule ignores the ball in collision, so physics never adds a second hit.
// Every impulse is a one-shot velocity change decided here and applied once by the UE side.
//
// Per frame at most one impulse is produced, in priority order:
//   Kick (buffered X release) > Push (buffered A) > Dribble touch > Body block.
//
// Possession (FSkatePossessionTuning::bEnabled): a reachable, low, controllable ball is
// trapped and then CARRIED: every frame the ball's velocity is steered onto a carry point in
// front of the skater (skater velocity + bounded correction). The ball stays a simulated body,
// so walls and cones still stop it - if it is held back it is lost. On turns the carry point
// orbits AROUND the skater at a limited rate, so the ball never passes through the legs.
// Carry steering is not a gameplay impulse; push / kick release the ball with a single impulse.
#pragma once

#include "SkateMath.h"
#include "SkateTuning.h"

/** Closed-form flight of a grounded ball under linear damping k (1/s) and rolling resistance a (cm/s^2):
 *  v(t) = (v0 + a/k) e^(-kt) - a/k. Used to pace and lead passes, and to meet them. */
namespace SkateBallFlight
{
	/** Speed left after travelling Distance from V0; 0 when the ball stops first. */
	float SpeedAfter(float V0, float Distance, float Damping, float Resistance);
	/** Time to travel Distance from V0; < 0 when the ball stops first. */
	float TimeFor(float V0, float Distance, float Damping, float Resistance);
	/** Time until a ball launched at V0 stops. */
	float StopTime(float V0, float Damping, float Resistance);
	/** Launch speed that covers Distance and still has ArriveSpeed there (MaxSpeed when even that is not enough). */
	float SpeedFor(float Distance, float ArriveSpeed, float Damping, float Resistance, float MaxSpeed);
	/** Where the ball is T seconds on (it stays where it stops). */
	FSkateVec2 PositionAt(const FSkateVec2& Pos, const FSkateVec2& Vel, float T, float Damping, float Resistance);
}

/** Why the ball can / cannot be touched right now. Shown in the debug HUD. */
enum class ESkateContactReason : unsigned char
{
	NoBall,
	Disabled,       // ball interaction switched off (stage A: skating only)
	Reachable,      // inside the foot reach zone: touches, push and kick allowed
	ActionReachOnly,// slightly outside the touch zone: push / kick allowed, no auto touch
	TooFar,         // further than the reach zone
	OutsideAngle,   // beside or behind the skater (angle from heading > ReachHalfAngle)
	Airborne,       // ball bottom higher than MaxTouchHeight above the ice
	BlockedByBoard, // a wall / static object between skater and ball
	TooFast,        // relative speed above MaxControllableRelSpeed
};

const char* SkateContactReasonName(ESkateContactReason Reason);

enum class ESkateImpulseKind : unsigned char
{
	None,
	Touch,
	Push,
	Kick,
	BodyBlock,
	Save,      // the goalkeeper's parry / throw-out (not a skater impulse)
};

const char* SkateImpulseKindName(ESkateImpulseKind Kind);

enum class ESkatePossessionLoss : unsigned char
{
	None,
	Kick,      // released by a shot
	Push,      // released by a push / knock-on
	Blocked,   // held back by a wall or obstacle
	Airborne,  // bounced up
	Disabled,  // interaction switched off / reset
	Taken,     // someone else (the keeper, an opponent) took the ball
	Hit,       // knocked loose by a body check
};

const char* SkatePossessionLossName(ESkatePossessionLoss Loss);

/** A board face near the skater (vertical plane): Point on the face, Normal pointing into the rink. */
struct FSkateWallPlane
{
	FSkateVec2 Point;
	FSkateVec2 Normal = FSkateVec2(1.f, 0.f);
};

/** Feet: 0 = left, 1 = right (same indexing as the pose). */
namespace SkateFoot
{
	constexpr int Left = 0;
	constexpr int Right = 1;
}

struct FSkateContactQuery
{
	FSkateVec2 SkaterPos;
	FSkateVec2 SkaterVel;
	FSkateVec2 Heading = FSkateVec2(1.f, 0.f);
	/** Skater's reference top speed (cm/s), used to scale how far touches release the ball. */
	float SkaterMaxSpeed = 580.f;

	/** World-space stick direction and shaped magnitude. */
	FSkateVec2 StickDir;
	float StickMag = 0.f;

	bool bHasBall = false;
	FSkateVec3 BallPos;
	FSkateVec3 BallVel;
	float BallRadius = 11.f;
	float IceZ = 0.f;

	/** False when a trace from the skater to the ball hits a wall (no touching through boards). */
	bool bLineOfSightClear = true;
	bool bInteractionEnabled = true;
	/** Just body-checked: no trap, no touch, no action; a carried ball is dropped (loss Hit). */
	bool bStunned = false;

	/** Another player (teammate / keeper) holds the ball: no trap, no touch, no impulse from this skater. */
	bool bBallHeldByOther = false;
	/** The holder is an opponent past its protection time (Possession.StealProtectTime): this skater may
	 *  trap the ball off its feet (the usual trap zone / speed rules still apply). */
	bool bStealAllowed = false;
	/** Time (s) since ANY gameplay impulse on the ball (other skaters, keeper): one impulse per frame overall. */
	float BallTimeSinceImpulse = 100.f;
	/** The ball is a teammate's pass (push / throw-out from the own team): received firmer, from any side and from further
	 *  out, and a released A / X waits for it (one touch). An opponent's pass is a loose ball. */
	bool bIncomingPass = false;
	/** Ball physics (ASkateBall): linear damping (1/s) and rolling resistance (cm/s^2), for the pass flight model. */
	float BallDamping = 0.35f;
	float BallRollingResistance = 50.f;

	/** The teammate every pass (A) goes to, led to where it will be. */
	bool bPassTargetValid = false;
	FSkateVec2 PassTargetPos;
	FSkateVec2 PassTargetVel;
	/** Where a shot goes when the stick is idle: the goal being attacked. */
	bool bShotTargetValid = false;
	FSkateVec2 ShotTargetPos;
	/** Through pass target: the point ahead of the teammate towards the goal (inside the rink). */
	bool bThroughTargetValid = false;
	FSkateVec2 ThroughTargetPos;

	/** Boards near the skater: the carried ball is kept in front of them instead of being pressed in. */
	static constexpr int MaxWalls = 4;
	FSkateWallPlane Walls[MaxWalls];
	int NumWalls = 0;

	void AddWall(const FSkateVec2& Point, const FSkateVec2& Normal)
	{
		if (NumWalls < MaxWalls)
		{
			Walls[NumWalls].Point = Point;
			Walls[NumWalls].Normal = Normal.GetSafeNormal(FSkateVec2(1.f, 0.f));
			++NumWalls;
		}
	}
};

struct FSkateContactReport
{
	ESkateContactReason Reason = ESkateContactReason::NoBall;
	float Distance = 0.f;            // skater centre -> ball centre, on the ice plane
	float DistanceToReachCentre = 0.f;
	float AngleFromHeadingDeg = 0.f;
	float BallHeight = 0.f;          // lowest point of the ball above the ice
	float RelativeSpeed = 0.f;       // |ball - skater| planar velocity
	float ClosingSpeed = 0.f;        // > 0 when the skater closes in on the ball
	bool bTouchAllowed = false;      // a dribble touch would happen this frame if not on cooldown
	bool bHasDribbleIntent = false;
	bool bOnCooldown = false;
	bool bInTrapZone = false;        // close enough (front or side) to be trapped into possession
};

struct FSkateBallImpulse
{
	ESkateImpulseKind Kind = ESkateImpulseKind::None;
	FSkateVec3 NewBallVelocity;
	FSkateVec3 DeltaV;
	FSkateVec2 Direction;
	float Power = 0.f; // kick charge 0..1
	int Foot = SkateFoot::Right; // which foot plays it (push / kick)

	bool IsValid() const { return Kind != ESkateImpulseKind::None; }
};

/** Button edges for this frame. */
struct FSkateBallActionInput
{
	bool bPushPressed = false;
	bool bPushReleased = false;
	bool bKickPressed = false;
	bool bKickReleased = false;
	/** Through pass (Y): one press, no charge - into the space ahead of the teammate. */
	bool bThroughPressed = false;
};

struct FSkatePossessionState
{
	bool bPossessed = false;
	/** World yaw (rad) and distance of the carry point around the skater centre. */
	float OrbitAngle = 0.f;
	float OrbitDistance = 0.f;
	/** Dribble rhythm phase 0..1. */
	float DribblePhase = 0.f;
	float BlockedTime = 0.f;
	float TimeHeld = 0.f;
	float TimeSinceLost = 100.f;
	float TimeSinceBlock = 100.f;
	/** Distance between the ball and its carry point this frame (cm). */
	float CarryError = 0.f;
	FSkateVec2 CarryTarget;
	bool bHasPrevTarget = false;
	/** Increments on every dribble tap (for the foot animation / tap sound). */
	int TouchPulseCount = 0;
	/** Foot of the latest dribble tap. */
	int TapFoot = SkateFoot::Right;
	/** Side (+1 right foot, -1 left foot) the ball sits at now, and its smoothed sideways offset (cm). */
	float SideSign = 1.f;
	float SideOffset = 0.f;
	int AcquireCount = 0;
	ESkatePossessionLoss LastLoss = ESkatePossessionLoss::None;
};

/** Carry steering for this frame (not a gameplay impulse). */
struct FSkateBallCarry
{
	bool bActive = false;
	FSkateVec3 Velocity;
	FSkateVec2 Target;
};

struct FSkateBallControlState
{
	FSkatePossessionState Possession;

	float TimeSinceImpulse = 100.f;
	float TimeSinceAction = 100.f;

	bool bCharging = false;
	float ChargeTime = 0.f;

	bool bChargingPass = false;
	float PassChargeTime = 0.f;
	float PendingPassPower = 0.f;

	/** Foot that will play the next push / kick (updated every frame, with hysteresis; pose wind-up). */
	int PlannedFoot = SkateFoot::Right;

	float PushBuffer = -1.f;   // >= 0 while a released pass waits for the ball
	bool bPendingThrough = false; // the waiting pass is a through pass
	float KickBuffer = -1.f;   // >= 0 while a released kick waits for the ball
	float PendingKickPower = 0.f;

	// Last outcome (debug HUD)
	ESkateImpulseKind LastKind = ESkateImpulseKind::None;
	float LastDeltaV = 0.f;
	float LastPower = 0.f;
	int ImpulseCount = 0;

	ESkateImpulseKind LastFailedAction = ESkateImpulseKind::None;
	ESkateContactReason LastFailReason = ESkateContactReason::NoBall;
	float TimeSinceFail = 100.f;
};

class FSkateBallControl
{
public:
	/** Pure query: where is the ball relative to the skater's feet and may it be touched. */
	static FSkateContactReport Evaluate(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);

	/** Advances timers/buffers and returns at most one impulse for this frame.
	 *  When the ball is possessed and no impulse happens, OutCarry receives the carry steering. */
	static FSkateBallImpulse Update(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query,
		const FSkateBallActionInput& Actions, float Dt, FSkateBallControlState& State, FSkateContactReport& OutReport,
		FSkateBallCarry* OutCarry = nullptr);

	/** Cancels a pass / shot wind-up in progress and pending buffers (control switched to another skater). */
	static void CancelActions(FSkateBallControlState& State);

	/** Drops possession (scene reset, interaction off). */
	static void ReleasePossession(FSkateBallControlState& State, ESkatePossessionLoss Reason);

	/** 0..1 kick charge while X is held (for HUD/pose). */
	static float ChargeFraction(const FSkateBallControlTuning& Tuning, const FSkateBallControlState& State);

	/** 0..1 pass charge while A is held (for HUD/pose). */
	static float PassChargeFraction(const FSkateBallControlTuning& Tuning, const FSkateBallControlState& State);

	/** Physical contact direction: from the foot (just in front of the skater centre) to the ball. */
	static FSkateVec2 ContactNormal(const FSkateContactQuery& Query);

	/** Which foot should play a ball going in Direction (keeps CurrentFoot unless the other one is clearly better). */
	static int ChooseFoot(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, const FSkateVec2& Direction, int CurrentFoot);

	/** Moves a carry point out of the boards in Query.Walls (and keeps it out of the body). */
	static FSkateVec2 KeepOffWalls(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, const FSkateVec2& Point);

	/** Rotates From towards To by at most MaxDeg. */
	static FSkateVec2 LimitDeviation(const FSkateVec2& From, const FSkateVec2& To, float MaxDeg);

	static void Reset(FSkateBallControlState& State);

	/** Minimum time between any two impulses (s). Hard guarantee against double hits. */
	static constexpr float MinImpulseGap = 0.05f;

	/** Where a through pass goes: ThroughLead ahead of the teammate towards the goal, plus the way it runs while the
	 *  ball travels there (the ball arrives at ThroughArriveSpeed and waits). Not clamped to the rink. */
	static FSkateVec2 ThroughTarget(const FSkateBallControlTuning& Tuning, const FSkateVec2& BallPos, const FSkateVec2& MatePos,
		const FSkateVec2& MateVel, const FSkateVec2& GoalCentre, float BallDamping, float BallRollingResistance);

private:
	static FSkateBallImpulse MakeTouch(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);
	static FSkateBallImpulse MakePush(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Power);
	static FSkateBallImpulse MakeThroughPass(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);
	static FSkateBallImpulse MakeKick(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Power);
	static FSkateBallImpulse MakeBodyBlock(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);
	static FSkateVec2 DesiredDirection(const FSkateContactQuery& Query, float MinStick);
	static bool CanAcquire(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, const FSkateContactReport& Report, const FSkateBallControlState& State);
	static FSkateBallCarry ComputeCarry(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Dt, FSkateBallControlState& State);
};

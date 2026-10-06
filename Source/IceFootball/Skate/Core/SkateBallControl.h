// Ice skating prototype - skater <-> ball contact rules (engine independent).
//
// The ball is a free rigid body. This code is the ONLY source of skater-to-ball impulses:
// the skater capsule ignores the ball in collision, so physics never adds a second hit.
// Every impulse is a one-shot velocity change decided here and applied once by the UE side.
//
// Per frame at most one impulse is produced, in priority order:
//   Kick (buffered X release) > Push (buffered A) > Dribble touch > Body block.
#pragma once

#include "SkateMath.h"
#include "SkateTuning.h"

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
};

const char* SkateImpulseKindName(ESkateImpulseKind Kind);

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
};

struct FSkateBallImpulse
{
	ESkateImpulseKind Kind = ESkateImpulseKind::None;
	FSkateVec3 NewBallVelocity;
	FSkateVec3 DeltaV;
	FSkateVec2 Direction;
	float Power = 0.f; // kick charge 0..1

	bool IsValid() const { return Kind != ESkateImpulseKind::None; }
};

/** Button edges for this frame. */
struct FSkateBallActionInput
{
	bool bPushPressed = false;
	bool bKickPressed = false;
	bool bKickReleased = false;
};

struct FSkateBallControlState
{
	float TimeSinceImpulse = 100.f;
	float TimeSinceAction = 100.f;

	bool bCharging = false;
	float ChargeTime = 0.f;

	float PushBuffer = -1.f;   // >= 0 while a push command waits for the ball
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

	/** Advances timers/buffers and returns at most one impulse for this frame. */
	static FSkateBallImpulse Update(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query,
		const FSkateBallActionInput& Actions, float Dt, FSkateBallControlState& State, FSkateContactReport& OutReport);

	/** 0..1 kick charge while X is held (for HUD/pose). */
	static float ChargeFraction(const FSkateBallControlTuning& Tuning, const FSkateBallControlState& State);

	/** Physical contact direction: from the foot (just in front of the skater centre) to the ball. */
	static FSkateVec2 ContactNormal(const FSkateContactQuery& Query);

	/** Rotates From towards To by at most MaxDeg. */
	static FSkateVec2 LimitDeviation(const FSkateVec2& From, const FSkateVec2& To, float MaxDeg);

	static void Reset(FSkateBallControlState& State);

	/** Minimum time between any two impulses (s). Hard guarantee against double hits. */
	static constexpr float MinImpulseGap = 0.05f;

private:
	static FSkateBallImpulse MakeTouch(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);
	static FSkateBallImpulse MakePush(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);
	static FSkateBallImpulse MakeKick(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query, float Power);
	static FSkateBallImpulse MakeBodyBlock(const FSkateBallControlTuning& Tuning, const FSkateContactQuery& Query);
	static FSkateVec2 DesiredDirection(const FSkateContactQuery& Query, float MinStick);
};

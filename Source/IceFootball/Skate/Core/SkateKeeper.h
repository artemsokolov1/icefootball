// Ice skating prototype - goalkeeper AI (engine independent).
//
// The keeper stands a little in front of the goal line and shuffles sideways to cut the angle
// (on the bisector of the ball -> posts angle). When a shot comes, it reacts after ReactionTime:
// it shuffles to the predicted crossing point or, when that is too far to reach in time, dives.
// Saves are decided here (like all ball impulses in this prototype, never by physics contacts):
// every frame, before the physics step, the ball's path for that frame is swept against the keeper's
// reach (body + hands, extended by the dive, lower at the far end of a dive). A slow ball near the
// body is caught, held and then rolled out to the controlled skater; anything else is parried away
// from the goal. Full-power shots into the corners beat the dive - goals are possible, not free.
//
// Goal space: Along = distance in front of the goal line (into the rink), Lateral = sideways
// (positive towards GoalFrame.Normal.Right()), Height = above the ice.
#pragma once

#include "SkateBallControl.h"
#include "SkateMath.h"
#include "SkateTuning.h"

struct FSkateGoalFrame
{
	/** Centre of the goal line on the ice (world XY), the direction into the rink, mouth size. */
	FSkateVec2 Center;
	FSkateVec2 Normal = FSkateVec2(-1.f, 0.f);
	float HalfWidth = 260.f;
	float Height = 180.f;
	float IceZ = 0.f;

	FSkateVec2 Right() const { return Normal.Right(); }
	float Along(const FSkateVec2& P) const { return (P - Center).Dot(Normal); }
	float Lateral(const FSkateVec2& P) const { return (P - Center).Dot(Right()); }
	FSkateVec2 ToWorld(float InAlong, float InLateral) const { return Center + Normal * InAlong + Right() * InLateral; }
};

enum class ESkateKeeperAction : unsigned char
{
	None,
	Parry,    // ball deflected away from the goal
	Catch,    // ball caught, now held
	Release,  // held ball rolled out to a skater
};

const char* SkateKeeperActionName(ESkateKeeperAction Action);

struct FSkateKeeperBall
{
	bool bValid = false;
	FSkateVec3 Pos;
	FSkateVec3 Vel;
	float Radius = 11.f;
	/** A skater has the ball at the feet (the keeper may still smother it right in front of the goal). */
	bool bHeldBySkater = false;
	/** Time (s) since any gameplay impulse on the ball: the keeper waits a frame rather than add a second one. */
	float TimeSinceImpulse = 100.f;
};

struct FSkateKeeperState
{
	float Lateral = 0.f;
	float LateralVel = 0.f;

	// Shot tracking.
	bool bThreat = false;
	float ThreatTime = 0.f;        // s since the current shot was first seen
	float PredLateral = 0.f;       // where the ball will cross the keeper's line
	float PredHeight = 0.f;        // lowest point of the ball there, above the ice
	float TimeToLine = 0.f;

	// Dive.
	float DiveSign = 0.f;          // -1 / +1 while diving, 0 standing
	float DiveTime = -1.f;         // s since the dive started (< 0 = not diving)

	// Ball in hands.
	bool bHolding = false;
	float HoldTimer = 0.f;
	float TimeSinceRelease = 100.f;

	// Results.
	int Saves = 0;
	int Catches = 0;
	ESkateKeeperAction LastAction = ESkateKeeperAction::None;
	float TimeSinceAction = 100.f;
};

struct FSkateKeeperOutput
{
	ESkateKeeperAction Action = ESkateKeeperAction::None;
	/** Parry / Release: the ball's new velocity. Release also teleports the ball to BallPosition. */
	FSkateVec3 BallVelocity;
	FSkateVec3 BallPosition;
	/** While holding: the ball sits here (in the keeper's hands), velocity zero. */
	bool bHolding = false;
	FSkateVec3 HoldPosition;
};

/** Pose hints for the visuals (derived from the state, no gameplay effect). */
struct FSkateKeeperPose
{
	FSkateVec2 Position;      // keeper centre on the ice (world)
	float DiveAlpha = 0.f;    // 0 standing .. 1 full stretch
	float DiveSign = 0.f;     // side of the dive in goal-lateral terms
	float ReachLateral = 0.f; // where the hands go (goal lateral, relative to the keeper)
	float ReachHeight = 120.f;
	bool bHolding = false;
};

class FSkateKeeper
{
public:
	static void Reset(FSkateKeeperState& State);

	/** One frame. ThrowTarget = where a held ball is rolled out to (the controlled skater). */
	static FSkateKeeperOutput Update(const FSkateKeeperTuning& Tuning, const FSkateGoalFrame& Goal, const FSkateKeeperBall& Ball,
		const FSkateVec2& ThrowTarget, float Dt, FSkateKeeperState& State);

	static FSkateKeeperPose Pose(const FSkateKeeperTuning& Tuning, const FSkateGoalFrame& Goal, const FSkateKeeperState& State);

	/** 0..1 extension of the current dive (TimeOffset: seconds relative to the state's time, e.g. earlier in the frame). */
	static float DiveAlpha(const FSkateKeeperTuning& Tuning, const FSkateKeeperState& State, float TimeOffset = 0.f);

	/** Where the keeper wants to stand for a ball at BallPos (angle bisector, inside the posts). */
	static float PositionTarget(const FSkateKeeperTuning& Tuning, const FSkateGoalFrame& Goal, const FSkateVec2& BallPos);

	/** Can the keeper reach a ball at (lateral, lowest point height) on its line right now? */
	static bool InReach(const FSkateKeeperTuning& Tuning, const FSkateKeeperState& State, float BallLateral, float BallBottom, float BallRadius,
		float TimeOffset = 0.f);
};

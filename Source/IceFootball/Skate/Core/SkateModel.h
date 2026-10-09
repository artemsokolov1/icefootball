// Ice skating prototype - skating movement model (engine independent).
//
// Separation of concerns (see Docs/IceSkatePrototype.md):
//   * Actual velocity      -> FSkateMoveState::Velocity (owned by CharacterMovement, passed in/out every step)
//   * Desired direction    -> FSkateMoveInput::Direction/Magnitude (camera-relative stick, already shaped)
//   * Blade/body heading   -> FSkateMoveState::Heading (turns at a speed-dependent rate, never snaps)
//   * Animation pose       -> derived elsewhere from the telemetry below; never feeds back.
//
// Velocity is NEVER assigned from the stick. Each step:
//   1. heading turns towards the stick (rate limited, lower at speed),
//   2. velocity is split into along-blade (glide) and across-blade (grip) parts,
//   3. grip removes across-blade speed and redirects most of it along the blade (carving),
//   4. thrust accelerates along the blade towards the stick's target speed,
//      glide friction slows it when not pushing,
//   5. LT / reverse-stop braking reduces speed magnitude, never below zero.
#pragma once

#include "SkateMath.h"
#include "SkateTuning.h"

enum class ESkateMovePhase : unsigned char
{
	Idle,
	Push,        // accelerating with the stick
	Glide,       // free glide, stick released or at target speed
	Carve,       // turning on the edges (significant lateral acceleration)
	Brake,       // LT braking
	ReverseStop, // stick pulled against travel at speed: stopping before going the other way
};

const char* SkatePhaseName(ESkateMovePhase Phase);

struct FSkateMoveInput
{
	/** World-space unit direction on the ice. Zero = stick released. */
	FSkateVec2 Direction;
	/** Shaped stick magnitude 0..1. */
	float Magnitude = 0.f;
	/** Shaped brake 0..1 (LT). */
	float Brake = 0.f;
	/** Boost 0..1 (RT). */
	float Boost = 0.f;
	/** Skate backwards: the blades point away from the stick, thrust goes the stick's way (defenders face the play). */
	bool bBackward = false;
	/** Deke this frame: -1 cut left, +1 cut right (a one-frame trigger; ignored while a deke is under way). */
	int DekeSide = 0;
};

struct FSkateMoveState
{
	// ---- Simulation state ----
	FSkateVec2 Velocity;
	FSkateVec2 Heading = FSkateVec2(1.f, 0.f);
	bool bReverseStop = false;
	/** Stick history for flick detection / committed turn direction. */
	FSkateVec2 PrevStickDir;
	bool bPrevStick = false;
	float TimeSinceStickFlick = 100.f;
	/** The last flick landed in the backward sector (such a flick starts a reverse stop at once). */
	bool bFlickIntoBack = false;
	/** Seconds the stick has been held in the backward sector (a hold of ReverseHoldTime starts a reverse stop). */
	float TimeStickBack = 0.f;
	float TurnSign = 0.f;
	/** Smoothed angular speed of the stick itself (rad/s, + = towards the right). */
	float StickSpin = 0.f;
	FSkateVec2 PrevFrameStickDir;
	bool bPrevFrameStick = false;
	/** Sprint stamina 0..1 (FSkateMovementTuning::StaminaTime); empty = exhausted until a third is back. */
	float Stamina = 1.f;
	bool bExhausted = false;
	/** Deke under way: seconds left of the sideways cut and its side (+1 = right of the blades). */
	float DekeLeft = 0.f;
	float DekeSign = 0.f;

	// ---- Telemetry of the last Step() call (averages over the step) ----
	ESkateMovePhase Phase = ESkateMovePhase::Idle;
	/** 0..1 how much the skater is actively pushing (drives stride animation). */
	float PushAmount = 0.f;
	/** Average thrust acceleration (cm/s^2). */
	float ThrustAccel = 0.f;
	/** Average braking deceleration from LT + reverse stop (cm/s^2). */
	float BrakeDecel = 0.f;
	/** Average speed lost to skidding (cm/s^2). */
	float ScrubDecel = 0.f;
	/** Average lateral (centripetal) acceleration, + = towards heading-right (cm/s^2). Drives lean. */
	float LateralAccel = 0.f;
	/** Angle between blade and velocity (deg, 0..90). */
	float SlipAngleDeg = 0.f;
	/** Speed the stick currently asks for (cm/s). */
	float TargetSpeed = 0.f;
	/** Current heading turn-rate limit (deg/s). */
	float TurnRateLimitDeg = 0.f;
	/** Concrete fault: braking flipped the velocity direction. Must never become true. */
	bool bBrakeReversalFault = false;
};

class FSkateModel
{
public:
	/** Advances the skating model by Dt. Internally sub-steps (<= Tuning.MaxSubstep) for frame-rate independence. */
	static void Step(const FSkateMovementTuning& Tuning, const FSkateMoveInput& Input, float Dt, FSkateMoveState& State);

	/** Heading turn-rate limit (rad/s) at a given speed and brake amount. */
	static float TurnRateLimit(const FSkateMovementTuning& Tuning, float Speed, float Brake);

	/** Clears velocity and transient state, keeps nothing from the previous run. */
	static void Reset(FSkateMoveState& State, const FSkateVec2& Heading);

private:
	struct FAccum
	{
		float Push = 0.f;
		float Thrust = 0.f;
		float Brake = 0.f;
		float Scrub = 0.f;
		float Lateral = 0.f;
		float Slip = 0.f;
	};

	static void SubStep(const FSkateMovementTuning& Tuning, const FSkateMoveInput& Input, float H, FSkateMoveState& State, FAccum& Acc);
};

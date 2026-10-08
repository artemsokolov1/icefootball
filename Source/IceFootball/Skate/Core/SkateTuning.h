// Ice skating prototype - THE single tuning set.
//
// Every gameplay number of the prototype lives in FSkateTuning (this file documents
// defaults = "Balanced" preset). The three switchable presets are built in
// SkateTuningPresets.cpp from these defaults by overriding a few fields.
// In the editor the presets are exposed on ASkateCharacter ("Skate|Tuning" category),
// so they can be tweaked live in PIE without recompiling.
//
// Units: centimetres, seconds, degrees, kilograms (1 UU = 1 cm).
//
// This header is plain C++ apart from the reflection macros. Tools/SkateSim compiles it
// with stubbed macros so the movement/contact core can be tested outside Unreal.
#pragma once

#include "CoreMinimal.h"
#include "SkateTuning.generated.h"

UENUM(BlueprintType)
enum class ESkatePreset : uint8
{
	Responsive UMETA(DisplayName = "1 - Responsive"),
	Balanced UMETA(DisplayName = "2 - Balanced"),
	Inertial UMETA(DisplayName = "3 - Inertial"),
};

/** Stick / trigger shaping. */
USTRUCT(BlueprintType)
struct FSkateInputTuning
{
	GENERATED_BODY()

	/** Radial inner dead zone of the left stick (fraction of full deflection). Below it the stick reads exactly zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0", ClampMax = "0.5"))
	float StickDeadZoneInner = 0.15f;

	/** Deflection treated as full (compensates sticks that never reach 1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.5", ClampMax = "1"))
	float StickDeadZoneOuter = 0.95f;

	/** Response curve after the dead zone remap: magnitude^Exponent. 1 = linear, >1 = finer control at small deflection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.5", ClampMax = "3"))
	float StickResponseExponent = 1.2f;

	/** Trigger dead zone (LT/RT). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0", ClampMax = "0.3"))
	float TriggerDeadZone = 0.04f;

	/** Brake trigger curve: brake = trigger^Exponent. >1 gives finer light braking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.5", ClampMax = "3"))
	float BrakeInputExponent = 1.4f;

	/** Keyboard only: stick magnitude while the "slow" key (Left Alt) is held, to test partial stick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.1", ClampMax = "1"))
	float KeyboardSlowMagnitude = 0.45f;
};

/** Skating model (USkateMovementComponent / FSkateModel). */
USTRUCT(BlueprintType)
struct FSkateMovementTuning
{
	GENERATED_BODY()

	// ---- Thrust ----

	/** Top speed with full stick, no boost (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Thrust", meta = (ClampMin = "100"))
	float MaxSpeed = 580.f;

	/** Top speed with full stick and full RT (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Thrust", meta = (ClampMin = "100"))
	float BoostMaxSpeed = 1000.f;

	/** Backward skating reaches this fraction of the forward speeds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.1", ClampMax = "1"))
	float BackwardSpeedScale = 0.8f;

	/** Time constant (s) of the speed approach to the stick's target speed. ~3x this is the time to 95% of top speed.
	 *  Acceleration is highest at the start (fast first response) and fades near the target (smooth top-out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Thrust", meta = (ClampMin = "0.05"))
	float ThrustTimeConstant = 0.34f;

	/** Same as ThrustTimeConstant while boosting (towards BoostMaxSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Thrust", meta = (ClampMin = "0.05"))
	float BoostTimeConstant = 0.36f;

	/** Thrust is only produced when the skates point roughly where the stick asks:
	 *  thrust scale ramps from 0 at this dot(heading, stick) to 1 at dot = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Thrust", meta = (ClampMin = "-1", ClampMax = "0.95"))
	float ThrustAlignMinDot = 0.25f;

	/** Deceleration (cm/s^2) towards the stick's target speed when going faster than it (partial stick after full speed, boost released). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Thrust", meta = (ClampMin = "0"))
	float OverspeedDecel = 170.f;

	// ---- Free glide ----

	/** Constant glide friction (cm/s^2) along the blade when not pushing. Makes the glide end in finite time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Glide", meta = (ClampMin = "0"))
	float GlideFriction = 55.f;

	/** Speed-proportional glide drag (1/s) along the blade when not pushing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Glide", meta = (ClampMin = "0"))
	float GlideDrag = 0.30f;

	/** Below this speed (cm/s), with no thrust, the skater snaps to a full stop (kills micro-sliding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Glide", meta = (ClampMin = "0", ClampMax = "30"))
	float StopSnapSpeed = 6.f;

	// ---- Blade grip / turning ----

	/** Lateral grip (1/s): rate at which sideways velocity (across the blades) is removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "0"))
	float LateralGrip = 10.f;

	/** Cap on how fast grip can bend the velocity (cm/s^2). At speed v, minimum arc radius = v^2 / this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "100"))
	float MaxLateralAccel = 1700.f;

	/** Fraction of the lateral speed removed by grip that is redirected along the blade (carving keeps speed).
	 *  The rest is scrubbed (skid). Falls off with slip angle, see SkidSlipAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "0", ClampMax = "1"))
	float CarveEfficiency = 0.92f;

	/** Slip angle (deg, between blade and velocity) at which carving stops redirecting speed - pure skid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "10", ClampMax = "90"))
	float SkidSlipAngle = 70.f;

	/** Heading turn rate (deg/s) when nearly stopped - compact pivot turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "30"))
	float TurnRateLowSpeed = 800.f;

	/** Heading turn rate (deg/s) at TurnRateSpeedRef and above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "30"))
	float TurnRateHighSpeed = 200.f;

	/** Speed (cm/s) at which the turn rate reaches TurnRateHighSpeed (smoothstep blend from 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "10"))
	float TurnRateSpeedRef = 520.f;

	/** With the stick released, the blades slowly align with the travel direction (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "0"))
	float GlideAlignRate = 90.f;

	// ---- Braking ----

	/** Deceleration (cm/s^2) at full LT. Never reverses the velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "0"))
	float BrakeDecel = 850.f;

	/** Heading turn rate multiplier at full brake (steering while in a hockey stop is reduced). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "0", ClampMax = "1"))
	float BrakeTurnRateScale = 0.4f;

	/** Stick pointing against the travel direction with dot(stick, velocity) below this starts a "reverse stop":
	 *  the skater first brakes (no instant velocity flip), then accelerates the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "-1", ClampMax = "0"))
	float ReverseIntentDot = -0.45f;

	/** Reverse stop only triggers above this speed (cm/s); below it the skater simply pivots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "0"))
	float ReverseMinSpeed = 150.f;

	/** Deceleration (cm/s^2) of a reverse stop at full stick deflection. High = snappy direction change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "0"))
	float ReverseBrakeDecel = 1900.f;

	/** In a reverse stop the blades swing round to the new direction at this rate (deg/s) and the skater
	 *  already pushes that way, so the stop flows straight into acceleration the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "90"))
	float ReverseTurnRate = 1100.f;

	/** A reverse stop needs a FLICK: the stick must jump (or come from neutral) into the backward sector
	 *  within this time (s). Sweeping the stick around the rim (circling) is a turn, never a stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "0", ClampMax = "1"))
	float ReverseFlickWindow = 0.2f;

	/** Stick direction change (deg) within one frame that counts as a flick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Brake", meta = (ClampMin = "30", ClampMax = "180"))
	float ReverseFlickAngle = 75.f;

	// ---- Carving with the stick swept around ----

	/** Minimum thrust (fraction) while the stick asks for a turn the blades have not reached yet:
	 *  the skater keeps pushing (crossovers) through a turn instead of coasting and losing speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "0", ClampMax = "1"))
	float TurnThrustScale = 0.8f;

	/** At speed, the blades may lead the travel direction by at most this angle (deg). Keeps hard turns
	 *  carved (speed kept) instead of turning the blades sideways into a skid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "5", ClampMax = "90"))
	float MaxCarveLead = 28.f;

	/** Below this speed (cm/s) the blades may pivot freely (compact turn on the spot); the lead limit fades in above it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Turning", meta = (ClampMin = "0"))
	float CarveLeadSpeed = 260.f;

	// ---- Integration ----

	/** Largest internal integration step (s). Smaller = less frame-rate dependence. 1/240 is cheap (pure math). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Integration", meta = (ClampMin = "0.001", ClampMax = "0.0334"))
	float MaxSubstep = 1.f / 240.f;
};

/** Ball possession: the ball stays at the skater's feet while carried (dribbling). */
USTRUCT(BlueprintType)
struct FSkatePossessionTuning
{
	GENERATED_BODY()

	/** On: a reachable, low, not-too-fast ball is trapped and carried at the feet.
	 *  Off: the older "free ball + dribble touches" mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession")
	bool bEnabled = true;

	/** Trap zone: a low ball whose centre is within this distance (cm) of the skater centre... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "30"))
	float TrapDistance = 100.f;

	/** ...and within this angle (deg) of the blades' heading is trapped - in front AND beside the skater,
	 *  not only exactly in front. It then swings around to the front (OrbitRate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "30", ClampMax = "180"))
	float TrapHalfAngle = 160.f;

	/** Ball lowest point up to this high above the ice (cm) can still be trapped (a small hop off the board). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float TrapMaxHeight = 45.f;

	/** Incoming balls faster than this (cm/s, relative to the skater) bounce off instead of being trapped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float AcquireMaxRelSpeed = 1600.f;

	/** A pass (from a teammate or the keeper's throw) can be received up to this relative speed (cm/s):
	 *  a firm pass is cushioned instead of bouncing off. Shots keep the AcquireMaxRelSpeed limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float PassReceiveMaxRelSpeed = 2600.f;

	/** No re-trap for this long (s) after the skater's own push or kick, so the ball can leave the feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float AcquireCooldownAfterAction = 0.4f;

	/** No re-trap for this long (s) after the ball was knocked loose (blocked by a wall, airborne). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float AcquireCooldownAfterLoss = 0.35f;

	/** The take button (B): an opponent's ball closer than TakeRange (cm) is knocked to the taker's feet, once it has
	 *  been held for StealProtectTime (s); the button then rests for TakeCooldown (s). One rule, no angles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Possession", meta = (ClampMin = "0"))
	float TakeRange = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Possession", meta = (ClampMin = "0"))
	float StealProtectTime = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Possession", meta = (ClampMin = "0"))
	float TakeCooldown = 1.0f;

	/** No take from behind: the taker must not be deeper in the carrier's back sector than this (dot of the
	 *  carrier's heading with the direction to the taker; -0.3 = more than ~107 deg behind is forbidden). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Possession", meta = (ClampMin = "-1", ClampMax = "1"))
	float TakeBehindDot = -0.3f;

	/** Speed (cm/s) the knocked-loose ball gets towards the taker's feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Possession", meta = (ClampMin = "0"))
	float TakeBallSpeed = 500.f;

	/** Without the button a carried ball is only taken when the carrier let it stray farther than this (cm)
	 *  from the carry point (dribbling error, board, shove): it counts as loose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Possession", meta = (ClampMin = "0"))
	float StealLooseDistance = 30.f;

	/** No re-trap for this long (s) after the ball bounced off the legs (short: a ball pinned at the board
	 *  must not ping-pong between the board and the skates). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float AcquireCooldownAfterBlock = 0.12f;

	/** Ball centre distance in front of the skater centre when standing / at MaxSpeed (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "30"))
	float CarryDistanceSlow = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "30"))
	float CarryDistanceFast = 60.f;

	/** Sideways offset of the carried ball towards the foot that taps it next (cm). Each dribble tap
	 *  sends it across to the other foot, so taps alternate left / right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float CarrySideOffset = 6.f;

	/** Time constant (s) of the ball converging onto its carry point. Smaller = tighter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0.01"))
	float FollowTime = 0.05f;

	/** How fast (deg/s) the ball can swing around the skater when the body turns. It goes AROUND
	 *  the skater (never through the legs). Higher than the body turn rate = the ball never falls behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "90"))
	float OrbitRate = 900.f;

	/** Cap on the correction speed (cm/s, relative to the skater) used to pull the ball onto its carry point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "50"))
	float MaxCorrectionSpeed = 900.f;

	/** Dribble rhythm: the ball is tapped this far ahead (cm) at MaxSpeed and reeled back in. 0 = glued. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float DribbleAmplitude = 18.f;

	/** Dribble taps per second when slow / at MaxSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0.1"))
	float DribbleCadenceSlow = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0.1"))
	float DribbleCadenceFast = 2.2f;

	/** Ball further than this from its carry point (cm) for LoseTime = blocked (wall, cone) -> loose ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "10"))
	float LoseDistance = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float LoseTime = 0.12f;

	/** Ball this far from its carry point (cm) = lost immediately (never yanked back from far away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "10"))
	float LoseDistanceInstant = 65.f;

	/** Ball lowest point higher than this above the ice (cm) = airborne -> loose ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float LoseHeight = 32.f;

	/** The carried ball is kept this far (cm) off the boards: it rolls along them instead of being pressed in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession", meta = (ClampMin = "0"))
	float BoardClearance = 1.f;
};

/** Skater <-> ball contact: reach zone, dribble touches, push (A), charged kick (X). */
USTRUCT(BlueprintType)
struct FSkateBallControlTuning
{
	GENERATED_BODY()

	// ---- Reach zone (where a foot can physically reach the ball) ----

	/** Centre of the reach zone in front of the skater (cm, along heading). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Reach", meta = (ClampMin = "0"))
	float ReachForward = 42.f;

	/** Radius of the reach zone around that centre (cm, ball centre must be inside). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Reach", meta = (ClampMin = "5"))
	float ReachRadius = 36.f;

	/** Extra radius (cm) allowed for deliberate actions (push / kick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Reach", meta = (ClampMin = "0"))
	float ActionReachBonus = 14.f;

	/** Ball must be within this angle (deg) of the heading, seen from the skater centre. Prevents touches behind the back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Reach", meta = (ClampMin = "10", ClampMax = "180"))
	float ReachHalfAngle = 80.f;

	/** Max height of the ball's lowest point above the ice (cm) for a ground touch. Higher = airborne, no dribble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Reach", meta = (ClampMin = "0"))
	float MaxTouchHeight = 18.f;

	/** Relative speed (cm/s) above which an incoming ball is not controllable (it is body-blocked instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Reach", meta = (ClampMin = "0"))
	float MaxControllableRelSpeed = 1300.f;

	// ---- Dribble touches ----

	/** Min stick magnitude to auto-dribble (otherwise skater must at least glide at DribbleMinSpeedNoStick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0", ClampMax = "1"))
	float DribbleMinStick = 0.2f;

	/** Without stick input, gliding faster than this (cm/s) into the ball still touches it on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0"))
	float DribbleMinSpeedNoStick = 180.f;

	/** Skater must close in on the ball faster than this (cm/s) for a dribble touch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0"))
	float DribbleMinClosingSpeed = 25.f;

	/** Minimum time between two dribble touches (s). Also the minimum gap after any impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0.02"))
	float TouchCooldown = 0.2f;

	/** Ball speed after a touch = skater speed along the touch * TouchCarry + extra (below). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0", ClampMax = "2"))
	float TouchCarry = 1.0f;

	/** Extra speed (cm/s) given on top of the skater's speed when slow: ball stays close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0"))
	float TouchExtraSpeedSlow = 70.f;

	/** Extra speed (cm/s) given on top of the skater's speed at MaxSpeed: ball is released further ahead.
	 *  Blend between Slow and Fast is quadratic in (skater speed / MaxSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0"))
	float TouchExtraSpeedFast = 300.f;

	/** Touch direction assist: the physical contact direction (foot -> ball) may be bent towards
	 *  the stick by at most this angle (deg). 0 = pure physical contact direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Dribble", meta = (ClampMin = "0", ClampMax = "60"))
	float TouchAssistMaxAngle = 28.f;

	// ---- Body block (ball hitting skater outside the foot zone) ----

	/** Radius (cm) of the skater's body for blocking the ball (the capsule itself ignores the ball). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Body", meta = (ClampMin = "5"))
	float BodyRadius = 30.f;

	/** Restitution of a ball bouncing off the skater's body/legs. Low = legs absorb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Body", meta = (ClampMin = "0", ClampMax = "1"))
	float BodyRestitution = 0.25f;

	// ---- Push (A) ----

	/** Pass (A) ball speed (cm/s) for a quick tap - already a firm pass. Hold A to charge up to PassMaxSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0"))
	float PushSpeed = 1150.f;

	/** A pass (A) always goes to the teammate, led to where it will be, and is played just fast enough to get there
	 *  at this speed (cm/s) given the ball's damping and rolling resistance. The charge adds pace on top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0"))
	float PassArriveSpeed = 600.f;

	/** With a pass on its way to this skater, a released A / X waits this long (s) for the ball: a one-touch pass or shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0", ClampMax = "3"))
	float OneTouchBufferTime = 1.5f;

	/** Through pass (Y): played this far (cm) ahead of the teammate towards the goal, arriving at ThroughArriveSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0"))
	float ThroughLead = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0"))
	float ThroughArriveSpeed = 650.f;

	/** Pass ball speed (cm/s) at full charge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0"))
	float PassMaxSpeed = 2300.f;

	/** Time (s) to reach full pass power while holding A. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0.1"))
	float PassMaxChargeTime = 0.6f;

	/** Fraction of the skater's speed along the push direction added to the push. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0", ClampMax = "2"))
	float PushCarry = 0.6f;

	/** Max angle (deg) between the push direction and the physical contact direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0", ClampMax = "90"))
	float PushMaxDeviation = 45.f;

	/** Pressing A while the ball is not reachable keeps the command alive this long (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Push", meta = (ClampMin = "0", ClampMax = "0.5"))
	float PushBufferTime = 0.25f;

	// ---- Charged kick (X) ----

	/** Time (s) to reach full kick power while holding X. Charging beyond it does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0.1"))
	float KickMaxChargeTime = 0.8f;

	/** Ball speed (cm/s) at zero charge (a quick tap of X is already a real shot) / at full charge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0"))
	float KickMinSpeed = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0"))
	float KickMaxSpeed = 3200.f;

	/** Fraction of the skater's speed along the kick direction added to the kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0", ClampMax = "2"))
	float KickCarry = 0.5f;

	/** Upward speed (cm/s) at full charge: a strong shot flies (ball centre apex ~1.55 m, the ball stays under a 1.8 m bar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0"))
	float KickLiftAtFullCharge = 530.f;

	/** Below this charge (0..1) shots stay on the ice; above it the lift grows smoothly to KickLiftAtFullCharge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0", ClampMax = "0.95"))
	float KickLiftStartCharge = 0.35f;

	/** Max angle (deg) between the kick direction and the physical contact direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0", ClampMax = "90"))
	float KickMaxDeviation = 35.f;

	// ---- Which foot plays the ball ----

	/** Foot choice for passes / shots: the foot on the ball's side, and for an angled ball the foot that
	 *  plays it with the inside (shot to the left = right foot, to the right = left foot).
	 *  This bias only decides near-ties: +1 = strongly right-footed, -1 = left-footed, 0 = two-footed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Feet", meta = (ClampMin = "-1", ClampMax = "1"))
	float FootPreference = 0.15f;

	/** After X is released while the ball is out of reach, the kick waits this long (s) for the ball, then whiffs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0", ClampMax = "0.5"))
	float KickBufferTime = 0.3f;

	/** No dribble touches for this long (s) after a push or kick, so the leaving ball is not re-touched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Kick", meta = (ClampMin = "0"))
	float NoTouchAfterAction = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball|Possession")
	FSkatePossessionTuning Possession;
};

/** Ball rigid body (ASkateBall). */
USTRUCT(BlueprintType)
struct FSkateBallPhysicsTuning
{
	GENERATED_BODY()

	/** Ball radius (cm). Size 5 football = 11 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "5"))
	float Radius = 11.f;

	/** Mass (kg). Size 5 football = 0.43 kg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0.05"))
	float MassKg = 0.43f;

	/** Physics linear damping (air drag; applies to all axes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0"))
	float LinearDamping = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0"))
	float AngularDamping = 0.6f;

	/** Rolling resistance on the ice (cm/s^2), applied only while the ball is on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0"))
	float RollingResistance = 50.f;

	/** Grounded ball slower than this (cm/s) is stopped (no endless creeping). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0", ClampMax = "20"))
	float StopSpeed = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0", ClampMax = "1"))
	float Friction = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0", ClampMax = "1"))
	float Restitution = 0.6f;

	/** Ice surface friction / restitution (ice physical material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0", ClampMax = "1"))
	float IceFriction = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0", ClampMax = "1"))
	float IceRestitution = 0.45f;

	/** Board (wall) restitution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics", meta = (ClampMin = "0", ClampMax = "1"))
	float BoardRestitution = 0.62f;

	/** Continuous collision detection on the ball only. Max kick ~3200 cm/s (+ carry) moves ~60 cm per 60 FPS frame,
	 *  almost 3x the ball diameter, so without CCD fast shots could tunnel through thin geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallPhysics")
	bool bUseCCD = true;
};

/** Body checks between skaters of different teams (FSkateHit). The check button (X without the ball)
 *  opens a short window: contact with an opponent inside it is a hit. */
USTRUCT(BlueprintType)
struct FSkateHitTuning
{
	GENERATED_BODY()

	/** After the check button the skater is "checking" this long (s): contact in that window hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float CheckWindow = 0.35f;

	/** The check button also lunges the skater forward: speed (cm/s) added along the heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float LungeSpeed = 220.f;

	/** The two skaters must close on each other at least this fast (cm/s, sum along the contact line). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float MinClosingSpeed = 150.f;

	/** The hitter must skate at least this fast (cm/s) ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float MinHitterSpeed = 220.f;

	/** ... and within this angle (deg) of the direction to the victim (a sideways brush is no check). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0", ClampMax = "90"))
	float FrontConeDeg = 60.f;

	/** Speed (cm/s) added to the victim along the contact line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float Push = 450.f;

	/** Fraction of its speed the hitter keeps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0", ClampMax = "1"))
	float HitterKeepsSpeed = 0.55f;

	/** The victim has no stick and no ball control for this long (s): a carried ball is knocked loose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float StunTime = 0.7f;

	/** No second check on either skater within this time (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0"))
	float Cooldown = 1.0f;
};

/** AI skaters (FSkateSkaterAI): the knobs that set the difficulty. */
USTRUCT(BlueprintType)
struct FSkateAITuning
{
	GENERATED_BODY()

	/** Boost (0..1, RT equivalent) the AI uses on long skates. The player's full sprint is 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0", ClampMax = "1"))
	float BoostAmount = 0.3f;

	/** Shoots from closer than this (cm to the goal line centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "100"))
	float ShootDistance = 1200.f;

	/** Holds the kick button this long (s). KickMaxChargeTime (0.8) = full power, which beats any keeper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float ShotCharge = 0.5f;

	/** Pressing the carrier: the AI presses the check button when the ball is closer than this (cm) and ahead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float CheckRange = 170.f;

	/** Random sideways error (cm) on the AI's shots: it aims at the corner but is not a machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float AimError = 90.f;

	/** The AI stays put this long (s) after the face-off drop: the player's reaction time, not a free ball for the bot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0"))
	float FaceOffReaction = 0.35f;
};

/** NHL-style side camera (ASkateCameraRig): high on the stands, looks across the rink, slides along it. */
USTRUCT(BlueprintType)
struct FSkateCameraTuning
{
	GENERATED_BODY()

	/** Fixed camera pitch (deg, negative = looking down). The camera never yaws with the skater. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "-89", ClampMax = "-20"))
	float Pitch = -40.f;

	/** Fixed camera yaw (deg). Stick "up" = this direction projected onto the ice.
	 *  -90: camera on the +Y stands looking across the rink, the goal (+X) is on screen right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Yaw = -90.f;

	/** Distance from the focus point (cm) when the skater and the interest point are close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "300"))
	float Distance = 2600.f;

	/** The camera pulls back up to this distance (cm) to keep the interest point in frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "300"))
	float MaxDistance = 6500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "20", ClampMax = "120"))
	float FieldOfView = 50.f;

	/** Interest point = the ball, or the teammate while this skater has the ball. The focus moves this
	 *  fraction of the way from the skater towards it (0 = skater only, 0.5 = midpoint, as in NHL). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "1"))
	float InterestWeight = 0.5f;

	/** The focus never leaves the skater by more than this (cm), however far the interest point is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float MaxInterestOffset = 1600.f;

	/** Smoothing time (s) of the interest offset and of the zoom (the ball / teammate change abruptly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.01"))
	float InterestSmoothTime = 0.6f;

	/** Fraction of the half-screen (from the centre) the skater and the interest point may reach before the camera pulls back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.2", ClampMax = "1"))
	float FrameFill = 0.75f;

	/** Extra field of view (deg) at full sprint speed, so a sprint reads as clearly faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "30"))
	float SprintFovKick = 5.f;

	/** Speed (cm/s) where the sprint FOV kick starts and where it is full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float SprintFovStartSpeed = 620.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1"))
	float SprintFovFullSpeed = 980.f;

	/** Look-ahead = velocity * this time (s), clamped to MaxLookAhead. Shows the space in front of the skater. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float LookAheadTime = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float MaxLookAhead = 200.f;

	/** Smoothing time (s) of the look-ahead offset only. The skater position itself is followed without lag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.01"))
	float LookAheadSmoothTime = 0.5f;

	/** Static diagnostic camera: pitch and distance, looking at the rink centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "-89", ClampMax = "-20"))
	float StaticPitch = -62.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "300"))
	float StaticDistance = 5200.f;
};

/** Procedural pose (USkaterPuppetComponent). Visual only, never feeds back into movement. */
USTRUCT(BlueprintType)
struct FSkateAnimTuning
{
	GENERATED_BODY()

	/** Lean angle (deg) per 1 g of lateral acceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0"))
	float LeanPerG = 32.f;

	/** Max sideways lean (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0", ClampMax = "45"))
	float MaxLean = 24.f;

	/** Forward lean while pushing (deg) and backward lean while braking (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0", ClampMax = "30"))
	float PushForwardLean = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0", ClampMax = "30"))
	float BrakeBackLean = 12.f;

	/** Lower-body twist in a hockey stop (deg): skates go across the travel direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0", ClampMax = "100"))
	float BrakeTwist = 78.f;

	/** Pose smoothing time (s). Short: the pose follows the motion, it must not lag behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0.01"))
	float PoseSmoothTime = 0.07f;

	/** Stride frequency (strokes/s, both legs) at low / top speed while pushing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0.1"))
	float StrideRateSlow = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0.1"))
	float StrideRateFast = 2.1f;

	/** Sideways push-out of the stroking skate (cm) at full thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim", meta = (ClampMin = "0"))
	float StrideWidth = 34.f;
};

/** Sound / trails / spray / rumble (USkateFeedbackComponent). */
USTRUCT(BlueprintType)
struct FSkateFeedbackTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0", ClampMax = "2"))
	float GlideVolume = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0", ClampMax = "2"))
	float BrakeVolume = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0", ClampMax = "2"))
	float ImpactVolume = 0.9f;

	/** Blade marks: spacing (cm) and lifetime (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "2"))
	float TrailSpacing = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0.5"))
	float TrailLifetime = 7.f;

	/** Ice spray starts when braking+skid deceleration exceeds this (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0"))
	float SprayDecelThreshold = 450.f;

	/** Controller rumble on kick (scaled by power) / push / dribble touch. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0", ClampMax = "1"))
	float KickRumble = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0", ClampMax = "1"))
	float PushRumble = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback", meta = (ClampMin = "0", ClampMax = "1"))
	float TouchRumble = 0.0f;
};

/** Goalkeeper (AI): positioning, reactions, dive and save rules. Owned by ASkateGoalkeeper. */
USTRUCT(BlueprintType)
struct FSkateKeeperTuning
{
	GENERATED_BODY()

	/** Challenge: with the ball loose or carried in front of the goal and no shot under way, the keeper comes out
	 *  up to ChallengeDepth (cm) as the ball closes from ChallengeFar to ChallengeNear (cm from the goal line),
	 *  at ChallengeSpeed (cm/s). Cuts the angle on close shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ChallengeDepth = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ChallengeNear = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ChallengeFar = 1300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ChallengeSpeed = 350.f;

	/** Butterfly: with the ball closer than ButterflyRange (cm) the keeper drops and covers low balls (bottom under
	 *  ButterflyMaxHeight cm) out to ButterflyReach (cm) each side without a dive. High close shots still score. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ButterflyRange = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ButterflyReach = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ButterflyMaxHeight = 70.f;

	/** The keeper stands this far (cm) in front of the goal line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float LineOffset = 55.f;

	/** Sideways shuffle along the goal: top speed (cm/s) and acceleration (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float MaxShuffleSpeed = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ShuffleAccel = 3000.f;

	/** Time (s) between a shot and the keeper's first reaction to it. Lower = harder to score. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ReactionTime = 0.15f;

	/** Half width of the body (cm): always covered while standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float BodyHalfWidth = 28.f;

	/** Sideways hand reach while standing (cm from the keeper's centre) and how high the hands reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float StandReach = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float StandReachHeight = 205.f;

	/** Extra sideways reach (cm) of a full dive, the time (s) to get there and to get up again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float DiveReach = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0.05"))
	float DiveTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float DiveRecoverTime = 0.8f;

	/** How high (cm) the hands still reach at the far end of a full dive: high corners beat a diving keeper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float DiveLowHeight = 95.f;

	/** Balls slower than this (cm/s) close to the body are caught and held instead of parried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float CatchMaxSpeed = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float CatchHalfWidth = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float CatchMaxHeight = 170.f;

	/** Parry: fraction of the shot speed sent back out, sideways speed (cm/s) away from the goal, lift (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0", ClampMax = "1"))
	float ParryRestitution = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ParryWideSpeed = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ParryLift = 220.f;

	/** A caught ball is held this long (s), then rolled out to the controlled skater at ThrowSpeed (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float HoldTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "0"))
	float ThrowSpeed = 1100.f;

	/** Gravity used to predict lofted shots (cm/s^2, UE default 980). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper", meta = (ClampMin = "1"))
	float Gravity = 980.f;
};

/** The complete tuning set. One preset = one FSkateTuning. */
USTRUCT(BlueprintType)
struct FSkateTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateInputTuning Input;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateMovementTuning Movement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateBallControlTuning BallControl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateBallPhysicsTuning BallPhysics;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate|Tuning")
	FSkateHitTuning Hit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate|Tuning")
	FSkateAITuning AI;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateCameraTuning Camera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateAnimTuning Anim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")
	FSkateFeedbackTuning Feedback;
};

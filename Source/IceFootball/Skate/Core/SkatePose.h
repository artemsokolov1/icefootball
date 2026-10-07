// Ice skating prototype - procedural skater pose (engine independent).
//
// There are no skating animation clips, so the placeholder skater is posed procedurally
// from the REAL movement state (never the other way round: the pose has no effect on motion):
//   stance       - knees bent, blades parallel, arms relaxed with bent elbows
//   push stroke  - alternate legs push out sideways-back (V stroke), arms swing against the legs
//   glide        - feet together, no stride
//   carve        - body leans into the turn from lateral acceleration, blades edge
//   brake        - lower body twists so both skates are across the travel (hockey stop),
//                  lean back, arms open for balance
//   touch / push / kick - the playing leg (left or right, chosen by the ball control) taps or swings
//                  (kick: wind-up while charging, swing on release); dribble taps alternate feet
//   check        - shoulder forward, arms driven in front, low (the body check lunge)
//   stagger      - knocked: leans back and low, arms out for balance
// Legs and arms are 2-bone chains (IK for the legs, so the blades stay on the ice).
// Output is in skater-local space: X forward, Y right, Z up, origin on the ice under the capsule.
#pragma once

#include "SkateBallControl.h"
#include "SkateMath.h"
#include "SkateTuning.h"

/** Body proportions of the placeholder skater (~185 cm with skates), cm. */
namespace SkateBody
{
	constexpr float AnkleHeight = 10.f;      // ankle above the ice with the blade on it
	constexpr float ShinLength = 46.f;
	constexpr float ThighLength = 47.f;
	constexpr float HipHeight = 98.f;        // pelvis height standing tall (legs not fully straight)
	constexpr float HipHalfWidth = 11.f;
	constexpr float StanceHalfWidth = 14.f;
	constexpr float TorsoLength = 50.f;      // pelvis -> shoulder line
	constexpr float ShoulderHalfWidth = 21.f;
	constexpr float UpperArmLength = 31.f;
	constexpr float ForearmLength = 28.f;
	constexpr float NeckLength = 7.f;
	constexpr float HeadRadius = 11.5f;

	// Mesh thicknesses (diameters / box sizes) used by the UE puppet.
	constexpr float ThighThickness = 17.f;
	constexpr float ShinThickness = 13.f;
	constexpr float UpperArmThickness = 11.f;
	constexpr float ForearmThickness = 9.5f;
	constexpr float HandSize = 10.f;
	constexpr float ChestWidth = 38.f;
	constexpr float ChestDepth = 24.f;
}

struct FSkatePoseInput
{
	float SpeedRatio = 0.f;     // |velocity| / MaxSpeed
	float PushAmount = 0.f;     // FSkateMoveState::PushAmount
	float ThrustAccel = 0.f;    // cm/s^2
	float BrakeAmount = 0.f;    // 0..1 actual braking deceleration / BrakeDecel
	float SkidAmount = 0.f;     // 0..1 skid (scrub) deceleration, normalised
	float LateralAccel = 0.f;   // cm/s^2, + = to the right
	float SlideSide = 1.f;      // sign of cross(heading, velocity): which side the hips turn to in a hockey stop
	float KickCharge = 0.f;     // 0..1 while X is held
	int ChargeFoot = 1;         // leg that winds up (0 = left, 1 = right)
	ESkateImpulseKind SwingKind = ESkateImpulseKind::None;
	int SwingFoot = 1;          // leg that taps / swings (0 = left, 1 = right)
	float SwingTime = 100.f;    // s since the last touch / push / kick (or whiff)
	float SwingPower = 0.f;
	float CheckAlpha = 0.f;     // 1 while the check button's window is open
	float StunAlpha = 0.f;      // 1 while knocked by a check
};

struct FSkatePose
{
	FSkateVec3 Pelvis;
	FSkateVec3 Chest;           // centre of the shoulder line
	FSkateVec3 Head;
	FSkateVec3 TorsoUp = FSkateVec3(0.f, 0.f, 1.f);
	FSkateVec3 TorsoForward = FSkateVec3(1.f, 0.f, 0.f);
	FSkateVec3 TorsoRight = FSkateVec3(0.f, 1.f, 0.f);
	float PelvisYawDeg = 0.f;

	// [0] = left, [1] = right
	FSkateVec3 Shoulder[2];
	FSkateVec3 Elbow[2];
	FSkateVec3 Hand[2];
	FSkateVec3 Hip[2];
	FSkateVec3 Knee[2];
	FSkateVec3 Ankle[2];
	float FootYawDeg[2] = { 0.f, 0.f };
	float FootLift[2] = { 0.f, 0.f };
	float EdgeRollDeg = 0.f;
	FSkateVec3 BladeContact[2]; // blade centre on the ice

	float LeanRightDeg = 0.f;
	float TwistDeg = 0.f;
	const char* Label = "Stance";
};

/** Smoothed pose state carried between frames. */
struct FSkatePoseState
{
	float LeanRight = 0.f;
	float LeanForward = 0.f;
	float Crouch = 8.f;
	float Twist = 0.f;
	float TwistSign = 1.f;
	float Brake = 0.f;
	float StrideAmp = 0.f;
	float StridePhase = 0.f;
	float Charge[2] = { 0.f, 0.f }; // wind-up per leg [left, right]
	float ArmOpen = 0.f;
	float Check = 0.f;
	float Stun = 0.f;
};

class FSkatePoseSolver
{
public:
	static FSkatePose Update(const FSkateAnimTuning& Tuning, const FSkatePoseInput& Input, float Dt, FSkatePoseState& State);

	/** Two-bone chain: knee/elbow position for Root -> End with the bend towards Pole.
	 *  End is pulled in when out of reach (never stretches the limb). */
	static FSkateVec3 SolveTwoBone(const FSkateVec3& Root, FSkateVec3& End, float UpperLen, float LowerLen, const FSkateVec3& Pole);

	static constexpr float KickSwingTime = 0.26f;
	static constexpr float PushSwingTime = 0.18f;
	static constexpr float TouchSwingTime = 0.3f;
};

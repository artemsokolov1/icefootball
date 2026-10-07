#include "SkatePoseTests.h"

#include "../Core/SkateModel.h"
#include "../Core/SkatePose.h"
#include "../Core/SkateTuningPresets.h"

#include <cstdarg>
#include <cstdio>

namespace SkatePoseTestsDetail
{
	std::string Fmt(const char* Format, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Format);
		std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);
		return std::string(Buffer);
	}

	struct FPoseCase
	{
		const char* Name;
		FSkatePoseInput Input;
	};

	std::vector<FPoseCase> Cases()
	{
		std::vector<FPoseCase> C;
		C.push_back({ "stance", FSkatePoseInput() });
		FSkatePoseInput Push; Push.SpeedRatio = 0.6f; Push.PushAmount = 1.f; Push.ThrustAccel = 900.f;
		C.push_back({ "push", Push });
		FSkatePoseInput Glide; Glide.SpeedRatio = 1.f;
		C.push_back({ "glide", Glide });
		FSkatePoseInput CarveL; CarveL.SpeedRatio = 1.f; CarveL.LateralAccel = -1500.f; CarveL.PushAmount = 1.f; CarveL.ThrustAccel = 300.f;
		C.push_back({ "carve-left", CarveL });
		FSkatePoseInput CarveR = CarveL; CarveR.LateralAccel = 1500.f;
		C.push_back({ "carve-right", CarveR });
		FSkatePoseInput Brake; Brake.SpeedRatio = 0.8f; Brake.BrakeAmount = 1.f;
		C.push_back({ "brake", Brake });
		FSkatePoseInput Wind; Wind.KickCharge = 1.f;
		C.push_back({ "kick-windup", Wind });
		return C;
	}

	float Len(const FSkateVec3& A, const FSkateVec3& B) { return (A - B).Size(); }
}

void RunSkatePoseTests(std::vector<FSkateTestResult>& Out)
{
	using namespace SkatePoseTestsDetail;
	const FSkateTuning T = SkateTuningPresets::Make(ESkatePreset::Balanced);
	const float Dt = 1.f / 60.f;

	// Run every case for 3 s and check every frame.
	float WorstBladeOffIce = 0.f;     // planted blade (no lift) height error, cm
	float WorstLimbError = 0.f;       // |segment length - nominal| for thigh, shin, upper arm, forearm
	float WorstHandInTorso = 1e9f;    // lateral distance of hands from the torso axis (must clear the chest)
	float LowestHeadOverChest = 1e9f;
	float WorstKneeBackwards = 1e9f;  // knee in front of the hip->ankle line (>= 0 = bends the right way)
	float MinArmReachRatio = 1e9f;    // hand-to-shoulder vs. torso length: arms must look long enough
	std::string Worst;
	for (const FPoseCase& Case : Cases())
	{
		FSkatePoseState State;
		for (int Frame = 0; Frame < 180; ++Frame)
		{
			const FSkatePose P = FSkatePoseSolver::Update(T.Anim, Case.Input, Dt, State);
			for (int Side = 0; Side < 2; ++Side)
			{
				if (P.FootLift[Side] < 0.01f)
				{
					const float Err = SkateMath::Abs(P.Ankle[Side].Z - SkateBody::AnkleHeight);
					if (Err > WorstBladeOffIce) { WorstBladeOffIce = Err; Worst = Case.Name; }
				}
				WorstLimbError = SkateMath::Max(WorstLimbError, SkateMath::Abs(Len(P.Hip[Side], P.Knee[Side]) - SkateBody::ThighLength));
				WorstLimbError = SkateMath::Max(WorstLimbError, SkateMath::Abs(Len(P.Knee[Side], P.Ankle[Side]) - SkateBody::ShinLength));
				WorstLimbError = SkateMath::Max(WorstLimbError, SkateMath::Abs(Len(P.Shoulder[Side], P.Elbow[Side]) - SkateBody::UpperArmLength));
				WorstLimbError = SkateMath::Max(WorstLimbError, SkateMath::Abs(Len(P.Elbow[Side], P.Hand[Side]) - SkateBody::ForearmLength));

				// Hand position relative to the torso: lateral clearance from the body centre line.
				const FSkateVec3 RelHand = P.Hand[Side] - P.Pelvis;
				const float Lateral = SkateMath::Abs(RelHand.Dot(P.TorsoRight));
				const float Depth = SkateMath::Abs(RelHand.Dot(P.TorsoForward));
				// Inside the chest box footprint only counts when also within its depth.
				if (Depth < SkateBody::ChestDepth * 0.5f) { WorstHandInTorso = SkateMath::Min(WorstHandInTorso, Lateral - SkateBody::ChestWidth * 0.5f); }

				// Knee must be on the toe side of the hip->ankle line (no backwards knee).
				const FSkateVec3 HipToAnkle = (P.Ankle[Side] - P.Hip[Side]).GetSafeNormal();
				const FSkateVec3 HipToKnee = P.Knee[Side] - P.Hip[Side];
				const FSkateVec3 Offset = HipToKnee - HipToAnkle * HipToKnee.Dot(HipToAnkle);
				const FSkateVec3 FootFwd = FSkateVec3(1.f, 0.f, 0.f).RotatedZ(P.FootYawDeg[Side]);
				WorstKneeBackwards = SkateMath::Min(WorstKneeBackwards, Offset.Dot(FootFwd));

				MinArmReachRatio = SkateMath::Min(MinArmReachRatio, Len(P.Shoulder[Side], P.Hand[Side]) / SkateBody::TorsoLength);
			}
			LowestHeadOverChest = SkateMath::Min(LowestHeadOverChest, (P.Head - P.Chest).Dot(FSkateVec3(0.f, 0.f, 1.f)));
		}
	}

	FSkateTestResult R("Pose.Anatomy");
	R.bPassed = WorstBladeOffIce < 0.6f && WorstLimbError < 0.6f && WorstHandInTorso > 0.f && LowestHeadOverChest > 10.f
		&& WorstKneeBackwards > -0.5f && MinArmReachRatio > 0.9f;
	R.Details = Fmt("7 poses x 3 s: planted blade height error %.2f cm (%s), limb length error %.2f cm, hands clear the chest by >= %.1f cm, head >= %.0f cm above the shoulders, knee bend direction %.1f (>=0 ok), arm reach / torso >= %.2f",
		WorstBladeOffIce, Worst.c_str(), WorstLimbError, WorstHandInTorso, LowestHeadOverChest, WorstKneeBackwards, MinArmReachRatio);
	Out.push_back(R);

	// Lean follows the lateral acceleration sign and the hockey stop turns the blades across.
	{
		FSkateTestResult L("Pose.LeanAndBrakeFollowMotion");
		FSkatePoseState SL;
		FSkatePoseState SR;
		FSkatePoseState SB;
		FSkatePose PL, PR, PB;
		std::vector<FPoseCase> C = Cases();
		for (int Frame = 0; Frame < 120; ++Frame)
		{
			PL = FSkatePoseSolver::Update(T.Anim, C[3].Input, Dt, SL);
			PR = FSkatePoseSolver::Update(T.Anim, C[4].Input, Dt, SR);
			PB = FSkatePoseSolver::Update(T.Anim, C[5].Input, Dt, SB);
		}
		const float BladeAcross = SkateMath::Abs(PB.FootYawDeg[0]);
		L.bPassed = PL.LeanRightDeg < -10.f && PR.LeanRightDeg > 10.f && PL.Chest.Y < 0.f && PR.Chest.Y > 0.f
			&& SkateMath::Abs(PL.LeanRightDeg) <= T.Anim.MaxLean + 0.01f && BladeAcross > 60.f && BladeAcross < 100.f;
		L.Details = Fmt("carve left lean %.1f deg (chest y %.0f), carve right %.1f deg (chest y %.0f), max %.0f; hockey stop blade yaw %.0f deg",
			PL.LeanRightDeg, PL.Chest.Y, PR.LeanRightDeg, PR.Chest.Y, T.Anim.MaxLean, BladeAcross);
		Out.push_back(L);
	}
	// Full chain as in the game: skating model + ball possession + pose, stick spun around with the ball.
	// Feet must move smoothly (no per-frame jumps), the lean must not flip, the hips must not flick sides.
	{
		FSkateTestResult J("Pose.SmoothWhileSpinningWithBall");
		std::string Info;
		bool bOk = true;
		for (float SpinDeg : { 0.f, 180.f, 360.f, 720.f, -540.f })
		{
			for (float Fps : { 30.f, 60.f })
			{
				const float Step = 1.f / Fps;
				FSkateMoveState Move;
				Move.Velocity = FSkateVec2(T.Movement.MaxSpeed, 0.f);
				FSkateVec2 Pos;
				FSkateVec3 BallPos(FSkateVec2(42.f, 0.f), T.BallPhysics.Radius);
				FSkateVec3 BallVel;
				FSkateBallControlState Control;
				FSkateContactReport Report;
				FSkatePoseState PoseState;
				float SwingTime = 100.f;
				ESkateImpulseKind SwingKind = ESkateImpulseKind::None;
				FSkateVec3 PrevAnkle[2];
				float PrevLean = 0.f;
				float MaxFootSpeed = 0.f;  // cm/s of an ankle relative to the skater
				float MaxLeanRate = 0.f;   // deg/s
				int LeanSignFlips = 0;
				int TwistFlips = 0;
				float PrevTwistSign = PoseState.TwistSign;
				for (float Time = 0.f; Time < 8.f; Time += Step)
				{
					FSkateMoveInput In;
					In.Direction = Time < 2.f ? FSkateVec2(1.f, 0.f) : FSkateVec2::FromYaw(SpinDeg * SkateMath::DegToRad * (Time - 2.f));
					In.Magnitude = 1.f;
					FSkateModel::Step(T.Movement, In, Step, Move);
					Pos += Move.Velocity * Step;

					FSkateContactQuery Q;
					Q.SkaterPos = Pos;
					Q.SkaterVel = Move.Velocity;
					Q.Heading = Move.Heading;
					Q.SkaterMaxSpeed = T.Movement.MaxSpeed;
					Q.StickDir = In.Direction;
					Q.StickMag = In.Magnitude;
					Q.bHasBall = true;
					Q.BallPos = BallPos;
					Q.BallVel = BallVel;
					Q.BallRadius = T.BallPhysics.Radius;
					const int TapsBefore = Control.Possession.TouchPulseCount;
					FSkateBallCarry Carry;
					const FSkateBallImpulse Imp = FSkateBallControl::Update(T.BallControl, Q, FSkateBallActionInput(), Step, Control, Report, &Carry);
					BallVel = Imp.IsValid() ? Imp.NewBallVelocity : (Carry.bActive ? Carry.Velocity : BallVel);
					BallPos = BallPos + BallVel * Step;
					SwingTime += Step;
					if (Control.Possession.TouchPulseCount != TapsBefore) { SwingTime = 0.f; SwingKind = ESkateImpulseKind::Touch; }

					FSkatePoseInput PI;
					PI.SpeedRatio = Move.Velocity.Size() / T.Movement.MaxSpeed;
					PI.PushAmount = Move.PushAmount;
					PI.ThrustAccel = Move.ThrustAccel;
					PI.BrakeAmount = SkateMath::Clamp01(Move.BrakeDecel / T.Movement.BrakeDecel);
					PI.SkidAmount = SkateMath::Clamp01(Move.ScrubDecel / 900.f);
					PI.LateralAccel = Move.LateralAccel;
					PI.SlideSide = Move.Heading.Cross(Move.Velocity) >= 0.f ? 1.f : -1.f;
					PI.SwingKind = SwingKind;
					PI.SwingTime = SwingTime;
					const FSkatePose P = FSkatePoseSolver::Update(T.Anim, PI, Step, PoseState);
					if (Time > 3.f)
					{
						for (int Side = 0; Side < 2; ++Side)
						{
							MaxFootSpeed = SkateMath::Max(MaxFootSpeed, (P.Ankle[Side] - PrevAnkle[Side]).Size() / Step);
						}
						MaxLeanRate = SkateMath::Max(MaxLeanRate, SkateMath::Abs(PoseState.LeanRight - PrevLean) / Step);
						LeanSignFlips += (SpinDeg != 0.f && PoseState.LeanRight * PrevLean < 0.f) ? 1 : 0;
						TwistFlips += PoseState.TwistSign != PrevTwistSign ? 1 : 0;
					}
					PrevAnkle[0] = P.Ankle[0];
					PrevAnkle[1] = P.Ankle[1];
					PrevLean = PoseState.LeanRight;
					PrevTwistSign = PoseState.TwistSign;
				}
				// A push stroke moves a foot ~110 cm/s and a soft dribble tap ~170 cm/s; 250 cm/s means a visible jerk.
				const bool bCase = MaxFootSpeed < 250.f && LeanSignFlips == 0 && TwistFlips == 0 && MaxLeanRate < 200.f && Control.Possession.bPossessed;
				bOk &= bCase;
				if (Fps == 60.f || !bCase)
				{
					Info += Fmt("%+.0f deg/s@%.0f: foot %.0f cm/s, lean rate %.0f deg/s, lean flips %d, hip flips %d%s; ",
						SpinDeg, Fps, MaxFootSpeed, MaxLeanRate, LeanSignFlips, TwistFlips, Control.Possession.bPossessed ? "" : " LOST BALL");
				}
			}
		}
		J.bPassed = bOk;
		J.Details = Info;
		Out.push_back(J);
	}
}


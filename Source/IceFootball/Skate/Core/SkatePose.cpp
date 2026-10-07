#include "SkatePose.h"

namespace SkatePoseDetail
{
	constexpr float StrideBack = 16.f;     // push foot travels back this far at full stride
	constexpr float RecoveryLift = 8.f;

	inline float Approach(float Current, float Target, float Dt, float SmoothTime)
	{
		return Current + (Target - Current) * SkateMath::DecayAlpha(1.f / SkateMath::Max(SmoothTime, 0.001f), Dt);
	}

	inline float EaseOut(float T)
	{
		T = SkateMath::Clamp01(T);
		return 1.f - (1.f - T) * (1.f - T);
	}

	// Direction of a limb hanging from a joint: Swing (+ forward) and Abduct (+ outward), degrees.
	inline FSkateVec3 HangDirection(const FSkateVec3& Down, const FSkateVec3& Forward, const FSkateVec3& Out, float SwingDeg, float AbductDeg)
	{
		const float S = SwingDeg * SkateMath::DegToRad;
		const float A = AbductDeg * SkateMath::DegToRad;
		return (Down * (std::cos(S) * std::cos(A)) + Forward * (std::sin(S) * std::cos(A)) + Out * std::sin(A)).GetSafeNormal(Down);
	}

	// Bends Dir by AngleDeg towards Towards (component orthogonal to Dir).
	inline FSkateVec3 Bend(const FSkateVec3& Dir, const FSkateVec3& Towards, float AngleDeg)
	{
		const FSkateVec3 Perp = (Towards - Dir * Dir.Dot(Towards)).GetSafeNormal(FSkateVec3(1.f, 0.f, 0.f));
		const float A = AngleDeg * SkateMath::DegToRad;
		return (Dir * std::cos(A) + Perp * std::sin(A)).GetSafeNormal(Dir);
	}
}

FSkateVec3 FSkatePoseSolver::SolveTwoBone(const FSkateVec3& Root, FSkateVec3& End, float UpperLen, float LowerLen, const FSkateVec3& Pole)
{
	const FSkateVec3 ToEnd = End - Root;
	const float MaxReach = UpperLen + LowerLen - 0.5f;
	const float MinReach = SkateMath::Abs(UpperLen - LowerLen) + 1.f;
	float Dist = ToEnd.Size();
	const FSkateVec3 Dir = ToEnd.GetSafeNormal(FSkateVec3(0.f, 0.f, -1.f));
	if (Dist > MaxReach || Dist < MinReach)
	{
		Dist = SkateMath::Clamp(Dist, MinReach, MaxReach);
		End = Root + Dir * Dist;
	}
	const float A = (UpperLen * UpperLen - LowerLen * LowerLen + Dist * Dist) / (2.f * Dist);
	const float H = std::sqrt(SkateMath::Max(UpperLen * UpperLen - A * A, 0.f));
	const FSkateVec3 PoleDir = (Pole - Dir * Pole.Dot(Dir)).GetSafeNormal(FSkateVec3(1.f, 0.f, 0.f));
	return Root + Dir * A + PoleDir * H;
}

FSkatePose FSkatePoseSolver::Update(const FSkateAnimTuning& Tuning, const FSkatePoseInput& In, float Dt, FSkatePoseState& S)
{
	using namespace SkatePoseDetail;
	FSkatePose Pose;

	// ---- Smoothed layers from the real motion ----
	// Small skids during carving must not trigger the hockey-stop pose (it would flicker); only real skids do.
	const float SkidPose = SkateMath::Clamp01((In.SkidAmount - 0.25f) / 0.75f);
	const float BrakeTarget = SkateMath::Clamp01(SkateMath::Max(In.BrakeAmount, SkidPose * 0.6f));
	const float PushTarget = In.PushAmount > 0.05f ? In.PushAmount * SkateMath::Clamp(In.ThrustAccel / 400.f, 0.35f, 1.f) : 0.f;
	const float LeanTarget = SkateMath::Clamp(In.LateralAccel / 980.f * Tuning.LeanPerG, -Tuning.MaxLean, Tuning.MaxLean);
	const bool bKickSwing = In.SwingKind == ESkateImpulseKind::Kick && In.SwingTime < KickSwingTime;

	if (S.Brake < 0.02f && BrakeTarget > 0.f)
	{
		S.TwistSign = In.SlideSide >= 0.f ? 1.f : -1.f; // chosen once, when a stop begins
	}
	const float SpeedRatio = SkateMath::Clamp(In.SpeedRatio, 0.f, 1.4f);
	S.Brake = Approach(S.Brake, BrakeTarget, Dt, Tuning.PoseSmoothTime);
	S.StrideAmp = Approach(S.StrideAmp, PushTarget * (1.f - S.Brake), Dt, 0.12f);
	S.LeanRight = Approach(S.LeanRight, LeanTarget, Dt, Tuning.PoseSmoothTime);
	S.LeanForward = Approach(S.LeanForward, PushTarget * Tuning.PushForwardLean - S.Brake * Tuning.BrakeBackLean + In.KickCharge * 6.f, Dt, Tuning.PoseSmoothTime);
	S.Crouch = Approach(S.Crouch, 8.f + 10.f * PushTarget + 14.f * S.Brake + 4.f * SkateMath::Min(SpeedRatio, 1.f) + 6.f * In.KickCharge, Dt, Tuning.PoseSmoothTime);
	S.Twist = Approach(S.Twist, S.Brake * Tuning.BrakeTwist * S.TwistSign, Dt, Tuning.PoseSmoothTime);
	// The kick swing starts from the wind-up itself, so the wind-up layer is dropped on release.
	for (int Side = 0; Side < 2; ++Side)
	{
		const float Target = Side == In.ChargeFoot ? In.KickCharge : 0.f;
		S.Charge[Side] = bKickSwing && Side == In.SwingFoot ? 0.f : Approach(S.Charge[Side], Target, Dt, 0.04f);
	}
	S.ArmOpen = Approach(S.ArmOpen, SkateMath::Max(S.Brake, bKickSwing ? 0.6f : 0.f), Dt, 0.1f);

	const float StrideRate = SkateMath::Lerp(Tuning.StrideRateSlow, Tuning.StrideRateFast, SkateMath::Min(SpeedRatio, 1.f));
	if (S.StrideAmp > 0.02f)
	{
		S.StridePhase += Dt * StrideRate * 0.5f;
		S.StridePhase -= std::floor(S.StridePhase);
	}

	// ---- Body frame: pelvis leans into the turn, feet stay on the ice ----
	const float PelvisHeight = SkateBody::HipHeight - S.Crouch;
	const FSkateVec3 Up = FSkateVec3(std::tan(S.LeanForward * SkateMath::DegToRad), std::tan(S.LeanRight * SkateMath::DegToRad), 1.f).GetSafeNormal();
	Pose.Pelvis = Up * PelvisHeight;
	Pose.TorsoUp = (Up + FSkateVec3(0.25f * S.StrideAmp, 0.f, 0.f)).GetSafeNormal();
	// The chest faces the travel direction; it follows the hip twist only a little (counter-rotation).
	const FSkateVec3 ChestYawFwd = FSkateVec3(1.f, 0.f, 0.f).RotatedZ(S.Twist * 0.2f);
	Pose.TorsoForward = (ChestYawFwd - Pose.TorsoUp * ChestYawFwd.Dot(Pose.TorsoUp)).GetSafeNormal(ChestYawFwd);
	Pose.TorsoRight = Pose.TorsoUp.Cross(Pose.TorsoForward).GetSafeNormal(FSkateVec3(0.f, 1.f, 0.f));
	Pose.Chest = Pose.Pelvis + Pose.TorsoUp * SkateBody::TorsoLength;
	const FSkateVec3 HeadUp = (Pose.TorsoUp * 0.6f + FSkateVec3(0.f, 0.f, 0.4f)).GetSafeNormal();
	Pose.Head = Pose.Chest + HeadUp * (SkateBody::NeckLength + SkateBody::HeadRadius) + Pose.TorsoForward * 2.f;
	Pose.PelvisYawDeg = S.Twist * 0.8f;
	Pose.LeanRightDeg = S.LeanRight;
	Pose.TwistDeg = S.Twist;
	Pose.EdgeRollDeg = S.LeanRight * 0.7f;

	// ---- Ball layer of the playing leg (touch / push / kick) ----
	float SwingX = 0.f;
	float SwingLift = 0.f;
	{
		const float T = In.SwingTime;
		switch (In.SwingKind)
		{
		case ESkateImpulseKind::Kick:
			if (T < KickSwingTime)
			{
				const float U = T / KickSwingTime;
				const float Forward = U < 0.3f ? EaseOut(U / 0.3f) : 1.f - SkateMath::SmoothStep01((U - 0.3f) / 0.7f);
				SwingX = SkateMath::Lerp(-38.f * In.SwingPower, 55.f, Forward) * (U < 0.3f ? 1.f : Forward);
				SwingLift = 16.f * std::sin(SkateMath::Pi * U);
			}
			break;
		case ESkateImpulseKind::Push:
			if (T < PushSwingTime)
			{
				const float U = T / PushSwingTime;
				SwingX = 40.f * std::sin(SkateMath::Pi * U);
				SwingLift = 4.f * std::sin(SkateMath::Pi * U);
			}
			break;
		case ESkateImpulseKind::Touch:
			if (T < TouchSwingTime)
			{
				// Soft dribble tap: sin^2 profile (starts and ends at rest), smaller in hard turns
				// where the legs are busy carving.
				const float U = T / TouchSwingTime;
				const float Shape = std::sin(SkateMath::Pi * U) * std::sin(SkateMath::Pi * U);
				const float TurnFade = 1.f - 0.8f * SkateMath::Clamp01(SkateMath::Abs(S.LeanRight) / SkateMath::Max(Tuning.MaxLean, 1.f));
				SwingX = 16.f * Shape * TurnFade;
				SwingLift = 2.f * Shape * TurnFade;
			}
			break;
		default:
			break;
		}
	}

	// ---- Legs ----
	float PushSignal[2] = { 0.f, 0.f };
	for (int Side = 0; Side < 2; ++Side)
	{
		const float Sgn = Side == 0 ? -1.f : 1.f;
		float LegPhase = S.StridePhase + (Side == 1 ? 0.5f : 0.f);
		LegPhase -= std::floor(LegPhase);
		PushSignal[Side] = std::sin(2.f * SkateMath::Pi * LegPhase) * S.StrideAmp; // > 0 while this leg pushes

		// Stride layer: push out sideways-back with the toe turned out, recover with a small lift.
		FSkateVec3 Foot(Sgn * 2.f, Sgn * SkateBody::StanceHalfWidth, 0.f);
		float Lift = 0.f;
		float Yaw = 0.f;
		if (LegPhase < 0.5f)
		{
			const float Out = SkateMath::SmoothStep01(LegPhase / 0.5f);
			Foot.Y += Sgn * Tuning.StrideWidth * S.StrideAmp * Out;
			Foot.X -= StrideBack * S.StrideAmp * Out;
			Yaw = Sgn * 30.f * S.StrideAmp;
		}
		else
		{
			const float U = (LegPhase - 0.5f) / 0.5f;
			const float Back = 1.f - SkateMath::SmoothStep01(U);
			Foot.Y += Sgn * Tuning.StrideWidth * S.StrideAmp * Back;
			Foot.X += -StrideBack * S.StrideAmp * Back + 10.f * S.StrideAmp * std::sin(SkateMath::Pi * U);
			Lift = RecoveryLift * S.StrideAmp * std::sin(SkateMath::Pi * U);
			Yaw = Sgn * 30.f * S.StrideAmp * Back;
		}

		// Brake layer: feet side by side across the travel direction (rotated by the hip twist below).
		Foot = FSkateVec3::Lerp(Foot, FSkateVec3(0.f, Sgn * 20.f, 0.f), S.Brake);
		Yaw = SkateMath::Lerp(Yaw, 0.f, S.Brake);
		Lift *= 1.f - S.Brake;

		// Ball layer: wind-up while charging, swing / tap of the playing foot after a contact.
		Foot.X += -38.f * S.Charge[Side];
		Lift += 14.f * S.Charge[Side];
		if (Side == In.SwingFoot)
		{
			Foot.X += SwingX;
			Lift += SwingLift;
		}

		Foot = Foot.RotatedZ(S.Twist);
		Yaw += S.Twist;

		FSkateVec3 Ankle(Foot.X, Foot.Y, SkateBody::AnkleHeight + Lift);
		const FSkateVec3 Hip = Pose.Pelvis + FSkateVec3(0.f, Sgn * SkateBody::HipHalfWidth, -6.f).RotatedZ(Pose.PelvisYawDeg);
		const FSkateVec3 Pole = FSkateVec3(1.f, 0.f, 0.f).RotatedZ(Yaw); // knees bend forward over the toes
		Pose.Knee[Side] = SolveTwoBone(Hip, Ankle, SkateBody::ThighLength, SkateBody::ShinLength, Pole);
		Pose.Hip[Side] = Hip;
		Pose.Ankle[Side] = Ankle;
		Pose.FootYawDeg[Side] = Yaw;
		Pose.FootLift[Side] = Ankle.Z - SkateBody::AnkleHeight;
		Pose.BladeContact[Side] = FSkateVec3(Ankle.X, Ankle.Y, 0.f) + FSkateVec3(2.f, 0.f, 0.f).RotatedZ(Yaw);
	}

	// ---- Arms: hang from the shoulders, elbows bent, swing against the legs, open up for balance ----
	const FSkateVec3 Down = -Pose.TorsoUp;
	for (int Side = 0; Side < 2; ++Side)
	{
		const float Sgn = Side == 0 ? -1.f : 1.f;
		const FSkateVec3 Out = Pose.TorsoRight * Sgn;
		Pose.Shoulder[Side] = Pose.Chest - Pose.TorsoUp * 3.f + Out * SkateBody::ShoulderHalfWidth;

		// Same-side arm goes back and out while that leg pushes (skating arm swing).
		float SwingDeg = 12.f - 38.f * PushSignal[Side];
		float AbductDeg = 14.f + 12.f * SkateMath::Max(PushSignal[Side], 0.f);
		float ElbowDeg = 28.f + 18.f * S.StrideAmp;

		// Balance: the arm on the outside of a lean opens up.
		AbductDeg += SkateMath::Max(0.f, -Sgn * S.LeanRight) * 0.9f;
		// Hockey stop / kick follow-through: both arms open forward-out.
		SwingDeg = SkateMath::Lerp(SwingDeg, 25.f, S.ArmOpen);
		AbductDeg += 38.f * S.ArmOpen;
		ElbowDeg = SkateMath::Lerp(ElbowDeg, 18.f, S.ArmOpen);
		// Kick wind-up: counter-balance, the arm opposite the kicking leg forward, the same-side arm back, both out.
		SwingDeg += S.Charge[1] * (Side == 0 ? 30.f : -25.f) + S.Charge[0] * (Side == 1 ? 30.f : -25.f);
		AbductDeg += 25.f * (S.Charge[0] + S.Charge[1]);

		const FSkateVec3 UpperDir = HangDirection(Down, Pose.TorsoForward, Out, SwingDeg, AbductDeg);
		Pose.Elbow[Side] = Pose.Shoulder[Side] + UpperDir * SkateBody::UpperArmLength;
		const FSkateVec3 ForeDir = Bend(UpperDir, Pose.TorsoForward + Pose.TorsoUp * 0.2f, ElbowDeg);
		Pose.Hand[Side] = Pose.Elbow[Side] + ForeDir * SkateBody::ForearmLength;
	}

	// ---- Label for the HUD ----
	const float SwingTime = In.SwingTime;
	if (bKickSwing) { Pose.Label = "Kick swing"; }
	else if (S.Charge[0] + S.Charge[1] > 0.05f) { Pose.Label = "Kick wind-up"; }
	else if (In.SwingKind != ESkateImpulseKind::None && In.SwingKind != ESkateImpulseKind::Kick && SwingTime < PushSwingTime) { Pose.Label = "Touch / push"; }
	else if (S.Brake > 0.3f) { Pose.Label = "Brake (blades across)"; }
	else if (S.StrideAmp > 0.15f) { Pose.Label = SkateMath::Abs(S.LeanRight) > 6.f ? "Push + lean" : "Push stroke"; }
	else if (SkateMath::Abs(S.LeanRight) > 6.f) { Pose.Label = "Carve (lean)"; }
	else if (SpeedRatio > 0.04f) { Pose.Label = "Glide"; }
	else { Pose.Label = "Stance"; }
	return Pose;
}

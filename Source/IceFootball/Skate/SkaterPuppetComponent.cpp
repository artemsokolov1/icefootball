#include "Skate/SkaterPuppetComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Skate/SkateBallControlComponent.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateMovementComponent.h"
#include "Skate/SkateVisuals.h"

namespace SkatePuppetDetail
{
	// Body proportions (cm), placeholder skater ~185 cm.
	constexpr float HipHeight = 96.f;
	constexpr float HipHalfWidth = 11.f;
	constexpr float ThighLength = 47.f;
	constexpr float ShinLength = 46.f;
	constexpr float AnkleHeight = 10.f;     // ankle above the ice with the blade on it
	constexpr float StanceHalfWidth = 14.f;
	constexpr float StrideBack = 16.f;      // push foot travels back this far at full stride
	constexpr float RecoveryLift = 8.f;
	constexpr float TorsoLength = 52.f;
	constexpr float ShoulderHalfWidth = 19.f;
	constexpr float ArmLength = 52.f;

	// Leg swing durations (s) after a contact / whiff.
	constexpr float KickSwingTime = 0.26f;
	constexpr float PushSwingTime = 0.18f;
	constexpr float TouchSwingTime = 0.14f;

	const FLinearColor Jersey(0.85f, 0.18f, 0.08f);
	const FLinearColor Pants(0.06f, 0.08f, 0.22f);
	const FLinearColor Skin(0.92f, 0.72f, 0.58f);
	const FLinearColor Boots(0.03f, 0.03f, 0.03f);
	const FLinearColor Steel(0.75f, 0.78f, 0.82f);
	const FLinearColor White(0.95f, 0.95f, 0.95f);

	float Approach(float Current, float Target, float Dt, float SmoothTime)
	{
		return Current + (Target - Current) * (1.f - FMath::Exp(-Dt / FMath::Max(SmoothTime, 0.001f)));
	}

	FVector RotZ(const FVector& V, float YawDeg)
	{
		return V.RotateAngleAxis(YawDeg, FVector::UpVector);
	}
}

USkaterPuppetComponent::USkaterPuppetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after movement and ball contact of this frame
	BladeLocal[0] = FVector(0.f, -SkatePuppetDetail::StanceHalfWidth, 0.f);
	BladeLocal[1] = FVector(0.f, SkatePuppetDetail::StanceHalfWidth, 0.f);
}

void USkaterPuppetComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildParts();
	ResetPose();
}

void USkaterPuppetComponent::BuildParts()
{
	using namespace SkatePuppetDetail;
	if (bBuilt)
	{
		return;
	}
	AActor* Owner = GetOwner();
	auto Add = [this, Owner](const TCHAR* Shape, const FLinearColor& Color)
	{
		UStaticMeshComponent* Part = SkateVisuals::AddPart(Owner, this, Shape, Color, true);
		Parts.Add(Part);
		return Part;
	};
	Pelvis = Add(TEXT("Sphere"), Pants);
	Torso = Add(TEXT("Cylinder"), Jersey);
	ChestMark = Add(TEXT("Cube"), White);
	Head = Add(TEXT("Sphere"), Skin);
	Visor = Add(TEXT("Cube"), Boots);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		Thigh[Side] = Add(TEXT("Cylinder"), Pants);
		Shin[Side] = Add(TEXT("Cylinder"), Pants);
		Arm[Side] = Add(TEXT("Cylinder"), Jersey);
		Boot[Side] = Add(TEXT("Cube"), Boots);
		Blade[Side] = Add(TEXT("Cube"), Steel);
	}
	bBuilt = true;
}

void USkaterPuppetComponent::ResetPose()
{
	LeanRight = 0.f;
	LeanForward = 0.f;
	Crouch = 8.f;
	Twist = 0.f;
	BrakeAmount = 0.f;
	StrideAmp = 0.f;
	StridePhase = 0.f;
	Charge = 0.f;
	FootLift[0] = FootLift[1] = 0.f;
}

FTransform USkaterPuppetComponent::GetBladeWorldTransform(int32 Side) const
{
	const int32 S = Side == 0 ? 0 : 1;
	const FTransform Local(FRotator(0.f, BladeYaw[S], 0.f), BladeLocal[S]);
	return Local * GetComponentTransform();
}

FVector USkaterPuppetComponent::SolveKnee(const FVector& Hip, FVector& Ankle, float ThighLen, float ShinLen, const FVector& Pole)
{
	FVector ToAnkle = Ankle - Hip;
	const float MaxReach = ThighLen + ShinLen - 0.5f;
	const float MinReach = FMath::Abs(ThighLen - ShinLen) + 1.f;
	float Dist = static_cast<float>(ToAnkle.Size());
	const FVector Dir = ToAnkle.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.f, 0.f, -1.f));
	if (Dist > MaxReach || Dist < MinReach)
	{
		// Out of reach: the foot follows the leg (it may lift slightly) rather than stretching it.
		Dist = FMath::Clamp(Dist, MinReach, MaxReach);
		Ankle = Hip + Dir * Dist;
	}
	const float A = (ThighLen * ThighLen - ShinLen * ShinLen + Dist * Dist) / (2.f * Dist);
	const float H = FMath::Sqrt(FMath::Max(ThighLen * ThighLen - A * A, 0.f));
	const FVector PoleDir = (Pole - Dir * FVector::DotProduct(Pole, Dir)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	return Hip + Dir * A + PoleDir * H;
}

void USkaterPuppetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bBuilt && DeltaTime > 0.f)
	{
		UpdatePose(DeltaTime);
	}
}

void USkaterPuppetComponent::UpdatePose(float Dt)
{
	using namespace SkatePuppetDetail;
	const ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	const USkateMovementComponent* Move = Skater ? Skater->GetSkateMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	const FSkateMoveState& State = Move->GetSkateState();
	const FSkateMovementTuning& MoveTuning = Move->GetMovementTuning();
	const USkateBallControlComponent* Ball = Skater->GetBallControl();

	const FVector Vel = Move->Velocity;
	const float Speed = static_cast<float>(Vel.Size2D());
	const float SpeedRatio = FMath::Clamp(Speed / FMath::Max(MoveTuning.MaxSpeed, 1.f), 0.f, 1.4f);

	// ---- Targets from the real motion ----
	const float BrakeFromDecel = State.BrakeDecel / FMath::Max(MoveTuning.BrakeDecel, 1.f);
	const float SkidFromScrub = State.ScrubDecel / 900.f;
	const float BrakeTarget = FMath::Clamp(FMath::Max(BrakeFromDecel, SkidFromScrub * 0.6f), 0.f, 1.f);
	const float PushTarget = State.PushAmount > 0.05f ? State.PushAmount * FMath::Clamp(State.ThrustAccel / 400.f, 0.35f, 1.f) : 0.f;
	const float LeanTarget = FMath::Clamp(State.LateralAccel / 980.f * Tuning.LeanPerG, -Tuning.MaxLean, Tuning.MaxLean);
	const float ChargeTarget = Ball ? Ball->GetKickCharge() : 0.f;

	if (BrakeAmount < 0.05f)
	{
		// Choose the hockey-stop side once per stop: hips turn towards the side the body already slides to.
		const FVector Heading(State.Heading.X, State.Heading.Y, 0.f);
		const double Side = FVector::CrossProduct(Heading, FVector(Vel.X, Vel.Y, 0.0)).Z;
		TwistSign = Side >= 0.0 ? 1.f : -1.f;
	}

	BrakeAmount = Approach(BrakeAmount, BrakeTarget, Dt, Tuning.PoseSmoothTime);
	StrideAmp = Approach(StrideAmp, PushTarget * (1.f - BrakeAmount), Dt, 0.12f);
	LeanRight = Approach(LeanRight, LeanTarget, Dt, Tuning.PoseSmoothTime);
	LeanForward = Approach(LeanForward, PushTarget * Tuning.PushForwardLean - BrakeAmount * Tuning.BrakeBackLean + ChargeTarget * 6.f, Dt, Tuning.PoseSmoothTime);
	Crouch = Approach(Crouch, 8.f + 10.f * PushTarget + 14.f * BrakeAmount + 4.f * FMath::Min(SpeedRatio, 1.f) + 6.f * ChargeTarget, Dt, Tuning.PoseSmoothTime);
	Twist = Approach(Twist, BrakeAmount * Tuning.BrakeTwist * TwistSign, Dt, Tuning.PoseSmoothTime);
	const bool bKickSwing = Ball && Ball->GetLastSwingKind() == ESkateImpulseKind::Kick && Ball->GetTimeSinceActionSwing() < KickSwingTime;
	// The kick swing starts from the wind-up itself, so the wind-up layer is dropped immediately on release.
	Charge = bKickSwing ? 0.f : Approach(Charge, ChargeTarget, Dt, 0.04f);

	const float StrideRate = FMath::Lerp(Tuning.StrideRateSlow, Tuning.StrideRateFast, FMath::Min(SpeedRatio, 1.f));
	if (StrideAmp > 0.02f)
	{
		StridePhase = FMath::Fmod(StridePhase + Dt * StrideRate * 0.5f, 1.f);
	}

	// ---- Body frame ----
	const float PelvisHeight = HipHeight - Crouch;
	const FVector Up = FVector(FMath::Tan(FMath::DegreesToRadians(LeanForward)), FMath::Tan(FMath::DegreesToRadians(LeanRight)), 1.f).GetSafeNormal();
	const FVector PelvisPos = Up * PelvisHeight;
	const FVector TorsoUp = (Up + FVector(0.25f * StrideAmp, 0.f, 0.f)).GetSafeNormal();
	const FVector TorsoRight = FVector::CrossProduct(TorsoUp, FVector::ForwardVector).GetSafeNormal(); // +Y = right
	const FVector Chest = PelvisPos + TorsoUp * TorsoLength;

	// ---- Leg swing (touch / push / kick, right leg) ----
	float SwingX = 0.f;
	float SwingLift = 0.f;
	if (Ball)
	{
		const float T = Ball->GetTimeSinceActionSwing();
		switch (Ball->GetLastSwingKind())
		{
		case ESkateImpulseKind::Kick:
			if (T < KickSwingTime)
			{
				const float U = T / KickSwingTime;
				const float Forward = U < 0.3f ? FMath::InterpEaseOut(0.f, 1.f, U / 0.3f, 2.f) : 1.f - FMath::SmoothStep(0.f, 1.f, (U - 0.3f) / 0.7f);
				SwingX = FMath::Lerp(-38.f * Ball->GetLastSwingPower(), 55.f, Forward) * (U < 0.3f ? 1.f : Forward);
				SwingLift = 16.f * FMath::Sin(UE_PI * U);
			}
			break;
		case ESkateImpulseKind::Push:
			if (T < PushSwingTime)
			{
				const float U = T / PushSwingTime;
				SwingX = 40.f * FMath::Sin(UE_PI * U);
				SwingLift = 4.f * FMath::Sin(UE_PI * U);
			}
			break;
		case ESkateImpulseKind::Touch:
			if (T < TouchSwingTime)
			{
				const float U = T / TouchSwingTime;
				SwingX = 30.f * FMath::Sin(UE_PI * U);
				SwingLift = 3.f * FMath::Sin(UE_PI * U);
			}
			break;
		default:
			break;
		}
	}

	// ---- Legs ----
	const float StrideWidth = Tuning.StrideWidth;
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const float Sgn = SideIndex == 0 ? -1.f : 1.f;
		const float LegPhase = FMath::Fmod(StridePhase + (SideIndex == 1 ? 0.5f : 0.f), 1.f);

		// Stride layer: push out sideways-back with the toe turned out, then recover with a small lift.
		FVector Foot(Sgn * 2.f, Sgn * StanceHalfWidth, 0.f);
		float Lift = 0.f;
		float Yaw = 0.f;
		if (LegPhase < 0.5f)
		{
			const float Out = FMath::SmoothStep(0.f, 1.f, LegPhase / 0.5f);
			Foot.Y += Sgn * StrideWidth * StrideAmp * Out;
			Foot.X -= StrideBack * StrideAmp * Out;
			Yaw = Sgn * 30.f * StrideAmp;
		}
		else
		{
			const float U = (LegPhase - 0.5f) / 0.5f;
			const float Back = 1.f - FMath::SmoothStep(0.f, 1.f, U);
			Foot.Y += Sgn * StrideWidth * StrideAmp * Back;
			Foot.X += -StrideBack * StrideAmp * Back + 10.f * StrideAmp * FMath::Sin(UE_PI * U);
			Lift = RecoveryLift * StrideAmp * FMath::Sin(UE_PI * U);
			Yaw = Sgn * 30.f * StrideAmp * Back;
		}

		// Brake layer: feet side by side across the travel direction (rotated by the hip twist below).
		const FVector BrakeFoot(0.f, Sgn * 20.f, 0.f);
		Foot = FMath::Lerp(Foot, BrakeFoot, BrakeAmount);
		Yaw = FMath::Lerp(Yaw, 0.f, BrakeAmount);
		Lift *= 1.f - BrakeAmount;

		// Ball layer (right foot): wind-up while charging, swing / tap after a contact.
		if (SideIndex == 1)
		{
			Foot.X += -38.f * Charge + SwingX;
			Lift += 14.f * Charge + SwingLift;
		}

		Foot = RotZ(Foot, Twist);
		Yaw += Twist;

		FVector Ankle(Foot.X, Foot.Y, AnkleHeight + Lift);
		const FVector Hip = PelvisPos + RotZ(FVector(0.f, Sgn * HipHalfWidth, -6.f), Twist * 0.8f);
		const FVector Pole = RotZ(FVector::ForwardVector, Yaw);
		const FVector Knee = SolveKnee(Hip, Ankle, ThighLength, ShinLength, Pole);

		SkateVisuals::SetSegment(Thigh[SideIndex], Hip, Knee, 16.f, 16.f);
		SkateVisuals::SetSegment(Shin[SideIndex], Knee, Ankle, 12.f, 12.f);

		const float EdgeRoll = LeanRight * 0.7f;
		const FRotator FootRot(0.f, Yaw, EdgeRoll);
		const FVector BootCenter = Ankle + FootRot.RotateVector(FVector(4.f, 0.f, -1.f));
		Boot[SideIndex]->SetRelativeTransform(FTransform(FootRot, BootCenter, FVector(26.f, 9.f, 10.f) / 100.f));
		const FVector BladeCenter = Ankle + FootRot.RotateVector(FVector(2.f, 0.f, -7.5f));
		Blade[SideIndex]->SetRelativeTransform(FTransform(FootRot, BladeCenter, FVector(32.f, 1.5f, 5.f) / 100.f));

		BladeLocal[SideIndex] = FVector(BladeCenter.X, BladeCenter.Y, 0.f);
		BladeYaw[SideIndex] = Yaw;
		FootLift[SideIndex] = static_cast<float>(Ankle.Z) - AnkleHeight;

		// Arms: swing opposite to the legs, open up for balance when braking or kicking.
		const FVector Shoulder = Chest - TorsoUp * 4.f + TorsoRight * (Sgn * ShoulderHalfWidth);
		const float SwingDeg = FMath::Sin(2.f * UE_PI * StridePhase) * 35.f * StrideAmp * Sgn;
		const float Spread = 8.f + 26.f * BrakeAmount + 18.f * Charge;
		const FVector HandOffset(FMath::Sin(FMath::DegreesToRadians(SwingDeg)) * ArmLength * 0.8f + 10.f * BrakeAmount,
			Sgn * Spread, -FMath::Cos(FMath::DegreesToRadians(SwingDeg)) * ArmLength * 0.85f);
		SkateVisuals::SetSegment(Arm[SideIndex], Shoulder, Shoulder + HandOffset, 10.f, 10.f);
	}

	// ---- Upper body ----
	SkateVisuals::SetSegment(Torso, PelvisPos, Chest, 32.f, 32.f);
	Pelvis->SetRelativeTransform(FTransform(FRotator(0.f, Twist * 0.8f, 0.f), PelvisPos, FVector(0.26f, 0.34f, 0.2f)));
	ChestMark->SetRelativeTransform(FTransform(FRotationMatrix::MakeFromZX(TorsoUp, FVector::ForwardVector).Rotator(),
		PelvisPos + TorsoUp * (TorsoLength * 0.65f) + FVector(15.f, 0.f, 0.f), FVector(0.04f, 0.18f, 0.12f)));
	const FVector HeadPos = Chest + TorsoUp * 17.f;
	Head->SetRelativeTransform(FTransform(FRotator::ZeroRotator, HeadPos, FVector(0.23f)));
	Visor->SetRelativeTransform(FTransform(FRotator::ZeroRotator, HeadPos + FVector(10.f, 0.f, 1.f), FVector(0.05f, 0.16f, 0.06f)));

	// ---- Label for the HUD ----
	if (bKickSwing) { PoseLabel = TEXT("Kick swing"); }
	else if (Charge > 0.05f) { PoseLabel = TEXT("Kick wind-up"); }
	else if (Ball && Ball->GetTimeSinceActionSwing() < PushSwingTime && Ball->GetLastSwingKind() != ESkateImpulseKind::None) { PoseLabel = TEXT("Touch / push"); }
	else if (BrakeAmount > 0.3f) { PoseLabel = TEXT("Brake (blades across)"); }
	else if (StrideAmp > 0.15f) { PoseLabel = FMath::Abs(LeanRight) > 6.f ? TEXT("Push + lean") : TEXT("Push stroke"); }
	else if (FMath::Abs(LeanRight) > 6.f) { PoseLabel = TEXT("Carve (lean)"); }
	else if (Speed > 20.f) { PoseLabel = TEXT("Glide"); }
	else { PoseLabel = TEXT("Stance"); }
}

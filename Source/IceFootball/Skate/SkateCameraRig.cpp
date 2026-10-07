#include "Skate/SkateCameraRig.h"

#include "Camera/CameraComponent.h"

namespace SkateCameraDetail
{
	// Critically damped spring towards Target (frame-rate independent).
	FVector2D SmoothDamp(const FVector2D& Current, const FVector2D& Target, FVector2D& Velocity, float SmoothTime, float Dt)
	{
		const float Omega = 2.f / FMath::Max(SmoothTime, 0.001f);
		const float X = Omega * Dt;
		const float Exp = 1.f / (1.f + X + 0.48f * X * X + 0.235f * X * X * X);
		const FVector2D Change = Current - Target;
		const FVector2D Temp = (Velocity + Change * Omega) * Dt;
		Velocity = (Velocity - Temp * Omega) * Exp;
		return Target + (Change + Temp) * Exp;
	}

	// The camera aims a little above the ice so the skater's body sits mid-frame.
	constexpr float FocusHeight = 60.f;
}

ASkateCameraRig::ASkateCameraRig()
{
	PrimaryActorTick.bCanEverTick = true;
	// After movement and physics, so the camera shows this frame's positions (no one-frame lag).
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->bConstrainAspectRatio = false;
}

void ASkateCameraRig::SetTarget(AActor* InTarget, bool bBlend)
{
	const FVector OldLocation = GetActorLocation();
	const bool bHadTarget = Target.IsValid();
	Target = InTarget;
	SnapToTarget();
	// Keep the old view for this frame and let the difference fade out.
	BlendOffset = bBlend && bHadTarget ? OldLocation - GetActorLocation() : FVector::ZeroVector;
	if (!BlendOffset.IsZero())
	{
		SetActorLocation(GetActorLocation() + BlendOffset);
	}
}

void ASkateCameraRig::SetStaticMode(bool bInStatic)
{
	bStaticMode = bInStatic;
	UpdateCamera(0.f);
}

void ASkateCameraRig::SnapToTarget()
{
	BlendOffset = FVector::ZeroVector;
	LookAhead = FVector2D::ZeroVector;
	LookAheadVelocity = FVector2D::ZeroVector;
	UpdateCamera(0.f);
}

void ASkateCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}

void ASkateCameraRig::UpdateCamera(float DeltaSeconds)
{
	using namespace SkateCameraDetail;
	if (bStaticMode)
	{
		Camera->SetFieldOfView(Tuning.FieldOfView);
		const FRotator Rotation(Tuning.StaticPitch, Tuning.Yaw, 0.f);
		SetActorLocationAndRotation(StaticFocus - Rotation.Vector() * Tuning.StaticDistance, Rotation);
		return;
	}

	const AActor* TargetActor = Target.Get();
	if (!TargetActor)
	{
		return;
	}

	const FVector Velocity = TargetActor->GetVelocity();
	FVector2D DesiredLookAhead(Velocity.X * Tuning.LookAheadTime, Velocity.Y * Tuning.LookAheadTime);
	if (DesiredLookAhead.Size() > Tuning.MaxLookAhead)
	{
		DesiredLookAhead = DesiredLookAhead.GetSafeNormal() * Tuning.MaxLookAhead;
	}
	// Sprint widens the view a little: speed reads as speed, not only as a bigger number.
	const float SprintAlpha = FMath::Clamp((static_cast<float>(Velocity.Size2D()) - Tuning.SprintFovStartSpeed)
		/ FMath::Max(Tuning.SprintFovFullSpeed - Tuning.SprintFovStartSpeed, 1.f), 0.f, 1.f);
	const float DesiredFovKick = Tuning.SprintFovKick * SprintAlpha * SprintAlpha * (3.f - 2.f * SprintAlpha);
	FovKick = DeltaSeconds > 0.f ? FMath::FInterpTo(FovKick, DesiredFovKick, DeltaSeconds, 4.f) : DesiredFovKick;
	Camera->SetFieldOfView(Tuning.FieldOfView + FovKick);

	if (DeltaSeconds > 0.f)
	{
		LookAhead = SmoothDamp(LookAhead, DesiredLookAhead, LookAheadVelocity, Tuning.LookAheadSmoothTime, DeltaSeconds);
	}

	// Ground point under the skater (capsule centre minus half height ~ 92) plus focus height.
	const FVector TargetLoc = TargetActor->GetActorLocation();
	const FVector Focus(TargetLoc.X + LookAhead.X, TargetLoc.Y + LookAhead.Y, TargetLoc.Z - 92.f + FocusHeight);
	const FRotator Rotation(Tuning.Pitch, Tuning.Yaw, 0.f);
	if (DeltaSeconds > 0.f)
	{
		BlendOffset *= FMath::Exp(-7.f * DeltaSeconds); // ~0.4 s to settle on a new skater
	}
	SetActorLocationAndRotation(Focus - Rotation.Vector() * Tuning.Distance + BlendOffset, Rotation);
}

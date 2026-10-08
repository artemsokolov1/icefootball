#include "Skate/SkateCameraRig.h"

#include "Camera/CameraComponent.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateMovementComponent.h"

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

void ASkateCameraRig::ToggleChaseMode()
{
	bChaseMode = !bChaseMode;
	SnapToTarget();
}

void ASkateCameraRig::SnapToTarget()
{
	if (const ASkateCharacter* Skater = Cast<ASkateCharacter>(Target.Get()))
	{
		const FSkateVec2 Heading = Skater->GetSkateMovement()->GetSkateState().Heading;
		ChaseYaw = FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X));
	}
	BlendOffset = FVector::ZeroVector;
	LookAhead = FVector2D::ZeroVector;
	LookAheadVelocity = FVector2D::ZeroVector;
	InterestOffset = FVector2D::ZeroVector;
	InterestOffsetVelocity = FVector2D::ZeroVector;
	Zoom = 0.f;
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
	if (bChaseMode)
	{
		// Behind the skater, easing after its heading. Look-ahead along the velocity, no interest framing.
		if (const ASkateCharacter* Skater = Cast<ASkateCharacter>(TargetActor))
		{
			const FSkateVec2 Heading = Skater->GetSkateMovement()->GetSkateState().Heading;
			const float Wanted = FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X));
			const float Alpha = DeltaSeconds > 0.f ? 1.f - FMath::Exp(-DeltaSeconds / Tuning.ChaseYawSmoothTime) : 1.f;
			ChaseYaw = FRotator::NormalizeAxis(ChaseYaw + FRotator::NormalizeAxis(Wanted - ChaseYaw) * Alpha);
		}
		FVector2D Ahead(Velocity.X * Tuning.LookAheadTime, Velocity.Y * Tuning.LookAheadTime);
		if (Ahead.Size() > Tuning.MaxLookAhead)
		{
			Ahead = Ahead.GetSafeNormal() * Tuning.MaxLookAhead;
		}
		if (DeltaSeconds > 0.f)
		{
			LookAhead = SmoothDamp(LookAhead, Ahead, LookAheadVelocity, Tuning.LookAheadSmoothTime, DeltaSeconds);
			BlendOffset *= FMath::Exp(-7.f * DeltaSeconds);
		}
		Camera->SetFieldOfView(Tuning.ChaseFieldOfView);
		const FVector TargetLoc = TargetActor->GetActorLocation();
		const FVector Focus(TargetLoc.X + LookAhead.X, TargetLoc.Y + LookAhead.Y, TargetLoc.Z - 92.f + FocusHeight);
		const FRotator Rotation(Tuning.ChasePitch, ChaseYaw, 0.f);
		SetActorLocationAndRotation(Focus - Rotation.Vector() * Tuning.ChaseDistance + BlendOffset, Rotation);
		return;
	}
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

	// Interest point: pull the focus towards the ball / teammate and zoom out so both stay in frame.
	const FVector TargetLoc = TargetActor->GetActorLocation();
	FVector2D DesiredInterestOffset = FVector2D::ZeroVector;
	if (const AActor* InterestActor = Interest.Get())
	{
		DesiredInterestOffset = FVector2D(InterestActor->GetActorLocation() - TargetLoc) * Tuning.InterestWeight;
		if (DesiredInterestOffset.Size() > Tuning.MaxInterestOffset)
		{
			DesiredInterestOffset = DesiredInterestOffset.GetSafeNormal() * Tuning.MaxInterestOffset;
		}
	}
	// Exact fit: the distance at which a ground point at Offset from the focus lands inside FrameFill of the
	// half-screen, both horizontally (screen right) and vertically (depth, foreshortened by the pitch).
	const FRotator Rotation(Tuning.Pitch, Tuning.Yaw, 0.f);
	const FVector2D Fwd(FRotator(0.f, Tuning.Yaw, 0.f).Vector());
	const FVector2D Right(-Fwd.Y, Fwd.X);
	const float PitchRad = FMath::DegreesToRadians(-Tuning.Pitch);
	const float CosP = FMath::Cos(PitchRad), SinP = FMath::Sin(PitchRad);
	const float TanH = FMath::Tan(FMath::DegreesToRadians(Tuning.FieldOfView * 0.5f));
	const float TanV = TanH * 9.f / 16.f; // ponytail: assumes a 16:9 viewport; read the real aspect if it matters
	const auto NeededZoom = [&](const FVector2D& Offset)
	{
		const float R = static_cast<float>(FVector2D::DotProduct(Offset, Right));
		const float D = static_cast<float>(FVector2D::DotProduct(Offset, Fwd)); // + = farther from the camera
		const float ForWidth = FMath::Abs(R) / (Tuning.FrameFill * TanH) - D * CosP;
		const float ForHeight = FMath::Abs(D) * SinP / (Tuning.FrameFill * TanV) - D * CosP;
		return FMath::Max(ForWidth, ForHeight);
	};
	float FitDistance = NeededZoom(-DesiredInterestOffset); // the skater
	if (const AActor* InterestActor = Interest.Get())
	{
		FitDistance = FMath::Max(FitDistance, NeededZoom(FVector2D(InterestActor->GetActorLocation() - TargetLoc) - DesiredInterestOffset));
	}
	const float DesiredZoom = FMath::Clamp(FitDistance, Tuning.Distance, FMath::Max(Tuning.MaxDistance, Tuning.Distance));
	if (DeltaSeconds > 0.f && Zoom > 0.f)
	{
		InterestOffset = SmoothDamp(InterestOffset, DesiredInterestOffset, InterestOffsetVelocity, Tuning.InterestSmoothTime, DeltaSeconds);
		Zoom = FMath::FInterpTo(Zoom, DesiredZoom, DeltaSeconds, 2.f / Tuning.InterestSmoothTime);
	}
	else
	{
		InterestOffset = DesiredInterestOffset;
		Zoom = DesiredZoom;
	}

	// Ground point under the skater (capsule centre minus half height ~ 92) plus focus height.
	const FVector Focus(TargetLoc.X + LookAhead.X + InterestOffset.X, TargetLoc.Y + LookAhead.Y + InterestOffset.Y,
		TargetLoc.Z - 92.f + FocusHeight);
	if (DeltaSeconds > 0.f)
	{
		BlendOffset *= FMath::Exp(-7.f * DeltaSeconds); // ~0.4 s to settle on a new skater
	}
	SetActorLocationAndRotation(Focus - Rotation.Vector() * Zoom + BlendOffset, Rotation);
}

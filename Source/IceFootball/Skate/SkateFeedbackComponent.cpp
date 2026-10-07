#include "Skate/SkateFeedbackComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Skate/SkateAudio.h"
#include "Skate/SkateBall.h"
#include "Skate/SkateBallControlComponent.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateMovementComponent.h"
#include "Skate/SkatePlayerController.h"
#include "Skate/SkaterPuppetComponent.h"
#include "Skate/SkateVisuals.h"

namespace SkateFeedbackDetail
{
	constexpr int32 MaxTrailMarks = 900;
	constexpr int32 MaxScrapeMarks = 300;
	constexpr int32 MaxChips = 140;
	constexpr float MarkHeight = 0.75f;      // above the ice, above the painted lines
	constexpr float TrailWidth = 1.4f;
	constexpr float ScrapeWidth = 6.f;
	constexpr float MinMarkSpeed = 40.f;     // cm/s
	constexpr float ChipLife = 0.45f;
	constexpr float ChipSize = 3.f;
	constexpr float FadeInterval = 0.1f;
	constexpr float FadeTime = 1.5f;         // marks shrink during their last 1.5 s

	const FLinearColor TrailColor(0.48f, 0.58f, 0.70f);
	const FLinearColor ScrapeColor(0.97f, 0.98f, 1.0f);
	const FLinearColor ChipColor(0.95f, 0.97f, 1.0f);
}

USkateFeedbackComponent::USkateFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	LastBladePos[0] = LastBladePos[1] = FVector::ZeroVector;
}

UInstancedStaticMeshComponent* USkateFeedbackComponent::MakeIsm(const TCHAR* Name, const FLinearColor& Color, int32 Count, bool bShadow)
{
	AActor* Owner = GetOwner();
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
	Ism->SetStaticMesh(SkateVisuals::LoadBasicShape(TEXT("Cube")));
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(bShadow);
	Ism->SetMobility(EComponentMobility::Movable);
	// World-space instances: the component itself must not move with the skater.
	Ism->SetUsingAbsoluteLocation(true);
	Ism->SetUsingAbsoluteRotation(true);
	Ism->SetUsingAbsoluteScale(true);
	Ism->SetupAttachment(Owner->GetRootComponent());
	Ism->RegisterComponent();
	Ism->SetWorldTransform(FTransform::Identity);
	Ism->SetMaterial(0, SkateVisuals::MakeColorMaterial(Ism, Color));

	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector::ZeroVector);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Ism->AddInstance(Hidden, /*bWorldSpace*/ true);
	}
	return Ism;
}

void USkateFeedbackComponent::BeginPlay()
{
	using namespace SkateFeedbackDetail;
	Super::BeginPlay();
	TrailIsm = MakeIsm(TEXT("TrailMarks"), TrailColor, MaxTrailMarks, false);
	ScrapeIsm = MakeIsm(TEXT("ScrapeMarks"), ScrapeColor, MaxScrapeMarks, false);
	SprayIsm = MakeIsm(TEXT("IceSpray"), ChipColor, MaxChips, true);
	TrailMarks.SetNum(MaxTrailMarks);
	ScrapeMarks.SetNum(MaxScrapeMarks);
	Chips.SetNum(MaxChips);
	if (Synth && !Synth->IsPlaying())
	{
		Synth->Start();
	}
}

void USkateFeedbackComponent::ClearMarks()
{
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector::ZeroVector);
	auto Clear = [&Hidden](UInstancedStaticMeshComponent* Ism, TArray<FMark>& Ring)
	{
		for (int32 Index = 0; Index < Ring.Num(); ++Index)
		{
			Ring[Index].Time = -1000.0;
			if (Ism)
			{
				Ism->UpdateInstanceTransform(Index, Hidden, true, false, true);
			}
		}
		if (Ism)
		{
			Ism->MarkRenderStateDirty();
		}
	};
	Clear(TrailIsm, TrailMarks);
	Clear(ScrapeIsm, ScrapeMarks);
	bHasLastBladePos[0] = bHasLastBladePos[1] = false;
}

void USkateFeedbackComponent::Rumble(float Intensity, float Duration)
{
	if (Intensity <= 0.f)
	{
		return;
	}
	// Only the skater the player controls right now rumbles the pad (the teammate has no controller of its own).
	ASkatePlayerController* Pc = Cast<ASkatePlayerController>(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
	if (Pc && Pc->GetSkater() == GetOwner())
	{
		Pc->PlayDynamicForceFeedback(FMath::Clamp(Intensity, 0.f, 1.f), Duration, true, true, true, true);
	}
}

void USkateFeedbackComponent::OnBallImpulse(const FSkateBallImpulse& Impulse, const FVector& BallLocation)
{
	const float DeltaV = Impulse.DeltaV.Size();
	if (Synth)
	{
		const float Strength = FMath::Clamp(DeltaV / 1800.f, 0.12f, 1.f) * Tuning.ImpactVolume;
		const float Pitch = Impulse.Kind == ESkateImpulseKind::Kick ? 0.8f : (Impulse.Kind == ESkateImpulseKind::BodyBlock ? 0.6f : 1.25f);
		Synth->TriggerImpact(Strength, Pitch);
	}
	switch (Impulse.Kind)
	{
	case ESkateImpulseKind::Kick:
		Rumble(Tuning.KickRumble * (0.4f + 0.6f * Impulse.Power), 0.12f);
		break;
	case ESkateImpulseKind::Push:
		Rumble(Tuning.PushRumble, 0.07f);
		break;
	case ESkateImpulseKind::Touch:
		Rumble(Tuning.TouchRumble, 0.04f);
		break;
	default:
		break;
	}
}

void USkateFeedbackComponent::OnDribbleTap()
{
	if (Synth)
	{
		Synth->TriggerImpact(0.12f * Tuning.ImpactVolume, 1.6f);
	}
}

void USkateFeedbackComponent::AddMark(UInstancedStaticMeshComponent* Ism, TArray<FMark>& Ring, int32& Next, const FVector& A, const FVector& B, float Width)
{
	if (!Ism || Ring.Num() == 0)
	{
		return;
	}
	const FVector Delta = B - A;
	const double Length = Delta.Size2D();
	if (Length < 0.5)
	{
		return;
	}
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Delta.Y), static_cast<float>(Delta.X)));
	// Blade contact points lie on the ice, so marks sit just above the ice wherever the rink is.
	const FVector Mid((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5, (A.Z + B.Z) * 0.5 + SkateFeedbackDetail::MarkHeight);
	FMark& Mark = Ring[Next];
	Mark.Time = GetWorld()->GetTimeSeconds();
	Mark.Transform = FTransform(FRotator(0.f, Yaw, 0.f), Mid, FVector((Length + 1.0) / 100.0, Width / 100.0, 0.2 / 100.0));
	Ism->UpdateInstanceTransform(Next, Mark.Transform, true, true, true);
	Next = (Next + 1) % Ring.Num();
}

void USkateFeedbackComponent::FadeMarks(UInstancedStaticMeshComponent* Ism, TArray<FMark>& Ring)
{
	using namespace SkateFeedbackDetail;
	if (!Ism)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector::ZeroVector);
	bool bDirty = false;
	for (int32 Index = 0; Index < Ring.Num(); ++Index)
	{
		FMark& Mark = Ring[Index];
		if (Mark.Time < -100.0)
		{
			continue;
		}
		const float Age = static_cast<float>(Now - Mark.Time);
		const float Remaining = Tuning.TrailLifetime - Age;
		if (Remaining <= 0.f)
		{
			Mark.Time = -1000.0;
			Ism->UpdateInstanceTransform(Index, Hidden, true, false, true);
			bDirty = true;
		}
		else if (Remaining < FadeTime)
		{
			FTransform Faded = Mark.Transform;
			const FVector Scale = Faded.GetScale3D();
			Faded.SetScale3D(FVector(Scale.X, Scale.Y * (Remaining / FadeTime), Scale.Z));
			Ism->UpdateInstanceTransform(Index, Faded, true, false, true);
			bDirty = true;
		}
	}
	if (bDirty)
	{
		Ism->MarkRenderStateDirty();
	}
}

void USkateFeedbackComponent::EmitMarks(float DeltaTime, float Speed, bool bScraping)
{
	using namespace SkateFeedbackDetail;
	const ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	const USkaterPuppetComponent* Puppet = Skater ? Skater->GetPuppet() : nullptr;
	if (!Puppet)
	{
		return;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Blade = Puppet->GetBladeWorldTransform(Side).GetLocation();
		const bool bOnIce = Puppet->IsBladeOnIce(Side) && Speed > MinMarkSpeed;
		if (!bOnIce)
		{
			bHasLastBladePos[Side] = false;
			continue;
		}
		if (!bHasLastBladePos[Side])
		{
			LastBladePos[Side] = Blade;
			bHasLastBladePos[Side] = true;
			continue;
		}
		if (FVector::Dist2D(Blade, LastBladePos[Side]) >= Tuning.TrailSpacing)
		{
			if (bScraping)
			{
				AddMark(ScrapeIsm, ScrapeMarks, NextScrape, LastBladePos[Side], Blade, ScrapeWidth);
			}
			else
			{
				AddMark(TrailIsm, TrailMarks, NextTrail, LastBladePos[Side], Blade, TrailWidth);
			}
			LastBladePos[Side] = Blade;
		}
	}
}

void USkateFeedbackComponent::UpdateSpray(float DeltaTime, float Intensity, const FVector& SkaterVelocity)
{
	using namespace SkateFeedbackDetail;
	const ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	const USkaterPuppetComponent* Puppet = Skater ? Skater->GetPuppet() : nullptr;

	// Emit: chips/s proportional to how far above the threshold the real deceleration is.
	if (Puppet && Intensity > 0.f)
	{
		SprayAccumulator += DeltaTime * 140.f * FMath::Min(Intensity, 2.f);
		while (SprayAccumulator >= 1.f)
		{
			SprayAccumulator -= 1.f;
			const int32 Side = FMath::RandRange(0, 1);
			FChip& Chip = Chips[NextChip];
			NextChip = (NextChip + 1) % Chips.Num();
			Chip.Age = 0.f;
			const FVector BladeOnIce = Puppet->GetBladeWorldTransform(Side).GetLocation();
			Chip.FloorZ = BladeOnIce.Z;
			Chip.Location = BladeOnIce + FVector(0.0, 0.0, 2.0);
			const FVector Spread = FMath::VRand() * 120.f;
			Chip.Velocity = SkaterVelocity * 0.7f + FVector(Spread.X, Spread.Y, 0.0) + FVector(0.0, 0.0, FMath::FRandRange(120.f, 260.f));
		}
	}
	else
	{
		SprayAccumulator = 0.f;
	}

	// Simulate (simple ballistic, visual only).
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector::ZeroVector);
	bool bAny = false;
	for (int32 Index = 0; Index < Chips.Num(); ++Index)
	{
		FChip& Chip = Chips[Index];
		if (Chip.Age >= ChipLife)
		{
			continue;
		}
		Chip.Age += DeltaTime;
		Chip.Velocity.Z -= 980.f * DeltaTime;
		Chip.Location += Chip.Velocity * DeltaTime;
		if (Chip.Location.Z < Chip.FloorZ + 0.5)
		{
			Chip.Location.Z = Chip.FloorZ + 0.5;
			Chip.Velocity *= 0.3f;
		}
		const float Size = ChipSize * (1.f - Chip.Age / ChipLife);
		SprayIsm->UpdateInstanceTransform(Index, Chip.Age < ChipLife ? FTransform(FQuat::Identity, Chip.Location, FVector(Size / 100.f)) : Hidden, true, false, true);
		bAny = true;
	}
	if (bAny)
	{
		SprayIsm->MarkRenderStateDirty();
	}
}

void USkateFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	const USkateMovementComponent* Move = Skater ? Skater->GetSkateMovement() : nullptr;
	if (!Move || DeltaTime <= 0.f || !TrailIsm)
	{
		return;
	}
	const FSkateMoveState& State = Move->GetSkateState();
	const float Speed = static_cast<float>(Move->Velocity.Size2D());
	const float StopDecel = State.BrakeDecel + State.ScrubDecel;
	const bool bOnGround = Move->IsMovingOnGround();

	// ---- Sound ----
	if (Synth)
	{
		const float GlideLevel = bOnGround && Speed > 15.f ? FMath::Clamp(Speed / FMath::Max(Move->GetMovementTuning().BoostMaxSpeed, 1.f), 0.f, 1.f) : 0.f;
		const float BrakeLevel = bOnGround && Speed > 10.f ? FMath::Clamp(StopDecel / FMath::Max(Move->GetMovementTuning().BrakeDecel, 1.f), 0.f, 1.2f) : 0.f;
		Synth->SetLevels(GlideLevel * Tuning.GlideVolume, BrakeLevel * Tuning.BrakeVolume);

		// Board bounces of the ball: quiet knock.
		if (const USkateBallControlComponent* BallControl = Skater->GetBallControl())
		{
			if (ASkateBall* Ball = BallControl->GetBall())
			{
				const float Bounce = Ball->ConsumeBounceStrength();
				if (Bounce > 0.08f)
				{
					Synth->TriggerImpact(Bounce * 0.5f * Tuning.ImpactVolume, 0.55f);
				}
			}
		}
	}

	// ---- Marks and spray ----
	const bool bScraping = StopDecel > Tuning.SprayDecelThreshold;
	EmitMarks(DeltaTime, Speed, bScraping);
	const float SprayIntensity = bOnGround && Speed > 60.f ? (StopDecel - Tuning.SprayDecelThreshold) / FMath::Max(Tuning.SprayDecelThreshold, 1.f) : 0.f;
	UpdateSpray(DeltaTime, FMath::Max(SprayIntensity, 0.f), Move->Velocity);

	FadeTimer += DeltaTime;
	if (FadeTimer >= SkateFeedbackDetail::FadeInterval)
	{
		FadeTimer = 0.f;
		FadeMarks(TrailIsm, TrailMarks);
		FadeMarks(ScrapeIsm, ScrapeMarks);
	}
}

#include "Skate/SkateCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Skate/Core/SkateTuningPresets.h"
#include "Skate/SkateAudio.h"
#include "Skate/SkateBallControlComponent.h"
#include "Skate/SkateFeedbackComponent.h"
#include "Skate/SkateMovementComponent.h"
#include "Skate/SkaterPuppetComponent.h"

namespace SkateCharacterDetail
{
	constexpr float CapsuleRadius = 30.f;
	constexpr float CapsuleHalfHeight = 92.f;
}

ASkateCharacter::ASkateCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USkateMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// Rotation comes from the skate heading only.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(SkateCharacterDetail::CapsuleRadius, SkateCharacterDetail::CapsuleHalfHeight);

	// No skeletal mesh / animation blueprint: no root motion can fight the capsule.
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SkateMovement = Cast<USkateMovementComponent>(GetCharacterMovement());

	Puppet = CreateDefaultSubobject<USkaterPuppetComponent>(TEXT("Puppet"));
	Puppet->SetupAttachment(GetCapsuleComponent());
	Puppet->SetRelativeLocation(FVector(0.f, 0.f, -SkateCharacterDetail::CapsuleHalfHeight));

	BallControl = CreateDefaultSubobject<USkateBallControlComponent>(TEXT("BallControl"));
	Feedback = CreateDefaultSubobject<USkateFeedbackComponent>(TEXT("Feedback"));

	IceSynth = CreateDefaultSubobject<USkateIceSynth>(TEXT("IceSynth"));
	IceSynth->SetupAttachment(GetCapsuleComponent());

	TuningResponsive = SkateTuningPresets::Make(ESkatePreset::Responsive);
	TuningBalanced = SkateTuningPresets::Make(ESkatePreset::Balanced);
	TuningInertial = SkateTuningPresets::Make(ESkatePreset::Inertial);
}

void ASkateCharacter::BeginPlay()
{
	// Before Super::BeginPlay(): it dispatches the components' BeginPlay, and the feedback
	// component starts the synth there.
	if (Feedback)
	{
		Feedback->SetSynth(IceSynth);
	}
	if (Puppet)
	{
		// Same team: same jersey; the chest patch tells the two skaters apart (white = 1, yellow = 2).
		Puppet->SetMarkColor(TeamSlot == 0 ? FLinearColor(0.95f, 0.95f, 0.95f) : FLinearColor(1.f, 0.85f, 0.05f));
	}
	Super::BeginPlay();

	// Ball contact must see this frame's movement result: tick after the movement component.
	if (BallControl && SkateMovement)
	{
		BallControl->PrimaryComponentTick.AddPrerequisite(SkateMovement, SkateMovement->PrimaryComponentTick);
	}
	SkateMovement->ResetSkating(GetActorForwardVector());
	ApplyActiveTuning();
}

const FSkateTuning& ASkateCharacter::GetActiveTuning() const
{
	switch (ActivePreset)
	{
	case ESkatePreset::Responsive: return TuningResponsive;
	case ESkatePreset::Inertial: return TuningInertial;
	case ESkatePreset::Balanced:
	default: return TuningBalanced;
	}
}

void ASkateCharacter::ApplyActiveTuning()
{
	const FSkateTuning& Tuning = GetActiveTuning();
	if (SkateMovement)
	{
		SkateMovement->SetMovementTuning(Tuning.Movement);
	}
	if (BallControl)
	{
		BallControl->SetTuning(Tuning);
		BallControl->SetInteractionEnabled(bBallInteractionEnabled);
	}
	if (Puppet)
	{
		Puppet->SetAnimTuning(Tuning.Anim);
	}
	if (Feedback)
	{
		Feedback->SetTuning(Tuning.Feedback);
	}
}

#if WITH_EDITOR
void ASkateCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyActiveTuning();
}
#endif

void ASkateCharacter::SetPreset(ESkatePreset Preset)
{
	ActivePreset = Preset;
	ApplyActiveTuning();
}

void ASkateCharacter::CyclePreset(int32 Direction)
{
	const int32 Index = (static_cast<int32>(ActivePreset) + Direction + SkateTuningPresets::Count) % SkateTuningPresets::Count;
	SetPreset(static_cast<ESkatePreset>(Index));
}

void ASkateCharacter::SetBallInteractionEnabled(bool bEnabled)
{
	bBallInteractionEnabled = bEnabled;
	if (BallControl)
	{
		BallControl->SetInteractionEnabled(bEnabled);
	}
}

void ASkateCharacter::ApplyFrameInput(const FSkateFrameInput& Input)
{
	const FSkateTuning& Tuning = GetActiveTuning();
	LastFrameInput = Input;
	LastStick = SkateInput::ShapeStickWorld(static_cast<float>(Input.RawStick.X), static_cast<float>(Input.RawStick.Y),
		FMath::DegreesToRadians(Input.CameraYawDeg), Tuning.Input);
	LastBrake = SkateInput::ShapeBrake(Input.BrakeRaw, Tuning.Input);
	LastBoost = SkateInput::ShapeTrigger(Input.BoostRaw, Tuning.Input.TriggerDeadZone);

	FSkateMoveInput MoveInput;
	MoveInput.Direction = LastStick.Direction;
	MoveInput.Magnitude = LastStick.Magnitude;
	MoveInput.Brake = LastBrake;
	MoveInput.Boost = LastBoost;
	SkateMovement->SetSkateInput(MoveInput);

	if (BallControl)
	{
		BallControl->QueueActions(Input.bPushPressed, Input.bPushReleased, Input.bKickPressed, Input.bKickReleased);
	}
}

void ASkateCharacter::ApplyMoveInput(const FSkateMoveInput& Input)
{
	LastFrameInput = FSkateFrameInput();
	LastStick.Direction = Input.Direction;
	LastStick.Magnitude = Input.Magnitude;
	LastBrake = Input.Brake;
	LastBoost = Input.Boost;
	SkateMovement->SetSkateInput(Input);
}

void ASkateCharacter::CancelBallActions()
{
	if (BallControl)
	{
		BallControl->CancelActions();
	}
}

void ASkateCharacter::ResetSkater(const FTransform& Transform)
{
	SetActorLocationAndRotation(Transform.GetLocation(), Transform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	SkateMovement->StopMovementImmediately();
	SkateMovement->ResetSkating(Transform.GetRotation().GetForwardVector());
	if (BallControl)
	{
		BallControl->ResetControl();
	}
	if (Puppet)
	{
		Puppet->ResetPose();
	}
	if (Feedback)
	{
		Feedback->ClearMarks();
	}
}

void ASkateCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDebugEnabled)
	{
		DrawMovementDebug();
	}
}

void ASkateCharacter::DrawMovementDebug() const
{
	UWorld* World = GetWorld();
	if (!World || !SkateMovement)
	{
		return;
	}
	const FSkateMoveState& State = SkateMovement->GetSkateState();
	const FVector Base = GetActorLocation() - FVector(0.f, 0.f, SkateCharacterDetail::CapsuleHalfHeight - 3.f);
	const FVector Vel(State.Velocity.X, State.Velocity.Y, 0.f);
	const FVector Heading(State.Heading.X, State.Heading.Y, 0.f);

	// Actual velocity (green, 1 s of travel scaled by 0.5), heading / blade direction (blue), stick (yellow).
	DrawDebugDirectionalArrow(World, Base, Base + Vel * 0.5f, 25.f, FColor::Green, false, -1.f, 0, 3.f);
	DrawDebugDirectionalArrow(World, Base + FVector(0, 0, 2), Base + FVector(0, 0, 2) + Heading * 120.f, 20.f, FColor(40, 120, 255), false, -1.f, 0, 3.f);
	if (LastStick.Magnitude > 0.f)
	{
		const FVector Stick(LastStick.Direction.X, LastStick.Direction.Y, 0.f);
		DrawDebugDirectionalArrow(World, Base + FVector(0, 0, 4), Base + FVector(0, 0, 4) + Stick * (40.f + 140.f * LastStick.Magnitude), 20.f, FColor::Yellow, false, -1.f, 0, 2.f);
	}
	// Lateral acceleration (orange), drives the lean.
	if (FMath::Abs(State.LateralAccel) > 50.f)
	{
		const FVector Right(-State.Heading.Y, State.Heading.X, 0.f);
		DrawDebugLine(World, Base, Base + Right * (State.LateralAccel * 0.08f), FColor::Orange, false, -1.f, 0, 2.f);
	}
}

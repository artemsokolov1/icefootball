#include "Skate/SkateCharacter.h"

#include "Components/CapsuleComponent.h"
#include "IceFootball.h"
#include "Skate/Core/SkateHit.h"
#include "Skate/SkateArena.h"
#include "Skate/SkateBall.h"
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
		// Red = the player's team, blue = opponents; the chest patch tells the two skaters apart (white = 1, yellow = 2).
		if (Team != 0)
		{
			Puppet->SetJerseyColor(FLinearColor(0.10f, 0.30f, 0.85f), FLinearColor(0.06f, 0.20f, 0.60f));
		}
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
	// Defending: with an opponent carrying the ball nearby, a stick that leads away from it skates backwards,
	// so the skater keeps facing the play. Anywhere else the stick is plain forward skating.
	if (BallControl && !BallControl->HasBall() && LastStick.Magnitude > 0.3f)
	{
		if (const ASkateBall* Ball = BallControl->GetBall())
		{
			const USkateBallControlComponent* Holder = Cast<USkateBallControlComponent>(Ball->GetHolder());
			const ASkateCharacter* Carrier = Holder ? Cast<ASkateCharacter>(Holder->GetOwner()) : nullptr;
			const FVector Delta = Ball->GetActorLocation() - GetActorLocation();
			const FVector ToBall = Delta.GetSafeNormal2D();
			MoveInput.bBackward = Carrier && Carrier->GetTeam() != Team && Delta.Size2D() < 1200.f
				&& LastStick.Direction.X * ToBall.X + LastStick.Direction.Y * ToBall.Y < -0.17f; // > 100 deg away
		}
	}
	SkateMovement->SetSkateInput(IsStunned() ? FSkateMoveInput() : MoveInput);

	if (BallControl)
	{
		// X: a shot with the ball at the feet or in reach, or with a pass on its way to me (the wind-up waits for the
		// ball: a one-timer); otherwise the take (or a body check on Normal).
		const ESkateContactReason Reach = BallControl->GetReport().Reason;
		const ASkateBall* Ball = BallControl->GetBall();
		const bool bPassComing = Ball && !Ball->GetHolder() && Ball->IsPassFor(BallControl);
		const bool bBallPlayable = BallControl->HasBall() || Reach == ESkateContactReason::Reachable || Reach == ESkateContactReason::ActionReachOnly || bPassComing;
		if (Input.bKickPressed && !bBallPlayable)
		{
			StartTake();
		}
		BallControl->QueueActions(Input.bPushPressed, Input.bPushReleased, Input.bKickPressed && bBallPlayable, Input.bKickReleased, Input.bThroughPressed);
	}
}

bool ASkateCharacter::CanTakeNow() const
{
	const ASkateBall* Ball = BallControl ? BallControl->GetBall() : nullptr;
	const USkateBallControlComponent* Holder = Ball ? Cast<USkateBallControlComponent>(Ball->GetHolder()) : nullptr;
	const ASkateCharacter* Carrier = Holder ? Cast<ASkateCharacter>(Holder->GetOwner()) : nullptr;
	const FSkatePossessionTuning& PT = GetActiveTuning().BallControl.Possession;
	if (!Carrier || Carrier->GetTeam() == Team || TimeSinceTake < PT.TakeCooldown || IsStunned()
		|| FVector::Dist2D(Ball->GetActorLocation(), GetActorLocation()) > PT.TakeRange)
	{
		return false;
	}
	const FVector ToMe = (GetActorLocation() - Carrier->GetActorLocation()).GetSafeNormal2D();
	return FVector::DotProduct(Carrier->GetActorForwardVector().GetSafeNormal2D(), ToMe) >= PT.TakeBehindDot;
}

void ASkateCharacter::StartTake()
{
	const FSkatePossessionTuning& PT = GetActiveTuning().BallControl.Possession;
	if (IsStunned() || TimeSinceTake < PT.TakeCooldown)
	{
		return;
	}
	if (BallControl && BallControl->TryTake(PT.TakeRange, PT.StealProtectTime, PT.TakeBallSpeed))
	{
		TimeSinceTake = 0.f;
		return;
	}
	// No ball to take: a body check, when the match allows them.
	const ASkateArena* Arena = ASkateArena::Find(GetWorld());
	if (Arena && Arena->AreHitsEnabled())
	{
		StartCheck();
	}
}

void ASkateCharacter::StartCheck()
{
	const FSkateHitTuning& HT = GetActiveTuning().Hit;
	if (IsStunned() || TimeSinceCheck < HT.Cooldown || !SkateMovement)
	{
		return;
	}
	TimeSinceCheck = 0.f;
	CheckLeft = HT.CheckWindow;
	const FSkateVec2 Heading = SkateMovement->GetSkateState().Heading;
	SkateMovement->Velocity.X += Heading.X * HT.LungeSpeed;
	SkateMovement->Velocity.Y += Heading.Y * HT.LungeSpeed;
}

void ASkateCharacter::ApplyMoveInput(const FSkateMoveInput& Input, const FSkateBallActionInput* Actions)
{
	LastFrameInput = FSkateFrameInput();
	LastStick.Direction = Input.Direction;
	LastStick.Magnitude = Input.Magnitude;
	LastBrake = Input.Brake;
	LastBoost = Input.Boost;
	SkateMovement->SetSkateInput(IsStunned() ? FSkateMoveInput() : Input);
	if (BallControl && Actions)
	{
		BallControl->QueueActions(Actions->bPushPressed, Actions->bPushReleased, Actions->bKickPressed, Actions->bKickReleased);
	}
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

void ASkateCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
	FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	ASkateCharacter* Rival = Cast<ASkateCharacter>(Other);
	const FSkateHitTuning& HT = GetActiveTuning().Hit;
	if (!Rival || Rival->GetTeam() == Team || TimeSinceHit < HT.Cooldown || Rival->TimeSinceHit < HT.Cooldown || !SkateMovement || !Rival->SkateMovement)
	{
		return;
	}
	const ASkateArena* Arena = ASkateArena::Find(GetWorld());
	if (Arena && !Arena->AreHitsEnabled())
	{
		return;
	}
	auto To2D = [](const FVector& V) { return FSkateVec2(static_cast<float>(V.X), static_cast<float>(V.Y)); };
	const FSkateHitResult Result = FSkateHit::Resolve(HT, To2D(GetActorLocation()), To2D(SkateMovement->Velocity), IsChecking(),
		To2D(Rival->GetActorLocation()), To2D(Rival->SkateMovement->Velocity), Rival->IsChecking());
	if (!Result.bHit)
	{
		return;
	}
	ASkateCharacter* Hitter = Result.Hitter == 0 ? this : Rival;
	ASkateCharacter* Victim = Result.Hitter == 0 ? Rival : this;
	Hitter->SkateMovement->Velocity.X = Result.HitterVelocity.X;
	Hitter->SkateMovement->Velocity.Y = Result.HitterVelocity.Y;
	Hitter->TimeSinceHit = 0.f;
	Hitter->CheckLeft = 0.f;
	Victim->ApplyHit(FVector2D(Result.VictimVelocity.X, Result.VictimVelocity.Y), HT.StunTime);
	UE_LOG(LogIceSkate, Verbose, TEXT("CHECK: team %d slot %d hits team %d slot %d, victim shoved at %.0f cm/s"), Hitter->Team, Hitter->TeamSlot,
		Victim->Team, Victim->TeamSlot, Result.VictimVelocity.Size());
}

void ASkateCharacter::ApplyHit(const FVector2D& NewVelocity, float Stun)
{
	if (SkateMovement)
	{
		SkateMovement->Velocity.X = NewVelocity.X;
		SkateMovement->Velocity.Y = NewVelocity.Y;
		SkateMovement->SetSkateInput(FSkateMoveInput());
	}
	StunLeft = Stun;
	TimeSinceHit = 0.f;
	if (BallControl)
	{
		BallControl->CancelActions();
	}
}

void ASkateCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StunLeft = FMath::Max(0.f, StunLeft - DeltaSeconds);
	CheckLeft -= DeltaSeconds;
	TimeSinceCheck += DeltaSeconds;
	TimeSinceTake += DeltaSeconds;
	TimeSinceHit += DeltaSeconds;
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

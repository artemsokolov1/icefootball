#include "Skate/SkatePlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "IceFootball.h"
#include "Skate/SkateArena.h"
#include "Skate/SkateBall.h"
#include "Skate/SkateBallControlComponent.h"
#include "Skate/SkateCameraRig.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateMovementComponent.h"

namespace SkatePlayerControllerDetail
{
	const int32 FpsCaps[] = { 0, 30, 60, 120 };
	constexpr int32 NumFpsCaps = UE_ARRAY_COUNT(FpsCaps);
	// Keyboard only wins when the stick is inside this radius (so a resting stick never blocks the keys).
	constexpr float StickIdleRadius = 0.12f;
}

ASkatePlayerController::ASkatePlayerController()
{
	// The camera rig is the view target, not the pawn.
	bAutoManageActiveCameraTarget = false;
	bShowMouseCursor = false;
}

int32 ASkatePlayerController::GetFpsCap() const
{
	return SkatePlayerControllerDetail::FpsCaps[FpsCapIndex];
}

UInputAction* ASkatePlayerController::MakeAction(const TCHAR* Name, EInputActionValueType ValueType)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = ValueType;
	Actions.Add(Action);
	return Action;
}

void ASkatePlayerController::BuildInputMappings()
{
	if (MappingContext)
	{
		return;
	}
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Skate"));
	UInputMappingContext* Imc = MappingContext;

	// ---- Movement ----
	IA_Stick = MakeAction(TEXT("IA_Skate_Stick"), EInputActionValueType::Axis2D);
	Imc->MapKey(IA_Stick, EKeys::Gamepad_Left2D); // raw: radial dead zone is applied in code

	IA_Keys = MakeAction(TEXT("IA_Skate_Keys"), EInputActionValueType::Axis2D);
	auto MapDirection = [Imc, this](const FKey& Key, bool bToY, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = Imc->MapKey(IA_Keys, Key);
		if (bToY)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Imc);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Swizzle);
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Imc));
		}
	};
	MapDirection(EKeys::W, true, false);
	MapDirection(EKeys::Up, true, false);
	MapDirection(EKeys::S, true, true);
	MapDirection(EKeys::Down, true, true);
	MapDirection(EKeys::D, false, false);
	MapDirection(EKeys::Right, false, false);
	MapDirection(EKeys::A, false, true);
	MapDirection(EKeys::Left, false, true);

	IA_Slow = MakeAction(TEXT("IA_Skate_SlowKey"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Slow, EKeys::LeftAlt);

	IA_Brake = MakeAction(TEXT("IA_Skate_Brake"), EInputActionValueType::Axis1D);
	Imc->MapKey(IA_Brake, EKeys::Gamepad_LeftTriggerAxis);
	Imc->MapKey(IA_Brake, EKeys::SpaceBar);

	IA_Boost = MakeAction(TEXT("IA_Skate_Boost"), EInputActionValueType::Axis1D);
	Imc->MapKey(IA_Boost, EKeys::Gamepad_RightTriggerAxis);
	Imc->MapKey(IA_Boost, EKeys::LeftShift);

	IA_Through = MakeAction(TEXT("IA_Skate_ThroughPass"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Through, EKeys::Gamepad_FaceButton_Top);
	Imc->MapKey(IA_Through, EKeys::I);

	// ---- Ball ----
	IA_Push = MakeAction(TEXT("IA_Skate_Push"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Push, EKeys::Gamepad_FaceButton_Bottom);
	Imc->MapKey(IA_Push, EKeys::J);

	IA_Kick = MakeAction(TEXT("IA_Skate_Kick"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Kick, EKeys::Gamepad_FaceButton_Left);
	Imc->MapKey(IA_Kick, EKeys::K);

	// ---- Test tools ----
	IA_Reset = MakeAction(TEXT("IA_Skate_Reset"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Reset, EKeys::Gamepad_Special_Right);
	Imc->MapKey(IA_Reset, EKeys::R);

	IA_BallToFeet = MakeAction(TEXT("IA_Skate_BallToFeet"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_BallToFeet, EKeys::Gamepad_RightShoulder);
	Imc->MapKey(IA_BallToFeet, EKeys::T);

	IA_Debug = MakeAction(TEXT("IA_Skate_Debug"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Debug, EKeys::Gamepad_Special_Left);
	Imc->MapKey(IA_Debug, EKeys::F1);

	IA_Camera = MakeAction(TEXT("IA_Skate_Camera"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Camera, EKeys::Gamepad_DPad_Up);
	Imc->MapKey(IA_Camera, EKeys::F2);

	IA_Chase = MakeAction(TEXT("IA_Skate_CameraChase"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Chase, EKeys::Gamepad_RightThumbstick);
	Imc->MapKey(IA_Chase, EKeys::C);

	IA_FpsCap = MakeAction(TEXT("IA_Skate_FpsCap"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_FpsCap, EKeys::Gamepad_DPad_Down);
	Imc->MapKey(IA_FpsCap, EKeys::F3);

	IA_ToggleBall = MakeAction(TEXT("IA_Skate_ToggleBall"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_ToggleBall, EKeys::F4);

	IA_Switch = MakeAction(TEXT("IA_Skate_SwitchSkater"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Switch, EKeys::Gamepad_LeftShoulder);
	Imc->MapKey(IA_Switch, EKeys::Q);

	IA_PresetNext = MakeAction(TEXT("IA_Skate_PresetNext"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_PresetNext, EKeys::Gamepad_DPad_Right);
	IA_PresetPrev = MakeAction(TEXT("IA_Skate_PresetPrev"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_PresetPrev, EKeys::Gamepad_DPad_Left);
	IA_Preset1 = MakeAction(TEXT("IA_Skate_Preset1"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Preset1, EKeys::One);
	IA_Preset2 = MakeAction(TEXT("IA_Skate_Preset2"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Preset2, EKeys::Two);
	IA_Preset3 = MakeAction(TEXT("IA_Skate_Preset3"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Preset3, EKeys::Three);
}

void ASkatePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildInputMappings();

	UEnhancedInputComponent* Eic = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Eic)
	{
		UE_LOG(LogIceSkate, Error, TEXT("Skate controls need Enhanced Input: set DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent (Config/DefaultInput.ini)."));
		return;
	}

	Eic->BindAction(IA_Stick, ETriggerEvent::Triggered, this, &ASkatePlayerController::OnStick);
	Eic->BindAction(IA_Stick, ETriggerEvent::Completed, this, &ASkatePlayerController::OnStickReleased);
	Eic->BindAction(IA_Keys, ETriggerEvent::Triggered, this, &ASkatePlayerController::OnKeys);
	Eic->BindAction(IA_Keys, ETriggerEvent::Completed, this, &ASkatePlayerController::OnKeysReleased);
	Eic->BindAction(IA_Slow, ETriggerEvent::Started, this, &ASkatePlayerController::OnSlowPressed);
	Eic->BindAction(IA_Slow, ETriggerEvent::Completed, this, &ASkatePlayerController::OnSlowReleased);
	Eic->BindAction(IA_Brake, ETriggerEvent::Triggered, this, &ASkatePlayerController::OnBrake);
	Eic->BindAction(IA_Brake, ETriggerEvent::Completed, this, &ASkatePlayerController::OnBrakeReleased);
	Eic->BindAction(IA_Boost, ETriggerEvent::Triggered, this, &ASkatePlayerController::OnBoost);
	Eic->BindAction(IA_Boost, ETriggerEvent::Completed, this, &ASkatePlayerController::OnBoostReleased);
	Eic->BindAction(IA_Through, ETriggerEvent::Started, this, &ASkatePlayerController::OnThrough);

	Eic->BindAction(IA_Push, ETriggerEvent::Started, this, &ASkatePlayerController::OnPushPressed);
	Eic->BindAction(IA_Push, ETriggerEvent::Completed, this, &ASkatePlayerController::OnPushReleased);
	Eic->BindAction(IA_Kick, ETriggerEvent::Started, this, &ASkatePlayerController::OnKickPressed);
	Eic->BindAction(IA_Kick, ETriggerEvent::Completed, this, &ASkatePlayerController::OnKickReleased);

	Eic->BindAction(IA_Reset, ETriggerEvent::Started, this, &ASkatePlayerController::OnReset);
	Eic->BindAction(IA_BallToFeet, ETriggerEvent::Started, this, &ASkatePlayerController::OnBallToFeet);
	Eic->BindAction(IA_Debug, ETriggerEvent::Started, this, &ASkatePlayerController::OnToggleDebug);
	Eic->BindAction(IA_Camera, ETriggerEvent::Started, this, &ASkatePlayerController::OnToggleCamera);
	Eic->BindAction(IA_Chase, ETriggerEvent::Started, this, &ASkatePlayerController::OnToggleChase);
	Eic->BindAction(IA_FpsCap, ETriggerEvent::Started, this, &ASkatePlayerController::OnCycleFpsCap);
	Eic->BindAction(IA_ToggleBall, ETriggerEvent::Started, this, &ASkatePlayerController::OnToggleBall);
	Eic->BindAction(IA_Switch, ETriggerEvent::Started, this, &ASkatePlayerController::OnSwitchSkater);
	Eic->BindAction(IA_PresetNext, ETriggerEvent::Started, this, &ASkatePlayerController::OnPresetNext);
	Eic->BindAction(IA_PresetPrev, ETriggerEvent::Started, this, &ASkatePlayerController::OnPresetPrev);
	Eic->BindAction(IA_Preset1, ETriggerEvent::Started, this, &ASkatePlayerController::OnPreset1);
	Eic->BindAction(IA_Preset2, ETriggerEvent::Started, this, &ASkatePlayerController::OnPreset2);
	Eic->BindAction(IA_Preset3, ETriggerEvent::Started, this, &ASkatePlayerController::OnPreset3);
}

void ASkatePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	BuildInputMappings();
	// -BotsVsBots on the command line: a spectated match (log checks without a gamepad).
	bBotsVsBots |= FParse::Param(FCommandLine::Get(), TEXT("BotsVsBots"));
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
	SetInputMode(FInputModeGameOnly());
	EnsureCameraRig();
	if (CameraRig && GetPawn())
	{
		CameraRig->SetTarget(GetPawn());
		SetViewTarget(CameraRig);
	}
}

void ASkatePlayerController::EnsureCameraRig()
{
	if (CameraRig || !IsLocalController())
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	CameraRig = GetWorld()->SpawnActor<ASkateCameraRig>(ASkateCameraRig::StaticClass(), FTransform::Identity, Params);
	if (CameraRig)
	{
		if (const ASkateArena* Arena = ASkateArena::Find(GetWorld()))
		{
			CameraRig->SetStaticFocus(Arena->GetRinkCenter());
		}
	}
}

void ASkatePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	EnsureCameraRig();
	if (CameraRig)
	{
		CameraRig->SetTarget(InPawn);
		SetViewTarget(CameraRig);
	}
	Team.Reset();
	RefreshTeam();
}

ASkateCharacter* ASkatePlayerController::GetSkater() const
{
	if (Team.IsValidIndex(ActiveIndex))
	{
		if (ASkateCharacter* Active = Team[ActiveIndex].Get())
		{
			return Active;
		}
	}
	return Cast<ASkateCharacter>(GetPawn());
}

FString ASkatePlayerController::GetTeammateModeName() const
{
	return FString(ANSI_TO_TCHAR(SkateSkaterModeName(static_cast<ESkateSkaterMode>(TeammateMode))));
}

void ASkatePlayerController::RefreshTeam()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 Found = 0;
	for (TActorIterator<ASkateCharacter> It(World); It; ++It)
	{
		++Found;
	}
	bool bAllValid = true;
	for (const TWeakObjectPtr<ASkateCharacter>& Member : Team)
	{
		bAllValid &= Member.IsValid();
	}
	for (const TWeakObjectPtr<ASkateCharacter>& Member : Opponents)
	{
		bAllValid &= Member.IsValid();
	}
	if (bAllValid && Found == Team.Num() + Opponents.Num())
	{
		return;
	}

	ASkateCharacter* Active = Team.IsValidIndex(ActiveIndex) ? Team[ActiveIndex].Get() : nullptr;
	if (!Active)
	{
		Active = Cast<ASkateCharacter>(GetPawn());
	}
	TArray<ASkateCharacter*> Skaters;
	for (TActorIterator<ASkateCharacter> It(World); It; ++It)
	{
		Skaters.Add(*It);
	}
	Skaters.StableSort([](const ASkateCharacter& A, const ASkateCharacter& B) { return A.GetTeamSlot() < B.GetTeamSlot(); });
	Team.Reset();
	TeamBrains.Reset();
	TeamModes.Reset();
	Opponents.Reset();
	OpponentBrains.Reset();
	OpponentModes.Reset();
	SeenAcquires.Reset();
	SeenImpulses.Reset();
	ActiveIndex = 0;
	for (ASkateCharacter* Skater : Skaters)
	{
		if (Skater != GetPawn())
		{
			// Input is applied to every AI skater in PlayerTick: their movement must tick after this controller.
			AddPawnTickDependency(Skater);
		}
		if (Skater->GetTeam() != 0)
		{
			Opponents.Add(Skater);
			OpponentBrains.AddDefaulted();
			OpponentBrains.Last().Seed = 777u * (Skater->GetTeamSlot() + 1);
			OpponentModes.Add(0);
			continue;
		}
		if (Skater == Active)
		{
			ActiveIndex = Team.Num();
		}
		Team.Add(Skater);
		TeamBrains.AddDefaulted();
		TeamModes.Add(0);
		const USkateBallControlComponent* Ball = Skater->GetBallControl();
		SeenAcquires.Add(Ball ? Ball->GetAcquireCount() : 0);
		SeenImpulses.Add(Ball ? Ball->GetImpulseCount() : 0);
	}
}

void ASkatePlayerController::SwitchTo(int32 Index, bool bLatchStick)
{
	if (!Team.IsValidIndex(Index) || Index == ActiveIndex)
	{
		return;
	}
	ASkateCharacter* Next = Team[Index].Get();
	if (!Next)
	{
		return;
	}
	if (ASkateCharacter* Previous = GetSkater())
	{
		Previous->CancelBallActions();
	}
	ActiveIndex = Index;
	LatchStick = LastRawStick;
	bStickLatched = bLatchStick && LastRawStick.Size() > 0.3;
	LatchTime = 0.f;
	bPushEdge = false;
	bPushReleaseEdge = false;
	bKickPressEdge = false;
	bKickReleaseEdge = false;
	if (CameraRig)
	{
		CameraRig->SetTarget(Next, /*bBlend*/ true);
	}
}

void ASkatePlayerController::UpdateAutoSwitch()
{
	int32 Target = INDEX_NONE;
	for (int32 Index = 0; Index < Team.Num(); ++Index)
	{
		const ASkateCharacter* Skater = Team[Index].Get();
		const USkateBallControlComponent* Ball = Skater ? Skater->GetBallControl() : nullptr;
		if (!Ball || !SeenAcquires.IsValidIndex(Index))
		{
			continue;
		}
		const bool bNewAcquire = Ball->GetAcquireCount() != SeenAcquires[Index];
		const bool bNewImpulse = Ball->GetImpulseCount() != SeenImpulses[Index];
		SeenAcquires[Index] = Ball->GetAcquireCount();
		SeenImpulses[Index] = Ball->GetImpulseCount();
		if (!bAutoSwitch)
		{
			continue;
		}
		// The teammate got the ball: take it over.
		if (Index != ActiveIndex && bNewAcquire && Ball->HasBall())
		{
			Target = Index;
		}
		// The player just passed: every pass goes to a partner, take it over.
		if (Index == ActiveIndex && bNewImpulse && Ball->GetLastImpulse().Kind == ESkateImpulseKind::Push && Target == INDEX_NONE && Ball->GetBall())
		{
			for (int32 Mate = 0; Mate < Team.Num() && Target == INDEX_NONE; ++Mate)
			{
				Target = Mate != Index && Team[Mate].IsValid() && Team[Mate]->GetBallControl() == Ball->GetBall()->GetPassReceiver() ? Mate : INDEX_NONE;
			}
		}
	}
	// An opponent has the ball: the player is always the skater nearest to it (the defender that matters).
	if (Target == INDEX_NONE && bAutoSwitch && TimeSinceManualSwitch > 2.f && CachedArena.IsValid() && CachedArena->GetBall())
	{
		const ASkateBall* Ball = CachedArena->GetBall();
		const USkateBallControlComponent* HolderComp = Cast<USkateBallControlComponent>(Ball->GetHolder());
		const ASkateCharacter* Carrier = HolderComp ? Cast<ASkateCharacter>(HolderComp->GetOwner()) : nullptr;
		if (Carrier && Carrier->GetTeam() != 0)
		{
			int32 Nearest = INDEX_NONE;
			float NearestDist = TNumericLimits<float>::Max();
			float ActiveDist = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Team.Num(); ++Index)
			{
				const ASkateCharacter* Member = Team[Index].Get();
				const float Dist = Member ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Ball->GetActorLocation())) : NearestDist;
				if (Member && Dist < NearestDist)
				{
					NearestDist = Dist;
					Nearest = Index;
				}
				if (Index == ActiveIndex && Member)
				{
					ActiveDist = Dist;
				}
			}
			if (Nearest != INDEX_NONE && Nearest != ActiveIndex && NearestDist < ActiveDist - 150.f) // hysteresis: no flip-flop
			{
				Target = Nearest;
			}
		}
	}
	if (Target != INDEX_NONE)
	{
		SwitchTo(Target);
	}
	// Tutorial steps: got the ball -> shot it -> took it from an opponent.
	if (const ASkateCharacter* Active = GetSkater())
	{
		const USkateBallControlComponent* Ball = Active->GetBallControl();
		if (TutorialStep == 0 && Ball && Ball->HasBall())
		{
			TutorialStep = 1;
		}
		else if (TutorialStep == 1 && Ball && Ball->GetLastImpulse().Kind == ESkateImpulseKind::Kick && Ball->GetTimeSinceLastImpulse() < 0.1f)
		{
			TutorialStep = 2;
		}
	}
}

void ASkatePlayerController::SyncTeamSettings()
{
	// Preset, debug view and ball interaction are toggled on the active skater; the teammate follows.
	const ASkateCharacter* Active = GetSkater();
	if (!Active)
	{
		return;
	}
	for (const TWeakObjectPtr<ASkateCharacter>& Member : Team)
	{
		ASkateCharacter* Mate = Member.Get();
		if (!Mate || Mate == Active)
		{
			continue;
		}
		if (Mate->GetPreset() != Active->GetPreset())
		{
			Mate->SetPreset(Active->GetPreset());
		}
		if (Mate->IsDebugEnabled() != Active->IsDebugEnabled())
		{
			Mate->SetDebugEnabled(Active->IsDebugEnabled());
		}
		if (Mate->IsBallInteractionEnabled() != Active->IsBallInteractionEnabled())
		{
			Mate->SetBallInteractionEnabled(Active->IsBallInteractionEnabled());
		}
	}
}

void ASkatePlayerController::UpdateThroughTargets()
{
	const ASkateArena* Arena = CachedArena.Get();
	if (!Arena)
	{
		return;
	}
	const FSkateGoalFrame Goal = Arena->GetGoalFrame(0); // team 0 attacks +X
	const FVector2D GoalCentre(Goal.Center.X, Goal.Center.Y);
	const FVector2D Half = Arena->GetLayout().RinkSize * 0.5f;
	const float Lead = Team.Num() > 0 && Team[0].IsValid() ? Team[0]->GetActiveTuning().BallControl.ThroughLead : 700.f;
	for (int32 Index = 0; Index < Team.Num(); ++Index)
	{
		ASkateCharacter* Member = Team[Index].Get();
		if (!Member || !Member->GetBallControl())
		{
			continue;
		}
		const ASkateCharacter* Mate = Member->GetBallControl()->GetPassMate();
		if (!Mate)
		{
			Member->GetBallControl()->SetThroughTarget(false, FVector2D::ZeroVector);
			continue;
		}
		const FVector2D MatePos(Mate->GetActorLocation());
		const FVector2D Dir = (GoalCentre - MatePos).GetSafeNormal();
		// A standing teammate gets the ball closer (it cannot run 7 m in time); a running one gets the full lead.
		const float Pace = FMath::Clamp(static_cast<float>(Mate->GetVelocity().Size2D()) / FMath::Max(Mate->GetActiveTuning().Movement.MaxSpeed, 1.f), 0.f, 1.f);
		FVector2D Target = MatePos + Dir * Lead * (0.4f + 0.6f * Pace);
		Target.X = FMath::Clamp(Target.X, -Half.X + 200.f, Goal.Center.X - 150.f); // never behind the goal line
		Target.Y = FMath::Clamp(Target.Y, -Half.Y + 200.f, Half.Y - 200.f);
		Member->GetBallControl()->SetThroughTarget(true, Target);
	}
}

void ASkatePlayerController::DriveAI()
{
	const ASkateArena* Arena = CachedArena.Get();
	const ASkateBall* Ball = Arena ? Arena->GetBall() : nullptr;
	if (!Arena || !Ball)
	{
		return;
	}
	DriveTeam(Team, 0, TeamBrains, TeamModes);
	DriveTeam(Opponents, 1, OpponentBrains, OpponentModes);
}

void ASkatePlayerController::DriveTeam(const TArray<TWeakObjectPtr<ASkateCharacter>>& Group, int32 SkaterTeam, TArray<FSkateSkaterBrain>& Brains, TArray<uint8>& Modes)
{
	const ASkateArena* Arena = CachedArena.Get();
	const ASkateBall* Ball = Arena->GetBall();
	const FVector BallLoc = Ball->GetActorLocation();
	const FVector OwnGoal(Arena->GetGoalFrame(SkaterTeam == 0 ? 1 : 0).Center.X, Arena->GetGoalFrame(SkaterTeam == 0 ? 1 : 0).Center.Y, BallLoc.Z);
	const USkateBallControlComponent* HolderComp = Cast<USkateBallControlComponent>(Ball->GetHolder());
	const ASkateCharacter* Carrier = HolderComp ? Cast<ASkateCharacter>(HolderComp->GetOwner()) : nullptr;
	const bool bOurBall = Carrier && Carrier->GetTeam() == SkaterTeam;
	// Nearest to the ball: the chaser. Nearest to the own goal among the others: holds back (when attacking, only
	// if someone else is still free to support).
	auto Nearest = [&](const FVector& Point, int32 Skip)
	{
		int32 Best = INDEX_NONE;
		float BestDist = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Group.Num(); ++Index)
		{
			const ASkateCharacter* Member = Group[Index].Get();
			const float Dist = Member ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Point)) : BestDist;
			if (Member && Index != Skip && Dist < BestDist)
			{
				BestDist = Dist;
				Best = Index;
			}
		}
		return Best;
	};
	const int32 Chaser = Nearest(BallLoc, INDEX_NONE);
	const int32 Back = (!bOurBall || Group.Num() >= 3) ? Nearest(OwnGoal, Chaser) : INDEX_NONE;
	for (int32 Index = 0; Index < Group.Num(); ++Index)
	{
		ASkateCharacter* Skater = Group[Index].Get();
		const bool bPlayer = SkaterTeam == 0 && Index == ActiveIndex;
		if ((bPlayer && !bBotsVsBots && !bStickLatched) || !Skater || !Brains.IsValidIndex(Index) || !Modes.IsValidIndex(Index))
		{
			continue;
		}
		// The latched active skater gets the AI's steering only: the buttons (and the aim) are the player's.
		DriveSkater(Skater, SkaterTeam, Index == Chaser, Index == Back, Brains[Index], Modes[Index], /*bActions*/ !bPlayer || bBotsVsBots);
		if (SkaterTeam == 0 && !bPlayer)
		{
			TeammateMode = Modes[Index];
		}
	}
}

void ASkatePlayerController::DriveSkater(ASkateCharacter* Skater, int32 SkaterTeam, bool bChaser, bool bHoldBack, FSkateSkaterBrain& Brain, uint8& Mode, bool bActions)
{
	const ASkateArena* Arena = CachedArena.Get();
	const ASkateBall* Ball = Arena->GetBall();
	auto To2D = [](const FVector& V) { return FSkateVec2(static_cast<float>(V.X), static_cast<float>(V.Y)); };
	const FVector BallLoc = Ball->GetActorLocation();

	// Who has the ball: a skater's ball control component or a keeper.
	const ASkateCharacter* Carrier = nullptr;
	bool bKeeperHolds = false;
	if (const USkateBallControlComponent* HolderComp = Cast<USkateBallControlComponent>(Ball->GetHolder()))
	{
		Carrier = Cast<ASkateCharacter>(HolderComp->GetOwner());
	}
	else if (Ball->GetHolder())
	{
		bKeeperHolds = true;
	}

	FSkateSkaterView View;
	const FVector Loc = Skater->GetActorLocation();
	View.Pos = To2D(Loc);
	View.Vel = To2D(Skater->GetVelocity());
	View.Heading = Skater->GetSkateMovement()->GetSkateState().Heading;
	View.bBallValid = true;
	View.BallPos = To2D(BallLoc);
	View.BallVel = To2D(Ball->GetBallVelocity());
	if (Carrier == Skater)
	{
		View.BallOwner = ESkateBallOwner::Me;
	}
	else if (Carrier)
	{
		View.BallOwner = Carrier->GetTeam() == SkaterTeam ? ESkateBallOwner::Teammate : ESkateBallOwner::Opponent;
	}
	else if (bKeeperHolds)
	{
		View.BallOwner = ESkateBallOwner::Keeper;
	}
	View.bBallIsMyPass = Ball->IsPassFrom(Skater->GetBallControl());
	View.bBallIsPassToMe = Ball->IsPassFor(Skater->GetBallControl(), SkaterTeam);
	View.BallDamping = Ball->GetPhysicsTuning().LinearDamping;
	View.BallRollingResistance = Ball->GetPhysicsTuning().RollingResistance;
	// Team 0 attacks goal 0 (+X), team 1 attacks goal 1 (-X).
	const FSkateGoalFrame Attack = Arena->GetGoalFrame(SkaterTeam == 0 ? 0 : 1);
	const FSkateGoalFrame Own = Arena->GetGoalFrame(SkaterTeam == 0 ? 1 : 0);
	View.AttackGoal = Attack.Center;
	View.OwnGoal = Own.Center;
	View.GoalHalfWidth = Attack.HalfWidth;
	View.RinkHalf = To2D(FVector(Arena->GetLayout().RinkSize * 0.5f, 0.f));
	View.CornerRadius = Arena->GetLayout().CornerRadius;
	View.bChaser = bChaser;
	View.bHoldBack = bHoldBack;
	for (const TWeakObjectPtr<ASkateCharacter>& Member : SkaterTeam == 0 ? Team : Opponents)
	{
		const ASkateCharacter* Mate = Member.Get();
		if (Mate && Mate != Skater && View.MateCount < FSkateSkaterView::MaxMates)
		{
			View.MatePos[View.MateCount] = To2D(Mate->GetActorLocation());
			View.MateVel[View.MateCount] = To2D(Mate->GetVelocity());
			++View.MateCount;
		}
	}
	// The nearest opponent is the threat; the nearest one that is not on the ball is the one to mark.
	float ThreatDist = TNumericLimits<float>::Max();
	float MarkDist = TNumericLimits<float>::Max();
	const ASkateCharacter* OnBall = Carrier;
	if (!OnBall)
	{
		float OnBallDist = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<ASkateCharacter>& Member : SkaterTeam == 0 ? Opponents : Team)
		{
			const float Dist = Member.IsValid() ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), BallLoc)) : OnBallDist;
			if (Member.IsValid() && Dist < OnBallDist)
			{
				OnBallDist = Dist;
				OnBall = Member.Get();
			}
		}
	}
	for (const TWeakObjectPtr<ASkateCharacter>& Member : SkaterTeam == 0 ? Opponents : Team)
	{
		const ASkateCharacter* Threat = Member.Get();
		const float Dist = Threat ? static_cast<float>(FVector::Dist2D(Threat->GetActorLocation(), Loc)) : ThreatDist;
		if (Threat && Dist < ThreatDist)
		{
			ThreatDist = Dist;
			View.bThreatValid = true;
			View.ThreatPos = To2D(Threat->GetActorLocation());
		}
		if (Threat && Threat != OnBall && Dist < MarkDist)
		{
			MarkDist = Dist;
			View.bMarkValid = true;
			View.MarkPos = To2D(Threat->GetActorLocation());
		}
	}

	FSkateSkaterDecision Decision = FSkateSkaterAI::Think(View, Arena->GetAITuning(Skater->GetActiveTuning().AI), Brain, GetWorld()->GetDeltaSeconds());
	if (Arena->IsGoalPause() || Arena->IsFaceOff() || Arena->GetTimeSinceDrop() < Skater->GetActiveTuning().AI.FaceOffReaction)
	{
		Decision = FSkateSkaterDecision();
		Decision.Move.Brake = 1.f;
	}
	Skater->ApplyMoveInput(Decision.Move, bActions ? &Decision.Actions : nullptr, /*bKeepAim*/ !bActions);
	if (Decision.bCheck && bActions)
	{
		Skater->StartTake();
	}
	if (Mode != static_cast<uint8>(Decision.Mode))
	{
		Mode = static_cast<uint8>(Decision.Mode);
		UE_LOG(LogIceSkate, Verbose, TEXT("%s %d: %s at (%.0f, %.0f), ball (%.0f, %.0f)"), SkaterTeam == 0 ? TEXT("Mate") : TEXT("CPU"),
			Skater->GetTeamSlot() + 1, ANSI_TO_TCHAR(SkateSkaterModeName(Decision.Mode)), Loc.X, Loc.Y, BallLoc.X, BallLoc.Y);
	}
}

void ASkatePlayerController::PlayerTick(float DeltaTime)
{
	// Processes input first: the handlers below update the stored values / edges.
	Super::PlayerTick(DeltaTime);

	if (!CachedArena.IsValid())
	{
		CachedArena = ASkateArena::Find(GetWorld());
	}
	RefreshTeam();
	UpdateAutoSwitch();
	UpdateThroughTargets();

	ASkateCharacter* Skater = GetSkater();
	if (Skater)
	{
		if (CameraRig)
		{
			CameraRig->Tuning = Skater->GetActiveTuning().Camera;
			if (!CachedArena.IsValid())
			{
				CachedArena = ASkateArena::Find(GetWorld());
			}
			if (const ASkateArena* Arena = CachedArena.Get())
			{
				CameraRig->SetStaticFocus(Arena->GetRinkCenter()); // the rink may settle onto the level's ground
			}
			// Keep the pass partner in frame while holding the ball, otherwise the ball itself.
			AActor* Interest = Skater->GetBallControl()->GetBall();
			if (Skater->GetBallControl()->HasBall() && Skater->GetBallControl()->GetPassMate())
			{
				Interest = Skater->GetBallControl()->GetPassMate();
			}
			CameraRig->SetInterest(Interest);
		}

		FSkateFrameInput FrameInput;
		const bool bUseKeys = KeysValue.SizeSquared() > 0.01 && StickValue.Size() < SkatePlayerControllerDetail::StickIdleRadius;
		if (bUseKeys)
		{
			FVector2D Keys = KeysValue;
			if (Keys.Size() > 1.0)
			{
				Keys.Normalize();
			}
			FrameInput.RawStick = Keys * (bSlowHeld ? Skater->GetActiveTuning().Input.KeyboardSlowMagnitude : 1.f);
			FrameInput.bFromKeyboard = true;
		}
		else
		{
			FrameInput.RawStick = StickValue;
		}
		FrameInput.CameraYawDeg = CameraRig ? CameraRig->GetControlYaw() : 0.f;
		FrameInput.BrakeRaw = BrakeValue;
		FrameInput.BoostRaw = BoostValue;
		FrameInput.bPushPressed = bPushEdge;
		FrameInput.bPushReleased = bPushReleaseEdge;
		FrameInput.bKickPressed = bKickPressEdge;
		FrameInput.bKickReleased = bKickReleaseEdge;
		FrameInput.bThroughPressed = bThroughEdge;
		if (CachedArena.IsValid() && CachedArena->IsFaceOff())
		{
			// Everybody waits for the drop: no stick, no buttons, skates held.
			const float Yaw = FrameInput.CameraYawDeg;
			FrameInput = FSkateFrameInput();
			FrameInput.CameraYawDeg = Yaw;
			FrameInput.BrakeRaw = 1.f;
		}
		LastRawStick = FrameInput.RawStick;
		// A pass on its way to this skater: the AI keeps it on the ball's line until the stick is re-aimed or the
		// ball arrives, whatever the buttons do (X winds up a one-timer meanwhile).
		const ASkateBall* Ball = CachedArena.IsValid() ? CachedArena->GetBall() : nullptr;
		const bool bPassInFlight = Ball && !Ball->GetHolder() && Ball->IsPassFor(Skater->GetBallControl(), Skater->GetTeam()) && Ball->GetBallVelocity().Size2D() > 250.f;
		if (bStickLatched)
		{
			LatchTime += DeltaTime;
			const bool bReleased = FrameInput.RawStick.Size() < 0.3;
			const bool bMoved = !bReleased && FVector2D::DotProduct(FrameInput.RawStick.GetSafeNormal(), LatchStick.GetSafeNormal()) < 0.5; // > 60 deg
			const bool bButton = !bPassInFlight && (bPushEdge || bKickPressEdge || bThroughEdge);
			if (bReleased || bMoved || bButton || (!bPassInFlight && LatchTime > 0.6f))
			{
				bStickLatched = false;
			}
		}
		// Movement runs right after this (the pawn's movement ticks after its controller): no added latency.
		// While the stick is latched the new skater is still steered by the AI (DriveAI overrides the movement below),
		// e.g. to receive the pass; the buttons still reach it and the stick stays the aim of a one-touch shot / pass.
		if (!bBotsVsBots)
		{
			Skater->ApplyFrameInput(FrameInput);
		}
	}
	bPushEdge = false;
	bPushReleaseEdge = false;
	bKickPressEdge = false;
	bKickReleaseEdge = false;
	bThroughEdge = false;
	TimeSinceManualSwitch += DeltaTime;
	if (TutorialStep == 2 && Skater && Skater->GetTimeSinceTake() < 0.05f)
	{
		TutorialStep = 3;
	}

	SyncTeamSettings();
	DriveAI();
}

// ---- Axis handlers ----

void ASkatePlayerController::OnStick(const FInputActionValue& Value)
{
	StickValue = Value.Get<FVector2D>();
	bLastInputGamepad = true;
}

void ASkatePlayerController::OnStickReleased(const FInputActionValue& Value)
{
	StickValue = FVector2D::ZeroVector;
}

void ASkatePlayerController::OnKeys(const FInputActionValue& Value)
{
	KeysValue = Value.Get<FVector2D>();
	bLastInputGamepad = false;
}

void ASkatePlayerController::OnKeysReleased(const FInputActionValue& Value)
{
	KeysValue = FVector2D::ZeroVector;
}

void ASkatePlayerController::OnBrake(const FInputActionValue& Value)
{
	BrakeValue = Value.Get<float>();
}

void ASkatePlayerController::OnBrakeReleased(const FInputActionValue& Value)
{
	BrakeValue = 0.f;
}

void ASkatePlayerController::OnThrough(const FInputActionValue& Value)
{
	bThroughEdge = true;
	bStickLatched = false;
}

void ASkatePlayerController::OnBoost(const FInputActionValue& Value)
{
	BoostValue = Value.Get<float>();
}

void ASkatePlayerController::OnBoostReleased(const FInputActionValue& Value)
{
	BoostValue = 0.f;
}

void ASkatePlayerController::OnSlowPressed(const FInputActionValue& Value)
{
	bSlowHeld = true;
}

void ASkatePlayerController::OnSlowReleased(const FInputActionValue& Value)
{
	bSlowHeld = false;
}

// ---- Buttons ----

void ASkatePlayerController::OnPushPressed(const FInputActionValue& Value)
{
	bPushEdge = true;
}

void ASkatePlayerController::OnPushReleased(const FInputActionValue& Value)
{
	bPushReleaseEdge = true;
}

void ASkatePlayerController::OnKickPressed(const FInputActionValue& Value)
{
	bKickPressEdge = true;
}

void ASkatePlayerController::OnKickReleased(const FInputActionValue& Value)
{
	bKickReleaseEdge = true;
}

void ASkatePlayerController::OnReset(const FInputActionValue& Value)
{
	if (ASkateArena* Arena = ASkateArena::Find(GetWorld()))
	{
		Arena->RestartMatch();
	}
	if (CameraRig)
	{
		CameraRig->SnapToTarget();
	}
	bKickPressEdge = false;
	bKickReleaseEdge = false;
	bPushEdge = false;
	bPushReleaseEdge = false;
}

void ASkatePlayerController::OnBallToFeet(const FInputActionValue& Value)
{
	if (ASkateArena* Arena = ASkateArena::Find(GetWorld()))
	{
		Arena->PlaceBallInFront(GetSkater());
	}
}

void ASkatePlayerController::OnToggleDebug(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->SetDebugEnabled(!Skater->IsDebugEnabled());
	}
}

void ASkatePlayerController::OnToggleChase(const FInputActionValue& Value)
{
	if (CameraRig)
	{
		CameraRig->ToggleChaseMode();
	}
}

void ASkatePlayerController::OnToggleCamera(const FInputActionValue& Value)
{
	if (CameraRig)
	{
		CameraRig->ToggleStaticMode();
	}
}

void ASkatePlayerController::OnPresetNext(const FInputActionValue& Value)
{
	if (ASkateArena* Arena = ASkateArena::Find(GetWorld()))
	{
		Arena->CycleDifficulty(+1);
	}
}

void ASkatePlayerController::OnPresetPrev(const FInputActionValue& Value)
{
	if (ASkateArena* Arena = ASkateArena::Find(GetWorld()))
	{
		Arena->CycleDifficulty(-1);
	}
}

void ASkatePlayerController::OnPreset1(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->SetPreset(ESkatePreset::Responsive);
	}
}

void ASkatePlayerController::OnPreset2(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->SetPreset(ESkatePreset::Balanced);
	}
}

void ASkatePlayerController::OnPreset3(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->SetPreset(ESkatePreset::Inertial);
	}
}

void ASkatePlayerController::OnCycleFpsCap(const FInputActionValue& Value)
{
	using namespace SkatePlayerControllerDetail;
	FpsCapIndex = (FpsCapIndex + 1) % NumFpsCaps;
	// VSync would hide the cap; turn it off while testing frame rates.
	ConsoleCommand(TEXT("r.VSync 0"));
	ConsoleCommand(FString::Printf(TEXT("t.MaxFPS %d"), FpsCaps[FpsCapIndex]));
}

void ASkatePlayerController::OnSwitchSkater(const FInputActionValue& Value)
{
	if (Team.Num() > 1)
	{
		// By hand: immediate control, and the automatic rules stay out of the way for a while.
		SwitchTo((ActiveIndex + 1) % Team.Num(), /*bLatchStick*/ false);
		TimeSinceManualSwitch = 0.f;
	}
}

void ASkatePlayerController::OnToggleBall(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->SetBallInteractionEnabled(!Skater->IsBallInteractionEnabled());
	}
}

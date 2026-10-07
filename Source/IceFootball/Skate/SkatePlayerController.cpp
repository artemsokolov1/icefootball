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
#include "Skate/Core/SkateTeamAI.h"
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
	// Auto-switch on a pass: the teammate must be within this angle (deg) of the pass and this distance (cm).
	constexpr float PassSwitchAngle = 35.f;
	constexpr float PassSwitchRange = 3500.f;
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

	// ---- Ball ----
	IA_Push = MakeAction(TEXT("IA_Skate_Push"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Push, EKeys::Gamepad_FaceButton_Bottom);
	Imc->MapKey(IA_Push, EKeys::J);

	IA_Kick = MakeAction(TEXT("IA_Skate_Kick"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Kick, EKeys::Gamepad_FaceButton_Left);
	Imc->MapKey(IA_Kick, EKeys::K);

	// ---- Test tools ----
	IA_Reset = MakeAction(TEXT("IA_Skate_Reset"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_Reset, EKeys::Gamepad_FaceButton_Top);
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

	IA_FpsCap = MakeAction(TEXT("IA_Skate_FpsCap"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_FpsCap, EKeys::Gamepad_DPad_Down);
	Imc->MapKey(IA_FpsCap, EKeys::F3);

	IA_ToggleBall = MakeAction(TEXT("IA_Skate_ToggleBall"), EInputActionValueType::Boolean);
	Imc->MapKey(IA_ToggleBall, EKeys::Gamepad_Special_Right);
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

	Eic->BindAction(IA_Push, ETriggerEvent::Started, this, &ASkatePlayerController::OnPushPressed);
	Eic->BindAction(IA_Push, ETriggerEvent::Completed, this, &ASkatePlayerController::OnPushReleased);
	Eic->BindAction(IA_Kick, ETriggerEvent::Started, this, &ASkatePlayerController::OnKickPressed);
	Eic->BindAction(IA_Kick, ETriggerEvent::Completed, this, &ASkatePlayerController::OnKickReleased);

	Eic->BindAction(IA_Reset, ETriggerEvent::Started, this, &ASkatePlayerController::OnReset);
	Eic->BindAction(IA_BallToFeet, ETriggerEvent::Started, this, &ASkatePlayerController::OnBallToFeet);
	Eic->BindAction(IA_Debug, ETriggerEvent::Started, this, &ASkatePlayerController::OnToggleDebug);
	Eic->BindAction(IA_Camera, ETriggerEvent::Started, this, &ASkatePlayerController::OnToggleCamera);
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
	return FString(ANSI_TO_TCHAR(SkateTeammateModeName(static_cast<ESkateTeammateMode>(TeammateMode))));
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
	if (bAllValid && Found == Team.Num())
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
	SeenAcquires.Reset();
	SeenImpulses.Reset();
	ActiveIndex = 0;
	for (ASkateCharacter* Skater : Skaters)
	{
		if (Skater == Active)
		{
			ActiveIndex = Team.Num();
		}
		Team.Add(Skater);
		const USkateBallControlComponent* Ball = Skater->GetBallControl();
		SeenAcquires.Add(Ball ? Ball->GetAcquireCount() : 0);
		SeenImpulses.Add(Ball ? Ball->GetImpulseCount() : 0);
		if (Skater != GetPawn())
		{
			// Input is applied to the teammate in PlayerTick: its movement must tick after this controller.
			AddPawnTickDependency(Skater);
		}
	}
}

void ASkatePlayerController::SwitchTo(int32 Index)
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
	using namespace SkatePlayerControllerDetail;
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
		// The player just passed: take over the teammate the pass goes to.
		if (Index == ActiveIndex && bNewImpulse && Ball->GetLastImpulse().Kind == ESkateImpulseKind::Push && Target == INDEX_NONE)
		{
			const FVector2D Dir(Ball->GetLastImpulse().Direction.X, Ball->GetLastImpulse().Direction.Y);
			float BestCos = FMath::Cos(FMath::DegreesToRadians(PassSwitchAngle));
			for (int32 Mate = 0; Mate < Team.Num(); ++Mate)
			{
				const ASkateCharacter* Receiver = Team[Mate].Get();
				if (Mate == Index || !Receiver)
				{
					continue;
				}
				const FVector2D To = FVector2D(Receiver->GetActorLocation() - Skater->GetActorLocation());
				const float Dist = static_cast<float>(To.Size());
				const float Cos = Dist > 1.f ? static_cast<float>(FVector2D::DotProduct(To / Dist, Dir)) : 0.f;
				if (Dist < PassSwitchRange && Cos > BestCos)
				{
					BestCos = Cos;
					Target = Mate;
				}
			}
		}
	}
	if (Target != INDEX_NONE)
	{
		SwitchTo(Target);
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

void ASkatePlayerController::DriveTeammates()
{
	const ASkateArena* Arena = CachedArena.Get();
	const ASkateBall* Ball = Arena ? Arena->GetBall() : nullptr;
	for (int32 Index = 0; Index < Team.Num(); ++Index)
	{
		ASkateCharacter* Mate = Team[Index].Get();
		if (Index == ActiveIndex || !Mate)
		{
			continue;
		}
		const USkateBallControlComponent* MateBall = Mate->GetBallControl();
		FSkateTeammateView View;
		const FVector Loc = Mate->GetActorLocation();
		const FVector Vel = Mate->GetVelocity();
		View.Pos = FSkateVec2(static_cast<float>(Loc.X), static_cast<float>(Loc.Y));
		View.Vel = FSkateVec2(static_cast<float>(Vel.X), static_cast<float>(Vel.Y));
		View.Heading = Mate->GetSkateMovement()->GetSkateState().Heading;
		View.bHasBall = MateBall && MateBall->HasBall();
		if (Ball)
		{
			const FVector BallLoc = Ball->GetActorLocation();
			const FVector BallVel = Ball->GetBallVelocity();
			View.bBallValid = true;
			View.BallPos = FSkateVec2(static_cast<float>(BallLoc.X), static_cast<float>(BallLoc.Y));
			View.BallVel = FSkateVec2(static_cast<float>(BallVel.X), static_cast<float>(BallVel.Y));
			View.bBallHeld = Ball->IsHeldByOther(MateBall);
		}
		ESkateTeammateMode Mode = ESkateTeammateMode::Wait;
		Mate->ApplyMoveInput(FSkateTeammateAI::Think(View, &Mode));
		TeammateMode = static_cast<uint8>(Mode);
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
		// Movement runs right after this (the pawn's movement ticks after its controller): no added latency.
		Skater->ApplyFrameInput(FrameInput);
	}
	bPushEdge = false;
	bPushReleaseEdge = false;
	bKickPressEdge = false;
	bKickReleaseEdge = false;

	SyncTeamSettings();
	DriveTeammates();
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
		Arena->ResetScene();
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

void ASkatePlayerController::OnToggleCamera(const FInputActionValue& Value)
{
	if (CameraRig)
	{
		CameraRig->ToggleStaticMode();
	}
}

void ASkatePlayerController::OnPresetNext(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->CyclePreset(+1);
	}
}

void ASkatePlayerController::OnPresetPrev(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->CyclePreset(-1);
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
		SwitchTo((ActiveIndex + 1) % Team.Num());
	}
}

void ASkatePlayerController::OnToggleBall(const FInputActionValue& Value)
{
	if (ASkateCharacter* Skater = GetSkater())
	{
		Skater->SetBallInteractionEnabled(!Skater->IsBallInteractionEnabled());
	}
}

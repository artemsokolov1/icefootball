// Ice skating prototype - input (Enhanced Input, created in code: no input assets needed).
//
// Gamepad (primary)                      Keyboard (technical checks)
//   Left stick   move / accelerate          WASD or arrows (hold Left Alt = half stick)
//   LT           brake (analog)             Space
//   RT           boost (analog)             Left Shift
//   A            short push of the ball     J
//   X (hold)     charge kick, release=kick  K
//   Y            reset skater + ball        R
//   RB           ball to feet (test aid)    T
//   View/Back    debug HUD on/off           F1
//   D-pad Up     follow / static camera     F2
//   D-pad L / R  previous / next preset     1 / 2 / 3 (direct)
//   D-pad Down   FPS cap 0/30/60/120        F3
//   Menu/Start   ball interaction on/off    F4
//
// The stick is read raw; ASkateCharacter applies the radial dead zone and response curve,
// then projects it onto the ice relative to the fixed camera yaw. No input smoothing.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SkatePlayerController.generated.h"

class ASkateArena;
class ASkateCameraRig;
class ASkateCharacter;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
enum class EInputActionValueType : uint8;

UCLASS()
class ICEFOOTBALL_API ASkatePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASkatePlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;

	ASkateCameraRig* GetCameraRig() const { return CameraRig; }
	int32 GetFpsCap() const;
	bool WasLastInputGamepad() const { return bLastInputGamepad; }

protected:
	UPROPERTY(Transient)
	TObjectPtr<ASkateCameraRig> CameraRig;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;

private:
	UInputAction* MakeAction(const TCHAR* Name, EInputActionValueType ValueType);
	void BuildInputMappings();
	ASkateCharacter* GetSkater() const;
	void EnsureCameraRig();

	// Axis handlers
	void OnStick(const FInputActionValue& Value);
	void OnStickReleased(const FInputActionValue& Value);
	void OnKeys(const FInputActionValue& Value);
	void OnKeysReleased(const FInputActionValue& Value);
	void OnBrake(const FInputActionValue& Value);
	void OnBrakeReleased(const FInputActionValue& Value);
	void OnBoost(const FInputActionValue& Value);
	void OnBoostReleased(const FInputActionValue& Value);
	void OnSlowPressed(const FInputActionValue& Value);
	void OnSlowReleased(const FInputActionValue& Value);

	// Button handlers
	void OnPushPressed(const FInputActionValue& Value);
	void OnPushReleased(const FInputActionValue& Value);
	void OnKickPressed(const FInputActionValue& Value);
	void OnKickReleased(const FInputActionValue& Value);
	void OnReset(const FInputActionValue& Value);
	void OnBallToFeet(const FInputActionValue& Value);
	void OnToggleDebug(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
	void OnPresetNext(const FInputActionValue& Value);
	void OnPresetPrev(const FInputActionValue& Value);
	void OnPreset1(const FInputActionValue& Value);
	void OnPreset2(const FInputActionValue& Value);
	void OnPreset3(const FInputActionValue& Value);
	void OnCycleFpsCap(const FInputActionValue& Value);
	void OnToggleBall(const FInputActionValue& Value);

	UInputAction* IA_Stick = nullptr;
	UInputAction* IA_Keys = nullptr;
	UInputAction* IA_Slow = nullptr;
	UInputAction* IA_Brake = nullptr;
	UInputAction* IA_Boost = nullptr;
	UInputAction* IA_Push = nullptr;
	UInputAction* IA_Kick = nullptr;
	UInputAction* IA_Reset = nullptr;
	UInputAction* IA_BallToFeet = nullptr;
	UInputAction* IA_Debug = nullptr;
	UInputAction* IA_Camera = nullptr;
	UInputAction* IA_PresetNext = nullptr;
	UInputAction* IA_PresetPrev = nullptr;
	UInputAction* IA_Preset1 = nullptr;
	UInputAction* IA_Preset2 = nullptr;
	UInputAction* IA_Preset3 = nullptr;
	UInputAction* IA_FpsCap = nullptr;
	UInputAction* IA_ToggleBall = nullptr;

	TWeakObjectPtr<ASkateArena> CachedArena;
	FVector2D StickValue = FVector2D::ZeroVector;
	FVector2D KeysValue = FVector2D::ZeroVector;
	bool bSlowHeld = false;
	float BrakeValue = 0.f;
	float BoostValue = 0.f;
	bool bPushEdge = false;
	bool bPushReleaseEdge = false;
	bool bKickPressEdge = false;
	bool bKickReleaseEdge = false;
	bool bLastInputGamepad = true;
	int32 FpsCapIndex = 0;
};

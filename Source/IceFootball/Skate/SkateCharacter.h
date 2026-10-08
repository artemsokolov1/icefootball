// Ice skating prototype - the skater pawn.
//
// Owns the tuning presets (one FSkateTuning per preset) and routes the per-frame input from
// ASkatePlayerController into the movement component and the ball control component.
// Visuals are a procedural placeholder (USkaterPuppetComponent); the skeletal mesh is unused.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Skate/Core/SkateInput.h"
#include "Skate/Core/SkateModel.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateCharacter.generated.h"

class USkateMovementComponent;
class USkateBallControlComponent;
class USkaterPuppetComponent;
class USkateFeedbackComponent;
class USkateIceSynth;
struct FSkateBallActionInput;

/** Raw per-frame input gathered by the controller (before shaping). */
struct FSkateFrameInput
{
	/** Stick (or keyboard) deflection: X = right, Y = up, camera space. */
	FVector2D RawStick = FVector2D::ZeroVector;
	bool bFromKeyboard = false;
	/** Camera yaw (deg) used to project the stick onto the ice. */
	float CameraYawDeg = 0.f;
	float BrakeRaw = 0.f;
	float BoostRaw = 0.f;
	bool bPushPressed = false;
	bool bPushReleased = false;
	bool bKickPressed = false;
	bool bKickReleased = false;
};

UCLASS()
class ICEFOOTBALL_API ASkateCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASkateCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	/** Live tuning: editing a preset in the Details panel during PIE applies it immediately. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Called by the controller every frame before movement runs. */
	void ApplyFrameInput(const FSkateFrameInput& Input);

	/** AI skaters: a ready world-space skate input (no stick shaping) and optional ball buttons. */
	void ApplyMoveInput(const FSkateMoveInput& Input, const FSkateBallActionInput* Actions = nullptr);

	/** Drops a pass / shot wind-up in progress (the player switched to the other skater). */
	void CancelBallActions();

	/** Body checks: capsule contact with a skater of the other team (see FSkateHit). */
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
		FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
	/** Shoved by a check: new velocity, no stick and no ball for Stun seconds. */
	void ApplyHit(const FVector2D& NewVelocity, float Stun);
	bool IsStunned() const { return StunLeft > 0.f; }
	/** The take button (B): an opponent's ball within TakeRange is knocked to our feet. Otherwise, when the match
	 *  allows hits, a body check: a short forward lunge and a window in which contact with an opponent is a hit. */
	void StartTake();
	void StartCheck();
	bool IsChecking() const { return CheckLeft > 0.f; }
	/** An opponent's ball is close enough for the take button right now (HUD ring / prompt). */
	bool CanTakeNow() const;
	float GetTimeSinceTake() const { return TimeSinceTake; }
	/** Seconds since this skater delivered or took a check (HUD flash, cooldown). */
	float GetTimeSinceHit() const { return TimeSinceHit; }

	/** Team 0 = the player's (attacks +X), 1 = the opponents (AI). Slot 0 / 1 within the team: spawn point, chest patch. */
	void SetTeam(int32 InTeam) { Team = InTeam; }
	int32 GetTeam() const { return Team; }
	void SetTeamSlot(int32 InSlot) { TeamSlot = InSlot; }
	int32 GetTeamSlot() const { return TeamSlot; }

	/** Teleports and stops the skater (scene reset). */
	void ResetSkater(const FTransform& Transform);

	// ---- Presets ----
	void SetPreset(ESkatePreset Preset);
	void CyclePreset(int32 Direction);
	ESkatePreset GetPreset() const { return ActivePreset; }
	const FSkateTuning& GetActiveTuning() const;

	// ---- Toggles ----
	void SetBallInteractionEnabled(bool bEnabled);
	bool IsBallInteractionEnabled() const { return bBallInteractionEnabled; }
	void SetDebugEnabled(bool bEnabled) { bDebugEnabled = bEnabled; }
	bool IsDebugEnabled() const { return bDebugEnabled; }

	// ---- Accessors for HUD / feedback ----
	USkateMovementComponent* GetSkateMovement() const { return SkateMovement; }
	USkateBallControlComponent* GetBallControl() const { return BallControl; }
	USkaterPuppetComponent* GetPuppet() const { return Puppet; }
	USkateFeedbackComponent* GetFeedback() const { return Feedback; }
	const FSkateStickResult& GetLastStick() const { return LastStick; }
	const FSkateFrameInput& GetLastFrameInput() const { return LastFrameInput; }
	float GetLastBrake() const { return LastBrake; }
	float GetLastBoost() const { return LastBoost; }

	/** The three presets. Defaults come from SkateTuningPresets::Make(); edit them here in PIE/BP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate|Tuning", meta = (DisplayName = "1 Responsive"))
	FSkateTuning TuningResponsive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate|Tuning", meta = (DisplayName = "2 Balanced"))
	FSkateTuning TuningBalanced;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate|Tuning", meta = (DisplayName = "3 Inertial"))
	FSkateTuning TuningInertial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skate|Tuning")
	ESkatePreset ActivePreset = ESkatePreset::Balanced;

protected:
	/** Pushes the active preset into all components. Re-run after editing presets live. */
	UFUNCTION(CallInEditor, Category = "Skate|Tuning")
	void ApplyActiveTuning();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skate")
	TObjectPtr<USkateMovementComponent> SkateMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skate")
	TObjectPtr<USkateBallControlComponent> BallControl;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skate")
	TObjectPtr<USkaterPuppetComponent> Puppet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skate")
	TObjectPtr<USkateFeedbackComponent> Feedback;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skate")
	TObjectPtr<USkateIceSynth> IceSynth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate")
	bool bBallInteractionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate")
	bool bDebugEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate")
	int32 Team = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate")
	int32 TeamSlot = 0;

private:
	void DrawMovementDebug() const;

	FSkateStickResult LastStick;
	FSkateFrameInput LastFrameInput;
	float StunLeft = 0.f;
	float CheckLeft = 0.f;
	float TimeSinceCheck = 100.f;
	float TimeSinceTake = 100.f;
	float TimeSinceHit = 100.f;
	float LastBrake = 0.f;
	float LastBoost = 0.f;
};

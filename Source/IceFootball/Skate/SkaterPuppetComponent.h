// Ice skating prototype - honest placeholder skater: a procedural puppet made of basic shapes.
//
// There are no skating animation clips in the project, so instead of playing a run cycle on a
// sliding character, the pose is generated from the actual movement state:
//   stance       - knees bent, blades parallel
//   push stroke  - alternate legs push out sideways-back (V stroke), arms swing; only while thrusting
//   glide        - feet together, no stride
//   carve        - body leans into the turn from lateral acceleration, blades edge
//   brake        - lower body twists so both skates are across the travel (hockey stop), lean back
//   touch / push / kick - right leg taps or swings (kick: wind-up while charging, swing on release)
// Legs use 2-bone IK so the blades stay on the ice. The capsule (movement) is the only source of
// motion; the pose never feeds back and there is no root motion.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Skate/Core/SkateTuning.h"
#include "SkaterPuppetComponent.generated.h"

class UStaticMeshComponent;

UCLASS(ClassGroup = (Skate))
class ICEFOOTBALL_API USkaterPuppetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USkaterPuppetComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetAnimTuning(const FSkateAnimTuning& InTuning) { Tuning = InTuning; }
	void ResetPose();

	/** Blade contact point on the ice (Side 0 = left, 1 = right), world space, X axis along the blade. */
	FTransform GetBladeWorldTransform(int32 Side) const;
	bool IsBladeOnIce(int32 Side) const { return FootLift[Side == 0 ? 0 : 1] < 1.5f; }

	/** Short label of the dominant pose layer, for the debug HUD. */
	const TCHAR* GetPoseLabel() const { return PoseLabel; }
	float GetLeanDeg() const { return LeanRight; }
	float GetBrakeTwistDeg() const { return Twist; }

private:
	void BuildParts();
	void UpdatePose(float Dt);

	static FVector SolveKnee(const FVector& Hip, FVector& Ankle, float ThighLen, float ShinLen, const FVector& Pole);

	FSkateAnimTuning Tuning;
	bool bBuilt = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UStaticMeshComponent* Pelvis = nullptr;
	UStaticMeshComponent* Torso = nullptr;
	UStaticMeshComponent* ChestMark = nullptr;
	UStaticMeshComponent* Head = nullptr;
	UStaticMeshComponent* Visor = nullptr;
	UStaticMeshComponent* Thigh[2] = { nullptr, nullptr };
	UStaticMeshComponent* Shin[2] = { nullptr, nullptr };
	UStaticMeshComponent* Arm[2] = { nullptr, nullptr };
	UStaticMeshComponent* Boot[2] = { nullptr, nullptr };
	UStaticMeshComponent* Blade[2] = { nullptr, nullptr };

	// Smoothed pose state (degrees / cm / 0..1)
	float LeanRight = 0.f;
	float LeanForward = 0.f;
	float Crouch = 8.f;
	float Twist = 0.f;
	float TwistSign = 1.f;
	float BrakeAmount = 0.f;
	float StrideAmp = 0.f;
	float StridePhase = 0.f;
	float Charge = 0.f;

	FVector BladeLocal[2];
	float BladeYaw[2] = { 0.f, 0.f };
	float FootLift[2] = { 0.f, 0.f };
	const TCHAR* PoseLabel = TEXT("Stance");
};

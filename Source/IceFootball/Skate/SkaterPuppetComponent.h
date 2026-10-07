// Ice skating prototype - honest placeholder skater built from basic shapes.
//
// The pose itself is computed by FSkatePoseSolver (Skate/Core/SkatePose.*, engine independent and
// unit tested): stance, push stroke, glide, carve lean, hockey stop, touch / push / kick leg swing,
// arms with elbows that swing against the legs. This component only feeds it the real movement
// state and places the meshes. The capsule (movement) is the only source of motion; the pose never
// feeds back and there is no root motion.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Skate/Core/SkatePose.h"
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
	/** Chest patch colour (set before BeginPlay). */
	void SetMarkColor(const FLinearColor& InColor) { MarkColor = InColor; }
	/** Jersey colours (set before BeginPlay): one per team. */
	void SetJerseyColor(const FLinearColor& Main, const FLinearColor& Dark) { JerseyColor = Main; JerseyDarkColor = Dark; }
	void ResetPose();

	/** Blade contact point on the ice (Side 0 = left, 1 = right), world space, X axis along the blade. */
	FTransform GetBladeWorldTransform(int32 Side) const;
	bool IsBladeOnIce(int32 Side) const { return Pose.FootLift[Side == 0 ? 0 : 1] < 1.5f; }

	/** Short label of the dominant pose layer, for the debug HUD. */
	const TCHAR* GetPoseLabel() const { return *PoseLabel; }
	float GetLeanDeg() const { return Pose.LeanRightDeg; }
	float GetBrakeTwistDeg() const { return Pose.TwistDeg; }

private:
	void BuildParts();
	void ApplyPose();

	FSkateAnimTuning Tuning;
	FSkatePoseState PoseState;
	FSkatePose Pose;
	FString PoseLabel = TEXT("Stance");
	bool bBuilt = false;
	FLinearColor MarkColor = FLinearColor(0.95f, 0.95f, 0.95f);
	FLinearColor JerseyColor = FLinearColor(0.85f, 0.18f, 0.08f);
	FLinearColor JerseyDarkColor = FLinearColor(0.62f, 0.12f, 0.05f);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UStaticMeshComponent* PelvisPart = nullptr;
	UStaticMeshComponent* ChestPart = nullptr;
	UStaticMeshComponent* ChestMarkPart = nullptr;
	UStaticMeshComponent* NeckPart = nullptr;
	UStaticMeshComponent* HeadPart = nullptr;
	UStaticMeshComponent* VisorPart = nullptr;
	UStaticMeshComponent* ShoulderPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* UpperArmPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* ForearmPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* HandPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* ThighPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* ShinPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* BootPart[2] = { nullptr, nullptr };
	UStaticMeshComponent* BladePart[2] = { nullptr, nullptr };
};

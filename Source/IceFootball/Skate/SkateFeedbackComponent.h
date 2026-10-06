// Ice skating prototype - feedback that mirrors the real action:
//   * glide hiss by speed, brake scrape by actual braking/skid deceleration (USkateIceSynth)
//   * short thump on every ball impulse (strength by delta-v), soft knock on board bounces
//   * thin blade marks from each skate that is on the ice; wider white scrape marks + ice spray
//     only while the real deceleration exceeds a threshold (hard stop / skid)
//   * weak, short rumble on push / kick (no rumble on plain skating, no camera shake, no hit-stop)
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Skate/Core/SkateBallControl.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateFeedbackComponent.generated.h"

class UInstancedStaticMeshComponent;
class USkateIceSynth;

UCLASS(ClassGroup = (Skate))
class ICEFOOTBALL_API USkateFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkateFeedbackComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetTuning(const FSkateFeedbackTuning& InTuning) { Tuning = InTuning; }
	void SetSynth(USkateIceSynth* InSynth) { Synth = InSynth; }

	/** Called once per applied ball impulse. */
	void OnBallImpulse(const FSkateBallImpulse& Impulse, const FVector& BallLocation);

	void ClearMarks();

private:
	struct FMark
	{
		double Time = -1000.0;
		FTransform Transform;
	};
	struct FChip
	{
		float Age = 100.f;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
	};

	void EmitMarks(float DeltaTime, float Speed, bool bScraping);
	void AddMark(UInstancedStaticMeshComponent* Ism, TArray<FMark>& Ring, int32& Next, const FVector& A, const FVector& B, float Width);
	void FadeMarks(UInstancedStaticMeshComponent* Ism, TArray<FMark>& Ring);
	void UpdateSpray(float DeltaTime, float Intensity, const FVector& SkaterVelocity);
	void Rumble(float Intensity, float Duration);
	UInstancedStaticMeshComponent* MakeIsm(const TCHAR* Name, const FLinearColor& Color, int32 Count, bool bShadow);

	FSkateFeedbackTuning Tuning;

	UPROPERTY(Transient)
	TObjectPtr<USkateIceSynth> Synth;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> TrailIsm;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> ScrapeIsm;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SprayIsm;

	TArray<FMark> TrailMarks;
	TArray<FMark> ScrapeMarks;
	int32 NextTrail = 0;
	int32 NextScrape = 0;
	FVector LastBladePos[2];
	bool bHasLastBladePos[2] = { false, false };
	float FadeTimer = 0.f;

	TArray<FChip> Chips;
	int32 NextChip = 0;
	float SprayAccumulator = 0.f;
};

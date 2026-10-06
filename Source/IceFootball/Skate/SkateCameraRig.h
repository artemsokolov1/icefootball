// Ice skating prototype - fixed-angle top-down camera.
//
// Follow mode: fixed pitch/yaw/distance. The skater position is followed without lag (no
// hidden input latency); only a small, clamped look-ahead offset (velocity * time) is smoothed.
// The camera never yaws with the skater's body. No shake.
// Static mode (diagnostics): fixed view of the whole rink, so movement can be judged without
// any camera motion.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateCameraRig.generated.h"

class UCameraComponent;

UCLASS()
class ICEFOOTBALL_API ASkateCameraRig : public AActor
{
	GENERATED_BODY()

public:
	ASkateCameraRig();

	virtual void Tick(float DeltaSeconds) override;

	void SetTarget(AActor* InTarget);
	void SetStaticFocus(const FVector& InFocus) { StaticFocus = InFocus; }
	void SetStaticMode(bool bInStatic);
	bool IsStaticMode() const { return bStaticMode; }
	void ToggleStaticMode() { SetStaticMode(!bStaticMode); }

	/** Yaw (deg) the stick is projected with. Same in both modes, so controls never change meaning. */
	float GetControlYaw() const { return Tuning.Yaw; }

	/** Snaps the look-ahead (after a reset/teleport). */
	void SnapToTarget();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FSkateCameraTuning Tuning;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

private:
	void UpdateCamera(float DeltaSeconds);

	TWeakObjectPtr<AActor> Target;
	bool bStaticMode = false;
	FVector StaticFocus = FVector::ZeroVector;
	FVector2D LookAhead = FVector2D::ZeroVector;
	FVector2D LookAheadVelocity = FVector2D::ZeroVector;
};

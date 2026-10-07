// Ice skating prototype - free football (Chaos rigid body).
//
// The ball is never attached, never teleported during play and its vertical velocity is never
// overwritten. Gameplay impulses arrive only through ApplyGameplayVelocity() (one call per
// contact, from USkateBallControlComponent). The ball ignores the Pawn channel so the skater
// capsule cannot add a second physical hit. Extra forces: rolling resistance while grounded,
// and a full stop below StopSpeed (no endless creeping).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Skate/Core/SkateBallControl.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateBall.generated.h"

class UStaticMeshComponent;
class UPhysicalMaterial;

UCLASS()
class ICEFOOTBALL_API ASkateBall : public AActor
{
	GENERATED_BODY()

public:
	ASkateBall();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Mass, damping, CCD and size. Physical material is passed separately (created once by the arena). */
	void ApplyPhysicsTuning(const FSkateBallPhysicsTuning& InTuning);
	void SetPhysicalMaterial(UPhysicalMaterial* Material);
	void SetIceZ(float InIceZ) { IceZ = InIceZ; }

	/** One gameplay contact: sets the new linear velocity and a matching rolling spin. Logged for diagnostics. */
	void ApplyGameplayVelocity(const FVector& NewVelocity, ESkateImpulseKind Kind);

	/** Possession: while carried, damping and rolling resistance are off and the velocity is steered every
	 *  frame by SetCarriedVelocity (not a gameplay impulse). The ball keeps colliding with everything. */
	void SetCarried(bool bInCarried);
	bool IsCarried() const { return bCarried; }
	void SetCarriedVelocity(const FVector& NewVelocity);

	/** Scene reset only (not used during play): teleports and stops the ball. */
	void ResetBall(const FVector& Location);

	FVector GetBallVelocity() const;
	float GetRadius() const { return Tuning.Radius; }
	float GetIceZ() const { return IceZ; }
	bool IsGrounded() const { return bGrounded; }

	// ---- Diagnostics (debug HUD) ----
	/** Two gameplay impulses closer than this are reported as a DOUBLE IMPULSE fault. */
	static constexpr float DoubleImpulseWindow = 0.04f;
	int32 GetDoubleImpulseFaults() const { return DoubleImpulseFaults; }
	float GetLastDoubleImpulseGap() const { return LastDoubleImpulseGap; }
	/** Physics contacts with a Pawn (should be impossible: the ball ignores the Pawn channel). */
	int32 GetPawnContactFaults() const { return PawnContactFaults; }
	double GetLastFaultTime() const { return LastFaultTime; }
	int32 GetGameplayImpulseCount() const { return GameplayImpulses; }

	/** Strongest wall/board impact this frame (0..1), for the bounce sound. Cleared when read. */
	float ConsumeBounceStrength();

protected:
	UFUNCTION()
	void OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<UStaticMeshComponent> StripeA;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<UStaticMeshComponent> StripeB;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
	FSkateBallPhysicsTuning Tuning;

private:
	float IceZ = 0.f;
	bool bGrounded = true;
	bool bCarried = false;

	double LastImpulseTime = -1000.0;
	int32 DoubleImpulseFaults = 0;
	float LastDoubleImpulseGap = 0.f;
	int32 PawnContactFaults = 0;
	double LastFaultTime = -1000.0;
	int32 GameplayImpulses = 0;
	float PendingBounce = 0.f;
};

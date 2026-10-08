// Ice skating prototype - free football (Chaos rigid body).
//
// The ball is never attached and its vertical velocity is never overwritten by skaters. Gameplay
// impulses arrive only through ApplyGameplayVelocity() (one call per contact, from a skater's
// USkateBallControlComponent or the goalkeeper). The only teleports during play: the keeper holding
// the ball in its hands (HoldAt) and its throw-out start point.
// The ball also records who has it (Holder: a skater carrying it, or the keeper), so a teammate never
// takes the ball off the other one, and who touched it last (a pass can be received firmer than a shot). The ball ignores the Pawn channel so the skater
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
	void ApplyGameplayVelocity(const FVector& NewVelocity, ESkateImpulseKind Kind, const UObject* Source = nullptr, int32 SourceTeam = INDEX_NONE);
	const FSkateBallPhysicsTuning& GetPhysicsTuning() const { return Tuning; }

	// ---- Who has the ball ----
	/** Claims the ball (a skater trapped it, the keeper caught it). */
	void SetHolder(const UObject* InHolder) { Holder = InHolder; }
	/** Gives the ball up, only if InHolder still has it. */
	void ClearHolder(const UObject* InHolder);
	bool IsHeldByOther(const UObject* Who) const;
	const UObject* GetHolder() const { return Holder.Get(); }

	/** Keeper: the ball sits in the hands (teleported, no velocity, no gravity) until released. */
	void HoldAt(const FVector& Location);
	void ReleaseHold(const FVector& Location, const FVector& Velocity, const UObject* Source, int32 SourceTeam);
	/** Lets go of a held ball where it is (no impulse): gravity and ice friction apply again. */
	void DropHold();

	/** Seconds since the last gameplay impulse (from anyone). */
	float GetTimeSinceGameplayImpulse() const;
	/** The ball is a pass (push / throw-out) from a teammate (or the own keeper) of Receiver: meet it, first touch waits for it. */
	bool IsPassFor(const UObject* Receiver, int32 ReceiverTeam) const;
	/** The ball is a pass (push / throw-out) that Receiver did not make itself, from either team: received firmer. */
	bool IsPassFromOther(const UObject* Receiver) const;
	/** The ball is a pass that Source played within the last 2 s (the passer leaves it to the receiver). */
	bool IsPassFrom(const UObject* Source) const;

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
	ESkateImpulseKind LastImpulseKind = ESkateImpulseKind::None;
	TWeakObjectPtr<const UObject> LastImpulseSource;
	int32 LastImpulseTeam = INDEX_NONE;
	TWeakObjectPtr<const UObject> Holder;
	bool bHeldInHands = false;
	int32 DoubleImpulseFaults = 0;
	float LastDoubleImpulseGap = 0.f;
	int32 PawnContactFaults = 0;
	double LastFaultTime = -1000.0;
	int32 GameplayImpulses = 0;
	float PendingBounce = 0.f;
};

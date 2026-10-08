// Ice skating prototype - CharacterMovement extension that drives ground velocity with FSkateModel.
//
// Why extend CharacterMovement instead of a physics body: CMC already gives robust capsule
// sweeps, floor detection, wall sliding and sub-stepping; only the velocity rule is replaced.
// Walking-mode collision response (slide along boards) stays untouched. The skater is never
// a free physics body.
//
// What is replaced / disabled:
//   * CalcVelocity (walking): FSkateModel replaces acceleration/friction/braking.
//   * PhysicsRotation: actor yaw = skate heading from FSkateModel (no OrientToMovement,
//     no controller-desired rotation; ASkateCharacter also disables bUseControllerRotationYaw).
//   * Physics interaction with simulated bodies is off: the ball is handled by
//     USkateBallControlComponent only (single source of skater->ball impulses).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Skate/Core/SkateModel.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateMovementComponent.generated.h"

UCLASS(ClassGroup = (Skate))
class ICEFOOTBALL_API USkateMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	USkateMovementComponent();

	/** Input for the next movement update (already shaped and camera-relative). */
	void SetSkateInput(const FSkateMoveInput& InInput) { PendingInput = InInput; }
	/** Deke on the next movement update: -1 left, +1 right. */
	void QueueDeke(int32 Side) { PendingDeke = Side; }
	const FSkateMoveInput& GetSkateInput() const { return PendingInput; }

	void SetMovementTuning(const FSkateMovementTuning& InTuning) { MoveTuning = InTuning; }
	const FSkateMovementTuning& GetMovementTuning() const { return MoveTuning; }

	/** Velocity, heading and telemetry of the last update. */
	const FSkateMoveState& GetSkateState() const { return SkateState; }

	/** True if braking ever flipped the velocity this frame (concrete fault, should never happen). */
	bool HadBrakeFaultThisFrame() const { return bBrakeFaultThisFrame; }

	/** Stops the skater and points the blades along Facing. */
	void ResetSkating(const FVector& Facing);

	/** Off = stock CharacterMovement walking (diagnostics only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate")
	bool bSkatingEnabled = true;

	//~ UCharacterMovementComponent
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual float GetMaxSpeed() const override;

private:
	FSkateMoveInput PendingInput;
	int32 PendingDeke = 0;
	FSkateMovementTuning MoveTuning;
	FSkateMoveState SkateState;
	bool bBrakeFaultThisFrame = false;
};

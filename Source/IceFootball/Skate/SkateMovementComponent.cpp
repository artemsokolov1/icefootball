#include "Skate/SkateMovementComponent.h"

USkateMovementComponent::USkateMovementComponent()
{
	// Rotation is owned by the skate heading.
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;

	// The ball must not be pushed by capsule sweeps (no second, physics-driven impulse).
	bEnablePhysicsInteraction = false;

	// The teammate has no controller of its own (the player controller drives whichever skater is
	// active and feeds the other one AI input), so movement must run without a controller too.
	bRunPhysicsWithNoController = true;

	// Small CMC sub-steps keep collision response consistent at 30 FPS (note: below the Details-panel
	// ClampMin of 0.0166, which only applies to editor edits); the skate model
	// sub-steps further internally (FSkateMovementTuning::MaxSubstep).
	MaxSimulationTimeStep = 1.f / 120.f;
	MaxSimulationIterations = 8;

	// Stock walking values are unused while skating, but keep sane fallbacks.
	MaxWalkSpeed = 600.f;
	GroundFriction = 8.f;
	BrakingDecelerationWalking = 2048.f;
	MaxAcceleration = 2048.f;
	bCanWalkOffLedges = true;
	NavAgentProps.bCanJump = false;
	NavAgentProps.bCanCrouch = false;
}

void USkateMovementComponent::ResetSkating(const FVector& Facing)
{
	FSkateModel::Reset(SkateState, FSkateVec2(static_cast<float>(Facing.X), static_cast<float>(Facing.Y)));
	PendingInput = FSkateMoveInput();
	Velocity = FVector::ZeroVector;
	bBrakeFaultThisFrame = false;
}

void USkateMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	bBrakeFaultThisFrame = false;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void USkateMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!bSkatingEnabled || !IsMovingOnGround() || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	// CMC owns the actual velocity (it is corrected by wall slides); the model reads and writes it.
	SkateState.Velocity = FSkateVec2(static_cast<float>(Velocity.X), static_cast<float>(Velocity.Y));
	FSkateModel::Step(MoveTuning, PendingInput, DeltaTime, SkateState);
	bBrakeFaultThisFrame |= SkateState.bBrakeReversalFault;

	Velocity.X = SkateState.Velocity.X;
	Velocity.Y = SkateState.Velocity.Y;
}

void USkateMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!bSkatingEnabled || !UpdatedComponent)
	{
		Super::PhysicsRotation(DeltaTime);
		return;
	}
	const FRotator Current = UpdatedComponent->GetComponentRotation();
	const FRotator Desired(0.f, FMath::RadiansToDegrees(SkateState.Heading.Yaw()), 0.f);
	if (!Current.Equals(Desired, 0.01f))
	{
		MoveUpdatedComponent(FVector::ZeroVector, Desired, /*bSweep*/ false);
	}
}

float USkateMovementComponent::GetMaxSpeed() const
{
	if (bSkatingEnabled && IsMovingOnGround())
	{
		return MoveTuning.BoostMaxSpeed;
	}
	return Super::GetMaxSpeed();
}

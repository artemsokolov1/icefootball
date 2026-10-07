// Ice skating prototype - skater side of ball interaction.
//
// Each frame (after the skater moved): build a contact query (positions, velocities, a wall
// trace), ask FSkateBallControl for at most one impulse, and apply it once to the free ball
// as a velocity change. Physics between the skater capsule and the ball is disabled
// (ASkateBall ignores the Pawn channel), so this is the only skater->ball impulse source.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Skate/Core/SkateBallControl.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateBallControlComponent.generated.h"

class ASkateBall;
class ASkateCharacter;

UCLASS(ClassGroup = (Skate))
class ICEFOOTBALL_API USkateBallControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkateBallControlComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetTuning(const FSkateTuning& InTuning);
	void SetInteractionEnabled(bool bEnabled) { bInteractionEnabled = bEnabled; }
	bool IsInteractionEnabled() const { return bInteractionEnabled; }

	/** Button edges from the controller; consumed on the next tick. */
	void QueueActions(bool bPush, bool bKickPress, bool bKickRelease);

	void ResetControl();

	ASkateBall* GetBall() const { return Ball.Get(); }
	const FSkateContactReport& GetReport() const { return Report; }
	const FSkateBallControlState& GetControlState() const { return ControlState; }
	float GetKickCharge() const;
	bool IsChargingKick() const { return ControlState.bCharging; }
	bool HasBall() const { return ControlState.Possession.bPossessed; }

	/** Last applied impulse (for HUD, pose and feedback). */
	const FSkateBallImpulse& GetLastImpulse() const { return LastImpulse; }
	float GetTimeSinceLastImpulse() const { return ControlState.TimeSinceImpulse; }
	/** Time since a push/kick command was executed or whiffed, for the leg swing pose. */
	float GetTimeSinceActionSwing() const { return TimeSinceSwing; }
	ESkateImpulseKind GetLastSwingKind() const { return LastSwingKind; }
	float GetLastSwingPower() const { return LastSwingPower; }

private:
	ASkateBall* FindBall();
	bool HasLineOfSight(const ASkateCharacter& Skater, const ASkateBall& InBall) const;
	void DrawDebug(const ASkateCharacter& Skater, const FSkateContactQuery& Query) const;

	TWeakObjectPtr<ASkateBall> Ball;
	FSkateBallControlTuning ControlTuning;
	FSkateBallPhysicsTuning BallPhysicsTuning;
	float SkaterMaxSpeed = 580.f;
	bool bInteractionEnabled = true;

	FSkateBallActionInput PendingActions;
	FSkateBallControlState ControlState;
	FSkateContactReport Report;
	FSkateBallImpulse LastImpulse;

	float TimeSinceSwing = 100.f;
	ESkateImpulseKind LastSwingKind = ESkateImpulseKind::None;
	float LastSwingPower = 0.f;
};

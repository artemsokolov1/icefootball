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
	void QueueActions(bool bPushPress, bool bPushRelease, bool bKickPress, bool bKickRelease, bool bThroughPress = false);
	/** Through pass target: ThroughLead cm ahead of the nearest teammate towards our goal, kept inside the rink. */
	void SetThroughTarget(bool bValid, const FVector2D& Target) { bThroughTargetValid = bValid; ThroughTarget = Target; }

	void ResetControl();
	/** The ball was taken away: drop it now (the usual re-trap cooldown after a loss applies). */
	void KnockLoose();
	/** The take button: an opponent's ball within Range (cm), held at least Protect (s), is knocked to our feet. */
	bool TryTake(float Range, float Protect, float BallSpeed);

	/** Drops a pass / shot wind-up and pending buttons (control switched to the other skater). */
	void CancelActions();

	/** Ball possessions so far (increments on every trap) - used to switch control to a skater that just got the ball. */
	int32 GetAcquireCount() const { return ControlState.Possession.AcquireCount; }
	/** Gameplay impulses so far (pass / shot / touch / block). */
	int32 GetImpulseCount() const { return ControlState.ImpulseCount; }

	ASkateBall* GetBall() const { return Ball.Get(); }
	const FSkateContactReport& GetReport() const { return Report; }
	const FSkateBallControlState& GetControlState() const { return ControlState; }
	float GetKickCharge() const;
	bool IsChargingKick() const { return ControlState.bCharging; }
	float GetPassCharge() const;
	bool IsChargingPass() const { return ControlState.bChargingPass; }
	bool HasBall() const { return ControlState.Possession.bPossessed; }

	/** Last applied impulse (for HUD, pose and feedback). */
	const FSkateBallImpulse& GetLastImpulse() const { return LastImpulse; }
	float GetTimeSinceLastImpulse() const { return ControlState.TimeSinceImpulse; }
	/** Time since a push/kick command was executed or whiffed, for the leg swing pose. */
	float GetTimeSinceActionSwing() const { return TimeSinceSwing; }
	ESkateImpulseKind GetLastSwingKind() const { return LastSwingKind; }
	/** Leg of the latest tap / pass / shot and the leg that will play the next one (0 = left, 1 = right). */
	int32 GetLastSwingFoot() const { return LastSwingFoot; }
	int32 GetPlannedFoot() const { return ControlState.PlannedFoot; }
	float GetLastSwingPower() const { return LastSwingPower; }

private:
	ASkateBall* FindBall();
	bool HasLineOfSight(const ASkateCharacter& Skater, const ASkateBall& InBall) const;
	/** Board faces around the skater (horizontal traces at ball height, arena boards only). */
	void FindBoards(const ASkateCharacter& Skater, const ASkateBall& InBall, FSkateContactQuery& Query) const;
	void DrawDebug(const ASkateCharacter& Skater, const FSkateContactQuery& Query) const;

	TWeakObjectPtr<ASkateBall> Ball;
	FSkateBallControlTuning ControlTuning;
	FSkateBallPhysicsTuning BallPhysicsTuning;
	float SkaterMaxSpeed = 580.f;
	bool bInteractionEnabled = true;

	FSkateBallActionInput PendingActions;
	bool bThroughTargetValid = false;
	FVector2D ThroughTarget = FVector2D::ZeroVector;
	FSkateBallControlState ControlState;
	FSkateContactReport Report;
	FSkateBallImpulse LastImpulse;

	float TimeSinceSwing = 100.f;
	ESkateImpulseKind LastSwingKind = ESkateImpulseKind::None;
	int32 LastSwingFoot = SkateFoot::Right;
	float LastSwingPower = 0.f;
};

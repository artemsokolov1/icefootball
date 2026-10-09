// Ice skating prototype - the goalkeeper (AI, not controllable).
//
// All decisions are made by the engine-independent FSkateKeeper (Core/SkateKeeper): positioning,
// reaction, dive, catch / parry, throw-out. This actor feeds it the ball and the goal, applies its
// output to the ball (one gameplay impulse per save, like the skaters) and poses a placeholder body
// made of basic shapes. The body has no collision: the ball meets the keeper only through FSkateKeeper.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Skate/Core/SkateKeeper.h"
#include "Skate/Core/SkateTuning.h"
#include "SkateGoalkeeper.generated.h"

class ASkateArena;
class ASkateBall;
class ASkateCharacter;
class UStaticMeshComponent;

UCLASS()
class ICEFOOTBALL_API ASkateGoalkeeper : public AActor
{
	GENERATED_BODY()

public:
	ASkateGoalkeeper();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Which goal (0 = +X, 1 = -X) this keeper guards and which team it plays for (throw-outs go to that team). */
	void SetGoal(ASkateArena* InArena, int32 InGoalIndex, int32 InTeam);
	int32 GetTeam() const { return Team; }
	int32 GetGoalIndex() const { return GoalIndex; }
	/** Back to the middle of the goal, ball released (scene reset). */
	void ResetKeeper();

	/** The player's keeper: while it holds a caught ball the player drives it (SetPlayerMove, RequestThrow / RequestClear)
	 *  and it throws by itself only after PlayerHoldTime. */
	void SetPlayerControlled(bool bIn) { bPlayerControlled = bIn; }
	bool IsHoldingBall() const { return State.bHolding; }
	/** The stick: world direction and 0..1; the keeper walks with the ball (out to 5 m, across to the posts). */
	void SetPlayerMove(const FVector2D& WorldDir, float Magnitude) { PlayerMove = WorldDir * Magnitude; }
	/** Throw the held ball now, to the teammate the aim points at (zero aim = the nearest). */
	void RequestThrow(const FVector2D& Aim) { bThrowRequested = true; bClear = false; ThrowAim = Aim; }
	/** Clear the held ball long along the aim (zero aim = straight out). */
	void RequestClear(const FVector2D& Aim) { bThrowRequested = true; bClear = true; ThrowAim = Aim; }

	const FSkateKeeperState& GetKeeperState() const { return State; }
	int32 GetSaves() const { return State.Saves; }
	ESkateKeeperAction GetLastAction() const { return State.LastAction; }
	float GetTimeSinceAction() const { return State.TimeSinceAction; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keeper")
	FSkateKeeperTuning Tuning;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Keeper")
	TObjectPtr<USceneComponent> Root;

	/** Hips: rolled sideways and lowered for a dive; the whole body hangs from it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Keeper")
	TObjectPtr<USceneComponent> BodyPivot;

private:
	void BuildBody();
	void ApplyPose(const FSkateKeeperPose& Pose, float DeltaSeconds);
	FSkateGoalFrame GoalFrame() const;
	FVector2D ThrowTarget() const;
	const ASkateCharacter* ThrowMate() const;

	TWeakObjectPtr<ASkateArena> Arena;
	int32 GoalIndex = 0;
	int32 Team = 1;
	FSkateKeeperState State;
	bool bPlayerControlled = false;
	bool bThrowRequested = false;
	bool bClear = false;
	FVector2D ThrowAim = FVector2D::ZeroVector;
	FVector2D PlayerMove = FVector2D::ZeroVector;
	float Butterfly = 0.f;
	bool bBuilt = false;
	FVector HandLocal[2];

	UStaticMeshComponent* Pelvis = nullptr;
	UStaticMeshComponent* Torso = nullptr;
	UStaticMeshComponent* Head = nullptr;
	UStaticMeshComponent* Cap = nullptr;
	UStaticMeshComponent* Arm[2] = { nullptr, nullptr };
	UStaticMeshComponent* Glove[2] = { nullptr, nullptr };
	UStaticMeshComponent* Leg[2] = { nullptr, nullptr };
	UStaticMeshComponent* Skate[2] = { nullptr, nullptr };
};

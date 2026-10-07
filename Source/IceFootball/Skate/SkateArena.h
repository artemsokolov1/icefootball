// Ice skating prototype - the hockey rink and the match, built at runtime from engine basic shapes.
//
// Contents: ice sheet, boards, hockey markings (centre / blue / goal lines, face-off circles, the
// trapezoid behind each net), two goals set in from the end boards, the ball, two AI keepers, the
// skaters of both teams, optional lighting. The arena owns the spawn points, the face-off reset and
// the match (score, clock). Team 0 (the player's, red) attacks the +X goal, team 1 (AI, blue) the -X
// goal. Spawned by ASkateGameMode if the level has none, or place it in a level manually.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Skate/Core/SkateKeeper.h"
#include "SkateArena.generated.h"

class ASkateBall;
class ASkateCharacter;
class ASkateGoalkeeper;
class UPhysicalMaterial;
class UStaticMeshComponent;

/** Rink layout (cm). Hockey-sized: 60 x 30 m. */
USTRUCT(BlueprintType)
struct FSkateArenaLayout
{
	GENERATED_BODY()

	/** Inner rink size: X = length (the goals are at +-X), Y = width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D RinkSize = FVector2D(6000.f, 3000.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float BoardHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float BoardThickness = 30.f;

	/** Corner radius (cm): hockey rinks have rounded corners (IIHF 8.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float CornerRadius = 850.f;

	/** Face-off: slot 0 of each team stands this far from the centre (X mirrored per team, team 0 at -X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D CentreSpawn = FVector2D(150.f, 0.f);

	/** Slot 1 of each team: X back from the centre, Y to the side (both mirrored per team). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D WingSpawn = FVector2D(900.f, 800.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D BallSpawn = FVector2D(0.f, 0.f);

	/** Spawns the teammate and the two opponents (the player's own skater comes from the game mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bSpawnTeams = true;

	/** AI goalkeeper in each goal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bSpawnGoalkeepers = true;

	/** Goal line this far (cm) in front of the end boards: room to skate behind the net, as in hockey. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalLineInset = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalWidth = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalHeight = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalDepth = 120.f;

	/** Blue lines this far (cm) from the centre line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float BlueLineX = 800.f;

	/** Match length (s) and the pause after a goal before the face-off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	float MatchLength = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	float GoalPause = 2.5f;

	/** Also paints the skating test course (acceleration lane, stop zone, slalom cones, figure eight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	bool bTrainingCourse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float AccelLaneY = -1100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float AccelStartX = -2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float StopZoneCenterX = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float StopZoneLength = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	FVector2D TurnCircleCenter = FVector2D(-1250.f, 900.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float TurnCircleRadius = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float SlalomY = -250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float SlalomStartX = -2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float SlalomSpacing = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	int32 SlalomCones = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	FVector2D FigureEightCenter = FVector2D(1300.f, -900.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float FigureEightRadius = 400.f;
};

UCLASS()
class ICEFOOTBALL_API ASkateArena : public AActor
{
	GENERATED_BODY()

public:
	ASkateArena();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static ASkateArena* Find(const UWorld* World);

	/** Component tag of the four boards (the carried ball is kept off them; posts are obstacles). */
	static FName BoardTag() { return FName(TEXT("SkateBoard")); }

	/** Team 0 slot 0: the player's own skater. */
	FTransform GetPlayerSpawnTransform() const { return GetSpawnTransform(0, 0); }
	/** Face-off spot of a skater: team 0 / 1, slot 0 (centre) / 1 (wing). */
	FTransform GetSpawnTransform(int32 Team, int32 Slot) const;
	/** The goal mouth in world space. Goal 0 is at +X (attacked by team 0), goal 1 at -X. */
	FSkateGoalFrame GetGoalFrame(int32 GoalIndex = 0) const;
	ASkateGoalkeeper* GetGoalkeeper(int32 GoalIndex = 0) const { return GoalIndex == 0 ? Goalkeeper0 : Goalkeeper1; }
	FVector GetBallSpawnLocation() const;
	FVector GetRinkCenter() const { return GetActorLocation(); }
	const FSkateArenaLayout& GetLayout() const { return Layout; }
	ASkateBall* GetBall() const { return Ball; }

	/** Face-off: every skater, both keepers and the ball back to their start points, all stopped. */
	void ResetScene();
	/** Score 0:0, full clock, face-off. */
	void RestartMatch();

	/** Test helper: places a resting ball just in front of the skater's feet. */
	void PlaceBallInFront(ASkateCharacter* Skater);

	/** Point inside the rink (with a small margin), used for "out of bounds / tunnelled" faults. */
	bool IsInsideRink(const FVector& WorldLocation, float Margin = 5.f) const;

	/** Lifts the rink so the ice sits on top of whatever ground the level already has (e.g. a landscape).
	 *  Returns true if the rink moved. Skaters and ball are reset to the new start points. */
	bool SettleOnGround();

	// ---- Match ----
	int32 GetScore(int32 Team) const { return Team == 0 ? Score0 : Score1; }
	int32 GetGoals() const { return Score0 + Score1; }
	double GetLastGoalTime() const { return LastGoalTime; }
	int32 GetLastGoalTeam() const { return LastGoalTeam; }
	/** Seconds left on the clock. */
	float GetClock() const { return Clock; }
	bool IsMatchOver() const { return bMatchOver; }
	/** The pause after a goal: the AI skaters stand still until the face-off. */
	bool IsGoalPause() const { return GoalPauseLeft >= 0.f; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	FSkateArenaLayout Layout;

	/** Adds sun, sky light and atmosphere when the level has no directional light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	bool bSpawnLightingIfMissing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	TSubclassOf<ASkateBall> BallClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<ASkateBall> Ball;

	UPROPERTY(Transient)
	TObjectPtr<ASkateGoalkeeper> Goalkeeper0;

	UPROPERTY(Transient)
	TObjectPtr<ASkateGoalkeeper> Goalkeeper1;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> IceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> BoardMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> NetMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> BallMaterial;

private:
	void BuildMaterials();
	void BuildRink();
	/** One straight piece of board (with its cap) centred at Center (ice level), Length along Yaw. */
	void AddBoard(const FVector2D& Center, float Length, float Yaw);
	void BuildMarkings();
	void BuildTrainingCourse();
	void BuildGoal(float Sign);
	void BuildLighting();
	void SpawnBall();
	void SpawnTeams();
	void SpawnGoalkeepers();
	/** The goal (0 / 1) the ball is in, or INDEX_NONE. */
	int32 BallInGoal() const;

	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& Size, const FLinearColor& Color, UPhysicalMaterial* PhysMat, float Yaw = 0.f);
	UStaticMeshComponent* AddMarkLine(const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Color);
	void AddMarkRing(const FVector2D& Center, float Radius, float Width, const FLinearColor& Color, int32 Segments = 48);
	void AddMarkRect(const FVector2D& Center, const FVector2D& Size, const FLinearColor& Color);
	void AddCone(const FVector2D& Location);
	void AddLabel(const FVector2D& Location, const FString& Text, const FColor& Color, float Size = 70.f);

	bool FindGroundTop(float& OutTopZ) const;

	int32 Score0 = 0;
	int32 Score1 = 0;
	int32 LastGoalTeam = INDEX_NONE;
	double LastGoalTime = -1000.0;
	float Clock = 0.f;
	/** Seconds left before the face-off after a goal (< 0: play on). */
	float GoalPauseLeft = -1.f;
	bool bMatchOver = false;
	bool bBallInGoal = false;
	float SettleTimer = 0.f;
	float SettleCheckAccumulator = 0.f;
};

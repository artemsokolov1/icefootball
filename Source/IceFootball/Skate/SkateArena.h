// Ice skating prototype - the test rink, built at runtime from engine basic shapes.
//
// Contents: ice sheet, boards, painted markings (acceleration straight, stop zone, turning
// circle, slalom cones, figure eight), a target goal, the ball, optional lighting.
// The arena owns spawn points and the scene reset. Spawned by ASkateGameMode if the level
// has none, or place it in a level manually.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkateArena.generated.h"

class ASkateBall;
class ASkateCharacter;
class UPhysicalMaterial;
class UStaticMeshComponent;

/** Rink layout (cm). Sized for ~580 cm/s skating: 50 x 32 m, stop distance ~1.6 m, glide ~10 m. */
USTRUCT(BlueprintType)
struct FSkateArenaLayout
{
	GENERATED_BODY()

	/** Inner rink size: X = length (screen up with the default camera), Y = width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D RinkSize = FVector2D(5000.f, 3200.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float BoardHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float BoardThickness = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D PlayerSpawn = FVector2D(-2150.f, -1100.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float PlayerSpawnYaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D BallSpawn = FVector2D(600.f, 600.f);

	/** Acceleration straight along +X at this Y, from AccelStartX to the stop zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float AccelLaneY = -1100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float AccelStartX = -2000.f;

	/** Stop zone (box) centre X along the acceleration lane and its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float StopZoneCenterX = -50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float StopZoneLength = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D TurnCircleCenter = FVector2D(-1250.f, 900.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float TurnCircleRadius = 450.f;

	/** Slalom: cones along +X at this Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float SlalomY = -250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float SlalomStartX = -2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float SlalomSpacing = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	int32 SlalomCones = 7;

	/** Figure eight: two circles touching at this centre, laid out along X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D FigureEightCenter = FVector2D(1300.f, -900.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float FigureEightRadius = 400.f;

	/** Target goal on the +X board: mouth centre Y, width, height, depth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalCenterY = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalWidth = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalHeight = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	float GoalDepth = 120.f;
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

	FTransform GetPlayerSpawnTransform() const;
	FVector GetBallSpawnLocation() const;
	FVector GetRinkCenter() const { return GetActorLocation(); }
	const FSkateArenaLayout& GetLayout() const { return Layout; }
	ASkateBall* GetBall() const { return Ball; }

	/** Puts skater and ball back to their start points, both stopped. */
	void ResetScene(ASkateCharacter* Skater);

	/** Test helper: places a resting ball just in front of the skater's feet. */
	void PlaceBallInFront(ASkateCharacter* Skater);

	/** Point inside the rink (with a small margin), used for "out of bounds / tunnelled" faults. */
	bool IsInsideRink(const FVector& WorldLocation, float Margin = 5.f) const;

	/** Lifts the rink so the ice sits on top of whatever ground the level already has (e.g. a landscape).
	 *  Returns true if the rink moved. Skater and ball are reset to the new start points. */
	bool SettleOnGround();

	int32 GetGoals() const { return Goals; }
	double GetLastGoalTime() const { return LastGoalTime; }

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
	void BuildMarkings();
	void BuildGoal();
	void BuildLighting();
	void SpawnBall();

	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& Size, const FLinearColor& Color, UPhysicalMaterial* PhysMat, float Yaw = 0.f);
	UStaticMeshComponent* AddMarkLine(const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Color);
	void AddMarkRing(const FVector2D& Center, float Radius, float Width, const FLinearColor& Color, int32 Segments = 48);
	void AddMarkRect(const FVector2D& Center, const FVector2D& Size, const FLinearColor& Color);
	void AddCone(const FVector2D& Location);
	void AddLabel(const FVector2D& Location, const FString& Text, const FColor& Color, float Size = 70.f);

	bool FindGroundTop(float& OutTopZ) const;

	int32 Goals = 0;
	double LastGoalTime = -1000.0;
	bool bBallInGoal = false;
	float SettleTimer = 0.f;
	float SettleCheckAccumulator = 0.f;
};

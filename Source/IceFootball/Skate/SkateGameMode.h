// Ice skating prototype - isolated game mode for the skating test rink.
// Use it per map (World Settings > GameMode Override) or per URL (?game=skate). It does not
// replace the project's default game mode.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SkateGameMode.generated.h"

class ASkateArena;

UCLASS()
class ICEFOOTBALL_API ASkateGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASkateGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	ASkateArena* GetArena();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skate")
	TSubclassOf<ASkateArena> ArenaClass;

	UPROPERTY(Transient)
	TObjectPtr<ASkateArena> Arena;
};

#include "Skate/SkateGameMode.h"

#include "Engine/World.h"
#include "IceFootball.h"
#include "Skate/SkateArena.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateDebugHUD.h"
#include "Skate/SkatePlayerController.h"

ASkateGameMode::ASkateGameMode()
{
	DefaultPawnClass = ASkateCharacter::StaticClass();
	PlayerControllerClass = ASkatePlayerController::StaticClass();
	HUDClass = ASkateDebugHUD::StaticClass();
	ArenaClass = ASkateArena::StaticClass();
}

void ASkateGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	GetArena();
}

ASkateArena* ASkateGameMode::GetArena()
{
	if (!Arena)
	{
		Arena = ASkateArena::Find(GetWorld());
	}
	if (!Arena && ArenaClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Arena = GetWorld()->SpawnActor<ASkateArena>(ArenaClass, FTransform::Identity, Params);
		UE_LOG(LogIceSkate, Log, TEXT("SkateGameMode: spawned the test rink"));
	}
	return Arena;
}

APawn* ASkateGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	if (ASkateArena* Rink = GetArena())
	{
		return SpawnDefaultPawnAtTransform(NewPlayer, Rink->GetPlayerSpawnTransform());
	}
	return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
}

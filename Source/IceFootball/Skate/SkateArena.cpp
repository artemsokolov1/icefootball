#include "Skate/SkateArena.h"

#include "Kismet/GameplayStatics.h"
#include "Skate/SkateBallControlComponent.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IceFootball.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Skate/SkateBall.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateGoalkeeper.h"
#include "Skate/SkateVisuals.h"

namespace SkateArenaDetail
{
	const FLinearColor IceColor(0.70f, 0.80f, 0.88f);
	const FLinearColor BoardColor(0.92f, 0.92f, 0.95f);
	const FLinearColor BoardCapColor(0.85f, 0.25f, 0.05f);
	const FLinearColor RedLine(0.75f, 0.05f, 0.05f);
	const FLinearColor BlueLine(0.05f, 0.15f, 0.65f);
	const FLinearColor LaneColor(0.15f, 0.35f, 0.55f);
	const FLinearColor StopZoneColor(0.95f, 0.75f, 0.05f);
	const FLinearColor ConeColor(1.0f, 0.35f, 0.0f);
	const FLinearColor GoalColor(0.95f, 0.95f, 0.95f);
	const FLinearColor NetColor(0.35f, 0.35f, 0.4f);

	constexpr float IceThickness = 50.f;
	constexpr float MarkThickness = 0.6f;
	constexpr float LineWidth = 10.f;
	constexpr float ConeHeight = 45.f;
	constexpr float ConeBase = 30.f;
	constexpr float PostRadius = 7.f;
	constexpr float NetThickness = 4.f;
	// Trapezoid behind the net (hockey): half width at the goal line and at the end boards.
	constexpr float TrapezoidNearHalf = 335.f;
	constexpr float TrapezoidFarHalf = 425.f;
	constexpr float FaceOffRadius = 450.f;
}

ASkateArena::ASkateArena()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BallClass = ASkateBall::StaticClass();
}

const TCHAR* SkateDifficultyName(ESkateDifficulty Difficulty)
{
	return Difficulty == ESkateDifficulty::Easy ? TEXT("Easy") : TEXT("Normal");
}

void ASkateArena::CycleDifficulty(int32 Direction)
{
	Difficulty = Difficulty == ESkateDifficulty::Easy ? ESkateDifficulty::Normal : ESkateDifficulty::Easy;
	UE_LOG(LogIceSkate, Log, TEXT("Difficulty: %s"), SkateDifficultyName(Difficulty));
}

FSkateAITuning ASkateArena::GetAITuning(const FSkateAITuning& Base) const
{
	FSkateAITuning AI = Base;
	if (Difficulty == ESkateDifficulty::Easy)
	{
		AI.BoostAmount = FMath::Min(AI.BoostAmount, 0.4f);
		AI.ShotCharge = FMath::Min(AI.ShotCharge, 0.4f);
		AI.AimError = FMath::Max(AI.AimError, 150.f);
		AI.FaceOffReaction = FMath::Max(AI.FaceOffReaction, 0.5f);
		AI.DekeRange = 0.f;
	}
	// The series: every match the player has won makes the bots a little quicker and more accurate.
	AI.BoostAmount = FMath::Min(1.f, AI.BoostAmount + 0.08f * Wins0);
	AI.AimError *= FMath::Max(0.5f, 1.f - 0.15f * Wins0);
	return AI;
}

FSkateKeeperTuning ASkateArena::GetKeeperTuning(const FSkateKeeperTuning& Base, int32 KeeperTeam) const
{
	FSkateKeeperTuning KT = Base;
	if (Difficulty == ESkateDifficulty::Easy && KeeperTeam != 0)
	{
		// The keeper the child shoots at: a beat slower, a shorter dive. The child's own keeper stays as it is.
		KT.ReactionTime = FMath::Max(KT.ReactionTime, 0.28f);
		KT.DiveReach = FMath::Min(KT.DiveReach, 120.f);
	}
	if (KeeperTeam != 0)
	{
		KT.ReactionTime = FMath::Max(0.1f, KT.ReactionTime - 0.03f * Wins0);
	}
	return KT;
}

ASkateGoalkeeper* ASkateArena::GetGoalkeeper(int32 GoalIndex) const
{
	for (ASkateGoalkeeper* Keeper : { Goalkeeper0.Get(), Goalkeeper1.Get() })
	{
		if (Keeper && Keeper->GetGoalIndex() == GoalIndex)
		{
			return Keeper;
		}
	}
	return nullptr;
}

ASkateArena* ASkateArena::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASkateArena> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool ASkateArena::FindGroundTop(float& OutTopZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SkateArenaGround), true, this);
	if (Ball)
	{
		Params.AddIgnoredActor(Ball.Get());
	}
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

	// Sample the rink footprint (boards included) on a 7 x 5 grid from far above.
	const FVector Origin = GetActorLocation();
	const float HalfL = Layout.RinkSize.X * 0.5f + Layout.BoardThickness;
	const float HalfW = Layout.RinkSize.Y * 0.5f + Layout.BoardThickness;
	TArray<float> Heights;
	for (int32 Ix = -3; Ix <= 3; ++Ix)
	{
		for (int32 Iy = -2; Iy <= 2; ++Iy)
		{
			const FVector P(Origin.X + HalfL * Ix / 3.f, Origin.Y + HalfW * Iy / 2.f, 0.0);
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, Origin.Z + 50000.0), FVector(P.X, P.Y, Origin.Z - 50000.0), Objects, Params))
			{
				Heights.Add(static_cast<float>(Hit.ImpactPoint.Z));
			}
		}
	}
	if (Heights.Num() == 0)
	{
		return false;
	}
	// Highest ground sample, ignoring isolated tall props (trees, walls) more than 1.5 m above the median.
	Heights.Sort();
	const float Median = Heights[Heights.Num() / 2];
	OutTopZ = Median;
	for (const float Height : Heights)
	{
		if (Height <= Median + 150.f)
		{
			OutTopZ = FMath::Max(OutTopZ, Height);
		}
	}
	return true;
}

bool ASkateArena::SettleOnGround()
{
	float GroundTop = 0.f;
	if (!FindGroundTop(GroundTop))
	{
		return false;
	}
	const FVector Location = GetActorLocation();
	const float Target = GroundTop + 1.f;
	if (FMath::Abs(Target - static_cast<float>(Location.Z)) < 2.f)
	{
		return false;
	}
	SetActorLocation(FVector(Location.X, Location.Y, Target));
	UE_LOG(LogIceSkate, Log, TEXT("ASkateArena: rink placed on existing ground, ice at z = %.0f"), Target);
	if (Ball)
	{
		Ball->SetIceZ(Target);
	}
	ResetScene();
	return true;
}

void ASkateArena::BeginPlay()
{
	Super::BeginPlay();
	// -Normal on the command line: start on Normal (bots-vs-bots log checks of the hits and dekes).
	if (FParse::Param(FCommandLine::Get(), TEXT("Normal")))
	{
		Difficulty = ESkateDifficulty::Normal;
	}
	// On a map that already has ground at the origin (e.g. the default open-world template), the rink is
	// put on top of it instead of intersecting it. On an empty map nothing is hit and the ice stays at z = 0.
	float GroundTop = 0.f;
	if (FindGroundTop(GroundTop))
	{
		const FVector Location = GetActorLocation();
		SetActorLocation(FVector(Location.X, Location.Y, GroundTop + 1.f));
	}
	BuildMaterials();
	BuildRink();
	BuildMarkings();
	if (Layout.bTrainingCourse)
	{
		BuildTrainingCourse();
	}
	BuildGoal(1.f);
	BuildGoal(-1.f);
	if (bSpawnLightingIfMissing)
	{
		BuildLighting();
	}
	SpawnBall();
	SpawnGoalkeepers();
	SpawnTeams();
	// The player's skater may have been spawned before the rink moved: put everyone on the ice.
	RestartMatch();
}

FTransform ASkateArena::GetSpawnTransform(int32 Team, int32 Slot) const
{
	// The team attacking +X lines up at -X facing +X, the other one mirrored. Capsule half height 92 + 2 cm clearance.
	const float Sign = GetAttackGoal(Team) == 0 ? -1.f : 1.f;
	const FVector2D Spot = Slot == 0 ? Layout.CentreSpawn : FVector2D(Layout.WingSpawn.X, Slot == 1 ? Layout.WingSpawn.Y : -Layout.WingSpawn.Y);
	const FVector Local(Sign * Spot.X, Sign * Spot.Y, 94.f);
	return FTransform(FRotator(0.f, Sign < 0.f ? 0.f : 180.f, 0.f), GetActorTransform().TransformPosition(Local));
}

FSkateGoalFrame ASkateArena::GetGoalFrame(int32 GoalIndex) const
{
	using namespace SkateArenaDetail;
	const FTransform& Xf = GetActorTransform();
	const float Sign = GoalIndex == 0 ? 1.f : -1.f;
	const float LineX = Sign * (Layout.RinkSize.X * 0.5f - Layout.GoalLineInset);
	const FVector Center = Xf.TransformPosition(FVector(LineX, 0.f, 0.f));
	const FVector Normal = Xf.TransformVectorNoScale(FVector(-Sign, 0.f, 0.f)).GetSafeNormal2D();
	FSkateGoalFrame Frame;
	Frame.Center = FSkateVec2(static_cast<float>(Center.X), static_cast<float>(Center.Y));
	Frame.Normal = FSkateVec2(static_cast<float>(Normal.X), static_cast<float>(Normal.Y));
	Frame.HalfWidth = Layout.GoalWidth * 0.5f - PostRadius;
	Frame.Height = Layout.GoalHeight - PostRadius;
	Frame.IceZ = static_cast<float>(Center.Z);
	return Frame;
}

void ASkateArena::SpawnTeams()
{
	UWorld* World = GetWorld();
	if (!World || !Layout.bSpawnTeams)
	{
		return;
	}
	// Team 0 slot 0 is the player's pawn (game mode); everyone else is spawned here unless placed in the level.
	TArray<TPair<int32, int32>> Wanted;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		for (int32 Slot = Team == 0 ? 1 : 0; Slot < FMath::Clamp(Layout.SkatersPerTeam, 1, 3); ++Slot)
		{
			Wanted.Emplace(Team, Slot);
		}
	}
	for (const TPair<int32, int32>& Pair : Wanted)
	{
		const int32 Who[2] = { Pair.Key, Pair.Value };
		bool bExists = false;
		for (TActorIterator<ASkateCharacter> It(World); It; ++It)
		{
			bExists |= It->GetTeam() == Who[0] && It->GetTeamSlot() == Who[1];
		}
		if (bExists)
		{
			continue;
		}
		const FTransform Spawn = GetSpawnTransform(Who[0], Who[1]);
		ASkateCharacter* Skater = World->SpawnActorDeferred<ASkateCharacter>(ASkateCharacter::StaticClass(), Spawn, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Skater)
		{
			Skater->SetTeam(Who[0]);
			Skater->SetTeamSlot(Who[1]);
			Skater->FinishSpawning(Spawn);
		}
	}
}

void ASkateArena::SpawnGoalkeepers()
{
	UWorld* World = GetWorld();
	if (!World || !Layout.bSpawnGoalkeepers)
	{
		return;
	}
	for (int32 GoalIndex = 0; GoalIndex < 2; ++GoalIndex)
	{
		TObjectPtr<ASkateGoalkeeper>& Slot = GoalIndex == 0 ? Goalkeeper0 : Goalkeeper1;
		if (Slot)
		{
			continue;
		}
		const FTransform Spawn(GetActorLocation());
		Slot = World->SpawnActorDeferred<ASkateGoalkeeper>(ASkateGoalkeeper::StaticClass(), Spawn, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Slot)
		{
			// Goal 0 (+X) is attacked by team 0 in the first period, so its keeper plays for team 1.
			Slot->SetGoal(this, GoalIndex, GoalIndex == 0 ? 1 : 0);
			Slot->FinishSpawning(Spawn);
		}
	}
}

FVector ASkateArena::GetBallSpawnLocation() const
{
	const float Radius = Ball ? Ball->GetRadius() : 11.f;
	return GetActorTransform().TransformPosition(FVector(Layout.BallSpawn.X, Layout.BallSpawn.Y, Radius + 0.5f));
}

bool ASkateArena::IsInsideRink(const FVector& WorldLocation, float Margin) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float HalfL = Layout.RinkSize.X * 0.5f;
	const float HalfW = Layout.RinkSize.Y * 0.5f;
	if (FMath::Abs(Local.X) >= HalfL + Margin || FMath::Abs(Local.Y) >= HalfW + Margin || Local.Z <= -20.f)
	{
		return false;
	}
	// Rounded corner: inside the arc.
	const float R = Layout.CornerRadius;
	const float Dx = FMath::Abs(Local.X) - (HalfL - R);
	const float Dy = FMath::Abs(Local.Y) - (HalfW - R);
	return Dx <= 0.f || Dy <= 0.f || Dx * Dx + Dy * Dy < (R + Margin) * (R + Margin);
}

void ASkateArena::ResetScene()
{
	UWorld* World = GetWorld();
	if (World)
	{
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	}
	SlowMoRealEnd = -1.0;
	if (bPendingEndsSwap)
	{
		// Change of ends: the keepers cross over with their teams (goal 0's keeper plays for team 1 only in odd periods).
		bPendingEndsSwap = false;
		bEndsSwapped = !bEndsSwapped;
		for (ASkateGoalkeeper* Keeper : { Goalkeeper0.Get(), Goalkeeper1.Get() })
		{
			if (Keeper)
			{
				Keeper->SetGoal(this, GetAttackGoal(1 - Keeper->GetTeam()), Keeper->GetTeam());
			}
		}
	}
	if (World)
	{
		for (TActorIterator<ASkateCharacter> It(World); It; ++It)
		{
			It->ResetSkater(GetSpawnTransform(It->GetTeam(), It->GetTeamSlot()));
		}
	}
	if (Ball)
	{
		Ball->ResetBall(GetBallSpawnLocation());
	}
	for (ASkateGoalkeeper* Keeper : { Goalkeeper0.Get(), Goalkeeper1.Get() })
	{
		if (Keeper)
		{
			Keeper->ResetKeeper();
		}
	}
	bBallInGoal = false;
	GoalPauseLeft = -1.f;
	FaceOffLeft = Layout.FaceOffCountdown;
}

void ASkateArena::RestartMatch()
{
	if (IsSeriesOver())
	{
		Wins0 = 0;
		Wins1 = 0;
	}
	Score0 = 0;
	Score1 = 0;
	LastGoalTeam = INDEX_NONE;
	LastScorerSlot = INDEX_NONE;
	LastGoalTime = -1000.0;
	LastPeriodEndTime = -1000.0;
	Clock = Layout.MatchLength;
	Period = 1;
	bPendingEndsSwap = bEndsSwapped; // back to the first-period ends
	bMatchOver = false;
	GoalPauseLeft = -1.f;
	ResetScene();
}

void ASkateArena::PlaceBallInFront(ASkateCharacter* Skater)
{
	if (!Skater || !Ball)
	{
		return;
	}
	for (ASkateGoalkeeper* Keeper : { Goalkeeper0.Get(), Goalkeeper1.Get() })
	{
		if (Keeper && Keeper->GetKeeperState().bHolding)
		{
			Keeper->ResetKeeper(); // the keeper lets go of the ball
		}
	}
	const FVector Forward = Skater->GetActorForwardVector().GetSafeNormal2D();
	FVector Location = Skater->GetActorLocation() + Forward * 70.f;
	Location.Z = GetActorLocation().Z + Ball->GetRadius() + 0.5f;
	Ball->ResetBall(Location);
}

void ASkateArena::BuildMaterials()
{
	const FSkateBallPhysicsTuning Defaults;

	IceMaterial = NewObject<UPhysicalMaterial>(this, TEXT("PM_Ice"));
	IceMaterial->Friction = Defaults.IceFriction;
	IceMaterial->Restitution = Defaults.IceRestitution;
	IceMaterial->bOverrideFrictionCombineMode = true;
	IceMaterial->FrictionCombineMode = EFrictionCombineMode::Min; // ice stays slippery against any other material

	BoardMaterial = NewObject<UPhysicalMaterial>(this, TEXT("PM_Board"));
	// Low friction (Min combine): a spinning ball that hits the board must bounce off it, not climb it.
	BoardMaterial->Friction = 0.05f;
	BoardMaterial->bOverrideFrictionCombineMode = true;
	BoardMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
	BoardMaterial->Restitution = Defaults.BoardRestitution;

	NetMaterial = NewObject<UPhysicalMaterial>(this, TEXT("PM_Net"));
	NetMaterial->Friction = 0.9f;
	NetMaterial->Restitution = 0.08f;
	NetMaterial->bOverrideRestitutionCombineMode = true;
	NetMaterial->RestitutionCombineMode = EFrictionCombineMode::Min; // the net absorbs the ball

	BallMaterial = NewObject<UPhysicalMaterial>(this, TEXT("PM_Ball"));
	BallMaterial->Friction = Defaults.Friction;
	BallMaterial->Restitution = Defaults.Restitution;
}

UStaticMeshComponent* ASkateArena::AddBox(const FVector& Center, const FVector& Size, const FLinearColor& Color, UPhysicalMaterial* PhysMat, float Yaw)
{
	UStaticMeshComponent* Box = SkateVisuals::AddPart(this, Root, TEXT("Cube"), Color, PhysMat != nullptr);
	Box->SetRelativeTransform(FTransform(FRotator(0.f, Yaw, 0.f), Center, Size / 100.f));
	if (PhysMat)
	{
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetPhysMaterialOverride(PhysMat);
	}
	return Box;
}

void ASkateArena::AddBoard(const FVector2D& Center, float Length, float Yaw)
{
	using namespace SkateArenaDetail;
	const float T = Layout.BoardThickness;
	const float H = Layout.BoardHeight;
	UStaticMeshComponent* Board = AddBox(FVector(Center.X, Center.Y, H * 0.5f), FVector(Length, T, H), BoardColor, BoardMaterial, Yaw);
	Board->ComponentTags.Add(BoardTag());
	if (Layout.GlassHeight > 0.f)
	{
		UStaticMeshComponent* Glass = AddBox(FVector(Center.X, Center.Y, H + Layout.GlassHeight * 0.5f), FVector(Length, T, Layout.GlassHeight), BoardColor, BoardMaterial, Yaw);
		Glass->SetVisibility(false);
		Glass->SetCastShadow(false);
	}
	const float CapH = 6.f;
	SkateVisuals::AddPart(this, Root, TEXT("Cube"), BoardCapColor)->SetRelativeTransform(
		FTransform(FRotator(0.f, Yaw, 0.f), FVector(Center.X, Center.Y, H + CapH * 0.5f), FVector(Length, T, CapH) / 100.f));
}

void ASkateArena::BuildRink()
{
	using namespace SkateArenaDetail;
	const float L = Layout.RinkSize.X;
	const float W = Layout.RinkSize.Y;
	const float T = Layout.BoardThickness;
	const float R = FMath::Clamp(Layout.CornerRadius, 0.f, FMath::Min(L, W) * 0.5f);

	// Ice: top surface at z = 0.
	UStaticMeshComponent* Ice = AddBox(FVector(0.f, 0.f, -IceThickness * 0.5f), FVector(L + 2.f * T + 400.f, W + 2.f * T + 400.f, IceThickness), IceColor, IceMaterial);
	Ice->SetCastShadow(false);

	// Straight boards between the corners (the board sits outside the inner rink: centre at half size + T/2).
	const FVector2D Half(L * 0.5f + T * 0.5f, W * 0.5f + T * 0.5f);
	AddBoard(FVector2D(Half.X, 0.f), W - 2.f * R, 90.f);
	AddBoard(FVector2D(-Half.X, 0.f), W - 2.f * R, 90.f);
	AddBoard(FVector2D(0.f, Half.Y), L - 2.f * R, 0.f);
	AddBoard(FVector2D(0.f, -Half.Y), L - 2.f * R, 0.f);

	// Rounded corners: each quarter circle as short straight pieces tangent to the arc.
	const int32 Segments = 10;
	const float Step = 90.f / Segments;
	const float Chord = 2.f * (R + T * 0.5f) * FMath::Sin(FMath::DegreesToRadians(Step * 0.5f)) + 4.f; // small overlap, no gaps
	for (const float Sx : { -1.f, 1.f })
	{
		for (const float Sy : { -1.f, 1.f })
		{
			const FVector2D CornerCentre(Sx * (L * 0.5f - R), Sy * (W * 0.5f - R));
			for (int32 Index = 0; Index < Segments; ++Index)
			{
				// Angle of the piece's midpoint, measured in the corner's quadrant.
				const float Angle = FMath::DegreesToRadians((Index + 0.5f) * Step);
				const FVector2D Radial(Sx * FMath::Cos(Angle), Sy * FMath::Sin(Angle));
				const FVector2D Center = CornerCentre + Radial * (R + T * 0.5f);
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Radial.Y, Radial.X)) + 90.f; // tangent
				AddBoard(Center, Chord, Yaw);
			}
		}
	}
}

UStaticMeshComponent* ASkateArena::AddMarkLine(const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Color)
{
	const FVector2D D = B - A;
	const float Len = static_cast<float>(D.Size());
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(D.Y), static_cast<float>(D.X)));
	const FVector2D Mid = (A + B) * 0.5f;
	UStaticMeshComponent* Line = SkateVisuals::AddPart(this, Root, TEXT("Cube"), Color, false);
	Line->SetRelativeTransform(FTransform(FRotator(0.f, Yaw, 0.f), FVector(Mid.X, Mid.Y, SkateArenaDetail::MarkThickness * 0.5f),
		FVector(Len, Width, SkateArenaDetail::MarkThickness) / 100.f));
	return Line;
}

void ASkateArena::AddMarkRing(const FVector2D& Center, float Radius, float Width, const FLinearColor& Color, int32 Segments)
{
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A0 = 2.f * UE_PI * Index / Segments;
		const float A1 = 2.f * UE_PI * (Index + 1) / Segments;
		const FVector2D P0 = Center + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius;
		const FVector2D P1 = Center + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius;
		// Slightly longer than the chord so segments overlap without gaps.
		const FVector2D Ext = (P1 - P0) * 0.03f;
		AddMarkLine(P0 - Ext, P1 + Ext, Width, Color);
	}
}

void ASkateArena::AddMarkRect(const FVector2D& Center, const FVector2D& Size, const FLinearColor& Color)
{
	UStaticMeshComponent* Rect = SkateVisuals::AddPart(this, Root, TEXT("Cube"), Color, false);
	Rect->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, SkateArenaDetail::MarkThickness * 0.4f),
		FVector(Size.X, Size.Y, SkateArenaDetail::MarkThickness * 0.8f) / 100.f));
}

void ASkateArena::AddCone(const FVector2D& Location)
{
	using namespace SkateArenaDetail;
	UStaticMeshComponent* Cone = SkateVisuals::AddPart(this, Root, TEXT("Cone"), ConeColor, true);
	Cone->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(Location.X, Location.Y, ConeHeight * 0.5f), FVector(ConeBase, ConeBase, ConeHeight) / 100.f));
	Cone->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Cone->SetPhysMaterialOverride(BoardMaterial);
}

void ASkateArena::AddLabel(const FVector2D& Location, const FString& Text, const FColor& Color, float Size)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->RegisterComponent();
	Label->SetText(FText::FromString(Text));
	Label->SetTextRenderColor(Color);
	Label->SetWorldSize(Size);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	// Laid back to face the side camera (on the +Y stands, pitched down).
	Label->SetRelativeLocationAndRotation(FVector(Location.X, Location.Y, 2.f), FRotator(40.f, 90.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCastShadow(false);
}

void ASkateArena::BuildMarkings()
{
	using namespace SkateArenaDetail;
	const float L = Layout.RinkSize.X;
	const float W = Layout.RinkSize.Y;
	const float LineX = L * 0.5f - Layout.GoalLineInset;

	// Centre line, blue lines, goal lines across the full width.
	AddMarkLine(FVector2D(0.f, -W * 0.5f), FVector2D(0.f, W * 0.5f), 30.f, RedLine);
	AddMarkLine(FVector2D(-Layout.BlueLineX, -W * 0.5f), FVector2D(-Layout.BlueLineX, W * 0.5f), 30.f, BlueLine);
	AddMarkLine(FVector2D(Layout.BlueLineX, -W * 0.5f), FVector2D(Layout.BlueLineX, W * 0.5f), 30.f, BlueLine);
	AddMarkLine(FVector2D(-LineX, -W * 0.5f), FVector2D(-LineX, W * 0.5f), 5.f, RedLine);
	AddMarkLine(FVector2D(LineX, -W * 0.5f), FVector2D(LineX, W * 0.5f), 5.f, RedLine);

	// Centre circle and dot, four end-zone face-off circles.
	AddMarkRing(FVector2D::ZeroVector, FaceOffRadius, 5.f, BlueLine, 64);
	AddMarkRing(FVector2D::ZeroVector, 15.f, 30.f, BlueLine, 12);
	const float CircleX = LineX - 600.f;
	const float CircleY = W * 0.5f - 800.f;
	for (const float Sx : { -1.f, 1.f })
	{
		for (const float Sy : { -1.f, 1.f })
		{
			AddMarkRing(FVector2D(Sx * CircleX, Sy * CircleY), FaceOffRadius, 5.f, RedLine, 64);
			AddMarkRing(FVector2D(Sx * CircleX, Sy * CircleY), 15.f, 30.f, RedLine, 12);
		}
	}
}

void ASkateArena::BuildTrainingCourse()
{
	using namespace SkateArenaDetail;

	// Acceleration straight: start line, lane edges, stop zone. Stop zone sized for the braking distance (~1.6 m).
	const float LaneHalf = 150.f;
	const float StopStart = Layout.StopZoneCenterX - Layout.StopZoneLength * 0.5f;
	const float StopEnd = Layout.StopZoneCenterX + Layout.StopZoneLength * 0.5f;
	AddMarkLine(FVector2D(Layout.AccelStartX, Layout.AccelLaneY - LaneHalf), FVector2D(Layout.AccelStartX, Layout.AccelLaneY + LaneHalf), 14.f, RedLine);
	AddMarkLine(FVector2D(Layout.AccelStartX - 300.f, Layout.AccelLaneY - LaneHalf), FVector2D(StopEnd + 150.f, Layout.AccelLaneY - LaneHalf), LineWidth, LaneColor);
	AddMarkLine(FVector2D(Layout.AccelStartX - 300.f, Layout.AccelLaneY + LaneHalf), FVector2D(StopEnd + 150.f, Layout.AccelLaneY + LaneHalf), LineWidth, LaneColor);
	AddMarkRect(FVector2D(Layout.StopZoneCenterX, Layout.AccelLaneY), FVector2D(Layout.StopZoneLength, LaneHalf * 2.f), StopZoneColor);
	AddMarkLine(FVector2D(StopEnd, Layout.AccelLaneY - LaneHalf), FVector2D(StopEnd, Layout.AccelLaneY + LaneHalf), 14.f, RedLine);
	AddLabel(FVector2D(Layout.AccelStartX - 150.f, Layout.AccelLaneY - LaneHalf - 120.f), TEXT("ACCELERATE >"), FColor(20, 60, 110));
	AddLabel(FVector2D(StopStart - 60.f, Layout.AccelLaneY), TEXT("STOP ZONE"), FColor(120, 80, 0), 60.f);

	// Turning circle.
	AddMarkRing(Layout.TurnCircleCenter, Layout.TurnCircleRadius, 14.f, RedLine, 64);
	AddLabel(Layout.TurnCircleCenter, TEXT("CIRCLE"), FColor(130, 20, 20));

	// Slalom cones with a gate line before the first cone.
	for (int32 Index = 0; Index < Layout.SlalomCones; ++Index)
	{
		AddCone(FVector2D(Layout.SlalomStartX + Index * Layout.SlalomSpacing, Layout.SlalomY));
	}
	AddMarkLine(FVector2D(Layout.SlalomStartX - 300.f, Layout.SlalomY - 200.f), FVector2D(Layout.SlalomStartX - 300.f, Layout.SlalomY + 200.f), 14.f, BlueLine);
	AddLabel(FVector2D(Layout.SlalomStartX - 450.f, Layout.SlalomY), TEXT("SLALOM"), FColor(150, 70, 0));

	// Figure eight: two touching circles.
	const FVector2D Offset(Layout.FigureEightRadius, 0.f);
	AddMarkRing(Layout.FigureEightCenter - Offset, Layout.FigureEightRadius, 12.f, BlueLine, 56);
	AddMarkRing(Layout.FigureEightCenter + Offset, Layout.FigureEightRadius, 12.f, BlueLine, 56);
	AddLabel(Layout.FigureEightCenter - FVector2D(Layout.FigureEightRadius + 120.f, 0.f), TEXT("FIGURE 8"), FColor(20, 40, 140));
}

void ASkateArena::BuildGoal(float Sign)
{
	using namespace SkateArenaDetail;
	// Local X grows into the net (away from the rink); mirrored by Sign for the -X goal.
	const float LineX = Layout.RinkSize.X * 0.5f - Layout.GoalLineInset;
	const float BackX = LineX + Layout.GoalDepth;
	const float Y0 = -Layout.GoalWidth * 0.5f;
	const float Y1 = Layout.GoalWidth * 0.5f;
	const float H = Layout.GoalHeight;
	auto X = [Sign](float LocalX) { return Sign * LocalX; };

	auto AddPost = [&](const FVector& A, const FVector& B)
	{
		UStaticMeshComponent* Post = SkateVisuals::AddPart(this, Root, TEXT("Cylinder"), GoalColor, true);
		SkateVisuals::SetSegment(Post, A, B, PostRadius * 2.f, PostRadius * 2.f);
		Post->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Post->SetPhysMaterialOverride(BoardMaterial);
	};
	AddPost(FVector(X(LineX), Y0, 0.f), FVector(X(LineX), Y0, H));
	AddPost(FVector(X(LineX), Y1, 0.f), FVector(X(LineX), Y1, H));
	AddPost(FVector(X(LineX), Y0 - PostRadius, H), FVector(X(LineX), Y1 + PostRadius, H));

	// Net: back, sides, roof. Absorbing material.
	const float Depth = Layout.GoalDepth;
	AddBox(FVector(X(BackX - NetThickness * 0.5f), 0.f, H * 0.5f), FVector(NetThickness, Layout.GoalWidth, H), NetColor, NetMaterial);
	AddBox(FVector(X(LineX + Depth * 0.5f), Y0 - NetThickness, H * 0.5f), FVector(Depth, NetThickness, H), NetColor, NetMaterial);
	AddBox(FVector(X(LineX + Depth * 0.5f), Y1 + NetThickness, H * 0.5f), FVector(Depth, NetThickness, H), NetColor, NetMaterial);
	AddBox(FVector(X(LineX + Depth * 0.5f), 0.f, H + NetThickness), FVector(Depth, Layout.GoalWidth, NetThickness), NetColor, NetMaterial);

	// Crease in front, trapezoid behind (goal line to the end boards).
	AddMarkRect(FVector2D(X(LineX - 90.f), 0.f), FVector2D(180.f, Layout.GoalWidth), FLinearColor(0.35f, 0.55f, 0.85f));
	const float BoardX = Layout.RinkSize.X * 0.5f;
	AddMarkLine(FVector2D(X(LineX), -TrapezoidNearHalf), FVector2D(X(BoardX), -TrapezoidFarHalf), 5.f, RedLine);
	AddMarkLine(FVector2D(X(LineX), TrapezoidNearHalf), FVector2D(X(BoardX), TrapezoidFarHalf), 5.f, RedLine);
}

void ASkateArena::BuildLighting()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		return; // the level already has its own lighting
	}

	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this, TEXT("Sun"));
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetupAttachment(Root);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetIntensity(8.f);
	Sun->RegisterComponent();
	Sun->SetWorldRotation(FRotator(-52.f, 35.f, 0.f));

	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(this, TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);
	Atmosphere->RegisterComponent();

	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(this, TEXT("SkyLight"));
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetupAttachment(Root);
	SkyLight->RegisterComponent();
	SkyLight->SetIntensity(1.f);
}

void ASkateArena::SpawnBall()
{
	UWorld* World = GetWorld();
	if (!World || !BallClass)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Ball = World->SpawnActor<ASkateBall>(BallClass, FTransform(GetBallSpawnLocation()), Params);
	if (Ball)
	{
		Ball->SetIceZ(static_cast<float>(GetActorLocation().Z));
		Ball->SetPhysicalMaterial(BallMaterial);
		Ball->ResetBall(GetBallSpawnLocation());
	}
	else
	{
		UE_LOG(LogIceSkate, Error, TEXT("ASkateArena: failed to spawn the ball"));
	}
}

void ASkateArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Streamed levels (World Partition) can load their ground a moment after BeginPlay: re-check briefly.
	if (SettleTimer < 3.f)
	{
		SettleTimer += DeltaSeconds;
		SettleCheckAccumulator += DeltaSeconds;
		if (SettleCheckAccumulator >= 0.25f)
		{
			SettleCheckAccumulator = 0.f;
			SettleOnGround();
		}
	}
	if (!Ball)
	{
		return;
	}

	// Match clock, the pause after a goal, the face-off countdown.
	TimeSinceDrop += DeltaSeconds;
	if (GoalPauseLeft >= 0.f)
	{
		GoalPauseLeft -= DeltaSeconds;
		if (GoalPauseLeft < 0.f)
		{
			ResetScene();
		}
		return;
	}
	if (FaceOffLeft >= 0.f)
	{
		FaceOffLeft -= DeltaSeconds;
		Ball->ResetBall(GetBallSpawnLocation()); // waits on the spot
		if (FaceOffLeft < 0.f)
		{
			// The drop: a small random nudge, so the face-off is not a pure reaction test.
			const FVector Nudge(FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-260.f, 260.f), 0.f);
			Ball->ApplyGameplayVelocity(Nudge, ESkateImpulseKind::Touch, this);
			TimeSinceDrop = 0.f;
		}
		return;
	}
	// The goal moment: slow motion for GoalSlowMoTime real seconds, then the clock runs on at full speed.
	if (SlowMoRealEnd >= 0.0 && GetWorld()->GetRealTimeSeconds() >= SlowMoRealEnd)
	{
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
		SlowMoRealEnd = -1.0;
	}
	if (!bMatchOver)
	{
		Clock = FMath::Max(0.f, Clock - DeltaSeconds);
		if (Clock <= 0.f)
		{
			if (Period < GetPeriods())
			{
				// End of a period: a short break, then the teams change ends.
				++Period;
				Clock = Layout.MatchLength;
				bPendingEndsSwap = true;
				GoalPauseLeft = Layout.PeriodBreak;
				LastPeriodEndTime = GetWorld()->GetTimeSeconds();
				UE_LOG(LogIceSkate, Log, TEXT("END OF PERIOD %d: %d:%d, change of ends"), Period - 1, Score0, Score1);
				return;
			}
			bMatchOver = true;
			if (Score0 != Score1)
			{
				(Score0 > Score1 ? Wins0 : Wins1)++;
			}
			UE_LOG(LogIceSkate, Log, TEXT("FULL TIME %d:%d, series %d-%d"), Score0, Score1, Wins0, Wins1);
		}
	}

	// Goal: ball centre fully behind the goal line, between the posts, under the bar.
	const int32 InGoal = BallInGoal();
	if (InGoal != INDEX_NONE && !bBallInGoal && !bMatchOver)
	{
		LastGoalTeam = GetAttackGoal(0) == InGoal ? 0 : 1;
		(LastGoalTeam == 0 ? Score0 : Score1)++;
		LastGoalTime = GetWorld()->GetTimeSeconds();
		GoalPauseLeft = Layout.GoalPause;
		// The scorer: the skater that last played the ball, if it is on the scoring team.
		LastScorerSlot = INDEX_NONE;
		if (const USkateBallControlComponent* Source = Cast<USkateBallControlComponent>(Ball->GetLastImpulseSource()))
		{
			if (const ASkateCharacter* Scorer = Cast<ASkateCharacter>(Source->GetOwner()))
			{
				LastScorerSlot = Scorer->GetTeam() == LastGoalTeam ? Scorer->GetTeamSlot() : INDEX_NONE;
			}
		}
		if (Layout.GoalSlowMo < 1.f && Layout.GoalSlowMoTime > 0.f)
		{
			UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Layout.GoalSlowMo);
			SlowMoRealEnd = GetWorld()->GetRealTimeSeconds() + Layout.GoalSlowMoTime;
		}
		UE_LOG(LogIceSkate, Log, TEXT("GOAL team %d (slot %d): %d:%d"), LastGoalTeam, LastScorerSlot, Score0, Score1);
	}
	bBallInGoal = InGoal != INDEX_NONE;

	// Out of play (over the glass, through a seam): face-off, like a puck leaving the rink.
	const FVector BallLoc = Ball->GetActorLocation();
	if (!IsInsideRink(BallLoc, 60.f) || BallLoc.Z - GetActorLocation().Z > Layout.BoardHeight + Layout.GlassHeight + 200.f)
	{
		UE_LOG(LogIceSkate, Log, TEXT("Ball out of play at (%.0f, %.0f, %.0f): face-off"), BallLoc.X, BallLoc.Y, BallLoc.Z);
		ResetScene();
	}
}

int32 ASkateArena::BallInGoal() const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Ball->GetActorLocation());
	const float LineX = Layout.RinkSize.X * 0.5f - Layout.GoalLineInset;
	if (FMath::Abs(Local.Y) >= Layout.GoalWidth * 0.5f || Local.Z >= Layout.GoalHeight)
	{
		return INDEX_NONE;
	}
	// Inside the net only: behind the net (between it and the end boards) is open ice, as in hockey.
	const float R = Ball->GetRadius();
	if (Local.X > LineX + R && Local.X < LineX + Layout.GoalDepth)
	{
		return 0;
	}
	if (Local.X < -LineX - R && Local.X > -LineX - Layout.GoalDepth)
	{
		return 1;
	}
	return INDEX_NONE;
}

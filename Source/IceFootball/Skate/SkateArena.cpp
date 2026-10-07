#include "Skate/SkateArena.h"

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
}

ASkateArena::ASkateArena()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BallClass = ASkateBall::StaticClass();
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
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASkateCharacter> It(World); It; ++It)
		{
			ResetScene(*It);
		}
	}
	if (Ball)
	{
		Ball->ResetBall(GetBallSpawnLocation());
	}
	return true;
}

void ASkateArena::BeginPlay()
{
	Super::BeginPlay();
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
	BuildGoal();
	if (bSpawnLightingIfMissing)
	{
		BuildLighting();
	}
	SpawnBall();
	// The skater may have been spawned before the rink moved: put it on the ice.
	for (TActorIterator<ASkateCharacter> It(GetWorld()); It; ++It)
	{
		ResetScene(*It);
	}
}

FTransform ASkateArena::GetPlayerSpawnTransform() const
{
	// Capsule half height 92 + 2 cm clearance above the ice (z = 0 in arena space).
	const FVector Local(Layout.PlayerSpawn.X, Layout.PlayerSpawn.Y, 94.f);
	return FTransform(FRotator(0.f, Layout.PlayerSpawnYaw, 0.f), GetActorTransform().TransformPosition(Local));
}

FVector ASkateArena::GetBallSpawnLocation() const
{
	const float Radius = Ball ? Ball->GetRadius() : 11.f;
	return GetActorTransform().TransformPosition(FVector(Layout.BallSpawn.X, Layout.BallSpawn.Y, Radius + 0.5f));
}

bool ASkateArena::IsInsideRink(const FVector& WorldLocation, float Margin) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	return FMath::Abs(Local.X) < Layout.RinkSize.X * 0.5f + Margin && FMath::Abs(Local.Y) < Layout.RinkSize.Y * 0.5f + Margin && Local.Z > -20.f;
}

void ASkateArena::ResetScene(ASkateCharacter* Skater)
{
	if (Skater)
	{
		Skater->ResetSkater(GetPlayerSpawnTransform());
	}
	if (Ball)
	{
		Ball->ResetBall(GetBallSpawnLocation());
	}
	bBallInGoal = false;
}

void ASkateArena::PlaceBallInFront(ASkateCharacter* Skater)
{
	if (!Skater || !Ball)
	{
		return;
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
	BoardMaterial->Friction = 0.3f;
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

void ASkateArena::BuildRink()
{
	using namespace SkateArenaDetail;
	const float L = Layout.RinkSize.X;
	const float W = Layout.RinkSize.Y;
	const float T = Layout.BoardThickness;
	const float H = Layout.BoardHeight;

	// Ice: top surface at z = 0.
	UStaticMeshComponent* Ice = AddBox(FVector(0.f, 0.f, -IceThickness * 0.5f), FVector(L + 2.f * T + 400.f, W + 2.f * T + 400.f, IceThickness), IceColor, IceMaterial);
	Ice->SetCastShadow(false);

	// Boards with a coloured cap so the edge reads from above.
	const FVector2D Half(L * 0.5f + T * 0.5f, W * 0.5f + T * 0.5f);
	AddBox(FVector(Half.X, 0.f, H * 0.5f), FVector(T, W + 2.f * T, H), BoardColor, BoardMaterial);
	AddBox(FVector(-Half.X, 0.f, H * 0.5f), FVector(T, W + 2.f * T, H), BoardColor, BoardMaterial);
	AddBox(FVector(0.f, Half.Y, H * 0.5f), FVector(L, T, H), BoardColor, BoardMaterial);
	AddBox(FVector(0.f, -Half.Y, H * 0.5f), FVector(L, T, H), BoardColor, BoardMaterial);
	const float CapH = 6.f;
	SkateVisuals::AddPart(this, Root, TEXT("Cube"), BoardCapColor)->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(Half.X, 0.f, H + CapH * 0.5f), FVector(T, W + 2.f * T, CapH) / 100.f));
	SkateVisuals::AddPart(this, Root, TEXT("Cube"), BoardCapColor)->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(-Half.X, 0.f, H + CapH * 0.5f), FVector(T, W + 2.f * T, CapH) / 100.f));
	SkateVisuals::AddPart(this, Root, TEXT("Cube"), BoardCapColor)->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, Half.Y, H + CapH * 0.5f), FVector(L, T, CapH) / 100.f));
	SkateVisuals::AddPart(this, Root, TEXT("Cube"), BoardCapColor)->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, -Half.Y, H + CapH * 0.5f), FVector(L, T, CapH) / 100.f));
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
	// Laid back to face the default top-down camera (looking along +X, pitched down).
	Label->SetRelativeLocationAndRotation(FVector(Location.X, Location.Y, 2.f), FRotator(56.f, 180.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCastShadow(false);
}

void ASkateArena::BuildMarkings()
{
	using namespace SkateArenaDetail;
	const float L = Layout.RinkSize.X;
	const float W = Layout.RinkSize.Y;

	// Centre red line and two blue lines (orientation cues only).
	AddMarkLine(FVector2D(0.f, -W * 0.5f), FVector2D(0.f, W * 0.5f), 30.f, RedLine);
	AddMarkLine(FVector2D(-L * 0.25f, -W * 0.5f), FVector2D(-L * 0.25f, W * 0.5f), 25.f, BlueLine);
	AddMarkLine(FVector2D(L * 0.25f, -W * 0.5f), FVector2D(L * 0.25f, W * 0.5f), 25.f, BlueLine);

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

	// Ball spot and dribble hint.
	AddMarkRing(Layout.BallSpawn, 30.f, 6.f, RedLine, 16);
	AddLabel(Layout.BallSpawn - FVector2D(180.f, 0.f), TEXT("BALL  >  GOAL"), FColor(30, 30, 30), 55.f);
}

void ASkateArena::BuildGoal()
{
	using namespace SkateArenaDetail;
	const float LineX = Layout.RinkSize.X * 0.5f - Layout.GoalDepth;
	const float BackX = Layout.RinkSize.X * 0.5f - NetThickness * 0.5f;
	const float Y0 = Layout.GoalCenterY - Layout.GoalWidth * 0.5f;
	const float Y1 = Layout.GoalCenterY + Layout.GoalWidth * 0.5f;
	const float H = Layout.GoalHeight;

	auto AddPost = [&](const FVector& A, const FVector& B)
	{
		UStaticMeshComponent* Post = SkateVisuals::AddPart(this, Root, TEXT("Cylinder"), GoalColor, true);
		SkateVisuals::SetSegment(Post, A, B, PostRadius * 2.f, PostRadius * 2.f);
		Post->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Post->SetPhysMaterialOverride(BoardMaterial);
	};
	AddPost(FVector(LineX, Y0, 0.f), FVector(LineX, Y0, H));
	AddPost(FVector(LineX, Y1, 0.f), FVector(LineX, Y1, H));
	AddPost(FVector(LineX, Y0 - PostRadius, H), FVector(LineX, Y1 + PostRadius, H));

	// Net: back, sides, roof. Absorbing material.
	const float Depth = Layout.GoalDepth;
	AddBox(FVector(BackX, Layout.GoalCenterY, H * 0.5f), FVector(NetThickness, Layout.GoalWidth, H), NetColor, NetMaterial);
	AddBox(FVector(LineX + Depth * 0.5f, Y0 - NetThickness, H * 0.5f), FVector(Depth, NetThickness, H), NetColor, NetMaterial);
	AddBox(FVector(LineX + Depth * 0.5f, Y1 + NetThickness, H * 0.5f), FVector(Depth, NetThickness, H), NetColor, NetMaterial);
	AddBox(FVector(LineX + Depth * 0.5f, Layout.GoalCenterY, H + NetThickness), FVector(Depth, Layout.GoalWidth, NetThickness), NetColor, NetMaterial);

	// Goal line and crease.
	AddMarkLine(FVector2D(LineX, Y0), FVector2D(LineX, Y1), 10.f, RedLine);
	AddMarkRect(FVector2D(LineX - 90.f, Layout.GoalCenterY), FVector2D(180.f, Layout.GoalWidth), FLinearColor(0.35f, 0.55f, 0.85f));
	AddLabel(FVector2D(LineX - 260.f, Layout.GoalCenterY), TEXT("GOAL"), FColor(140, 20, 20), 80.f);
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
	// Goal: ball centre fully behind the goal line, between the posts, under the bar.
	const FVector Local = GetActorTransform().InverseTransformPosition(Ball->GetActorLocation());
	const float LineX = Layout.RinkSize.X * 0.5f - Layout.GoalDepth;
	const bool bInside = Local.X > LineX + Ball->GetRadius()
		&& FMath::Abs(Local.Y - Layout.GoalCenterY) < Layout.GoalWidth * 0.5f
		&& Local.Z < Layout.GoalHeight;
	if (bInside && !bBallInGoal)
	{
		++Goals;
		LastGoalTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogIceSkate, Log, TEXT("GOAL #%d"), Goals);
	}
	bBallInGoal = bInside;
}

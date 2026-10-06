#include "Skate/SkateBall.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "IceFootball.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Skate/SkateVisuals.h"
#include "UObject/ConstructorHelpers.h"

namespace SkateBallDetail
{
	// Ball counts as on the ice when its lowest point is within this height (cm) and it is not moving up/down fast.
	constexpr float GroundTolerance = 2.f;
	constexpr float GroundVerticalSpeed = 40.f;
	// Board/wall impact (normal impulse, kg*cm/s) mapped to a 0..1 bounce sound.
	constexpr float BounceImpulseForFullSound = 600.f;
}

ASkateBall::ASkateBall()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	SetRootComponent(BallMesh);
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
	}
	BallMesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// Single impulse source: the skater capsule (Pawn) never collides with the ball.
	BallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	BallMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BallMesh->SetSimulatePhysics(true);
	BallMesh->SetNotifyRigidBodyCollision(true);
	BallMesh->BodyInstance.bUseCCD = true;
	BallMesh->SetCanEverAffectNavigation(false);

	// Two thin bands make the spin readable.
	auto MakeStripe = [this, &CylinderMesh](const TCHAR* Name, const FRotator& Rotation)
	{
		UStaticMeshComponent* Stripe = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Stripe->SetupAttachment(BallMesh);
		if (CylinderMesh.Succeeded())
		{
			Stripe->SetStaticMesh(CylinderMesh.Object);
		}
		Stripe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Stripe->SetRelativeRotation(Rotation);
		Stripe->SetRelativeScale3D(FVector(1.02f, 1.02f, 0.16f));
		return Stripe;
	};
	StripeA = MakeStripe(TEXT("StripeA"), FRotator::ZeroRotator);
	StripeB = MakeStripe(TEXT("StripeB"), FRotator(0.f, 0.f, 90.f));
}

void ASkateBall::BeginPlay()
{
	Super::BeginPlay();
	BallMesh->SetMaterial(0, SkateVisuals::MakeColorMaterial(this, FLinearColor(0.95f, 0.95f, 0.92f)));
	StripeA->SetMaterial(0, SkateVisuals::MakeColorMaterial(this, FLinearColor(0.05f, 0.08f, 0.25f)));
	StripeB->SetMaterial(0, SkateVisuals::MakeColorMaterial(this, FLinearColor(0.9f, 0.15f, 0.1f)));
	BallMesh->OnComponentHit.AddDynamic(this, &ASkateBall::OnBallHit);
	ApplyPhysicsTuning(Tuning);
}

void ASkateBall::ApplyPhysicsTuning(const FSkateBallPhysicsTuning& InTuning)
{
	Tuning = InTuning;
	BallMesh->SetWorldScale3D(FVector(Tuning.Radius / 50.f));
	BallMesh->SetMassOverrideInKg(NAME_None, Tuning.MassKg, true);
	BallMesh->SetLinearDamping(Tuning.LinearDamping);
	BallMesh->SetAngularDamping(Tuning.AngularDamping);
	BallMesh->SetUseCCD(Tuning.bUseCCD);
}

void ASkateBall::SetPhysicalMaterial(UPhysicalMaterial* Material)
{
	BallMesh->SetPhysMaterialOverride(Material);
}

FVector ASkateBall::GetBallVelocity() const
{
	return BallMesh->GetPhysicsLinearVelocity();
}

void ASkateBall::ApplyGameplayVelocity(const FVector& NewVelocity, ESkateImpulseKind Kind)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const double Gap = Now - LastImpulseTime;
	if (Gap < DoubleImpulseWindow)
	{
		++DoubleImpulseFaults;
		LastDoubleImpulseGap = static_cast<float>(Gap);
		LastFaultTime = Now;
		UE_LOG(LogIceSkate, Warning, TEXT("DOUBLE IMPULSE on ball: %s %.3f s after the previous one"), ANSI_TO_TCHAR(SkateImpulseKindName(Kind)), Gap);
	}
	LastImpulseTime = Now;
	++GameplayImpulses;

	BallMesh->SetPhysicsLinearVelocity(NewVelocity);
	// Rolling spin that matches the new planar velocity (w = up x v / r), so the ball rolls instead of sliding.
	const FVector Planar(NewVelocity.X, NewVelocity.Y, 0.0);
	const FVector Spin = FVector::CrossProduct(FVector::UpVector, Planar) / FMath::Max(Tuning.Radius, 1.f);
	BallMesh->SetPhysicsAngularVelocityInRadians(Spin);
}

void ASkateBall::ResetBall(const FVector& Location)
{
	SetActorLocationAndRotation(Location, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	BallMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	BallMesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	BallMesh->WakeRigidBody();
	LastImpulseTime = -1000.0;
}

void ASkateBall::Tick(float DeltaSeconds)
{
	using namespace SkateBallDetail;
	Super::Tick(DeltaSeconds);
	if (!BallMesh->IsSimulatingPhysics() || DeltaSeconds <= 0.f)
	{
		return;
	}

	const FVector Velocity = BallMesh->GetPhysicsLinearVelocity();
	const float Height = static_cast<float>(GetActorLocation().Z) - Tuning.Radius - IceZ;
	bGrounded = Height < GroundTolerance && FMath::Abs(Velocity.Z) < GroundVerticalSpeed;
	if (!bGrounded)
	{
		return; // in the air only gravity + damping act; vertical velocity is never touched
	}

	const FVector Planar(Velocity.X, Velocity.Y, 0.0);
	const float Speed = static_cast<float>(Planar.Size());
	if (Speed < Tuning.StopSpeed)
	{
		if (Speed > 0.f)
		{
			// Near-zero residual only: stop planar motion, keep the vertical component as is.
			BallMesh->SetPhysicsLinearVelocity(FVector(0.0, 0.0, Velocity.Z));
			BallMesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		}
		return;
	}
	// Rolling resistance as an acceleration opposite to travel, never more than what stops the ball this frame.
	const float Decel = FMath::Min(Tuning.RollingResistance, Speed / DeltaSeconds);
	BallMesh->AddForce(-Planar / Speed * Decel, NAME_None, /*bAccelChange*/ true);
}

float ASkateBall::ConsumeBounceStrength()
{
	const float Value = PendingBounce;
	PendingBounce = 0.f;
	return Value;
}

void ASkateBall::OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor && OtherActor->IsA<APawn>())
	{
		++PawnContactFaults;
		LastFaultTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogIceSkate, Warning, TEXT("Ball had a PHYSICS contact with pawn %s (should be ignored)"), *OtherActor->GetName());
		return;
	}
	// Ignore rolling contact with the ice (impulse mostly vertical and small).
	const float Strength = FMath::Clamp(static_cast<float>(NormalImpulse.Size2D()) / SkateBallDetail::BounceImpulseForFullSound, 0.f, 1.f);
	PendingBounce = FMath::Max(PendingBounce, Strength);
}

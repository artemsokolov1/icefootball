#include "Skate/SkateGoalkeeper.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Skate/SkateArena.h"
#include "Skate/SkateBall.h"
#include "EngineUtils.h"
#include "IceFootball.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateVisuals.h"

namespace SkateGoalkeeperDetail
{
	const FLinearColor JerseyTeam0(0.55f, 0.10f, 0.08f);
	const FLinearColor JerseyTeam1(0.08f, 0.18f, 0.55f);
	const FLinearColor Pants(0.05f, 0.05f, 0.06f);
	const FLinearColor Skin(0.92f, 0.72f, 0.58f);
	const FLinearColor Gloves(0.95f, 0.95f, 0.90f);
	const FLinearColor Dark(0.03f, 0.03f, 0.03f);

	constexpr float HipHeight = 95.f;
	// Pivot-space body points (X forward into the rink, Y to the keeper's right = goal lateral, Z up).
	const FVector Shoulder(0.f, 22.f, 50.f);
	const FVector Hip(0.f, 12.f, -6.f);
	const FVector Ankle(6.f, 26.f, -86.f);

	FVector2D ToVector2D(const FSkateVec2& V) { return FVector2D(V.X, V.Y); }
	FSkateVec3 ToSkate(const FVector& V) { return FSkateVec3(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z)); }
	FVector ToVector(const FSkateVec3& V) { return FVector(V.X, V.Y, V.Z); }
}

ASkateGoalkeeper::ASkateGoalkeeper()
{
	PrimaryActorTick.bCanEverTick = true;
	// Before the physics step: a save changes the ball's velocity before it would reach the goal.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);
	BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, SkateGoalkeeperDetail::HipHeight));
	HandLocal[0] = FVector(28.f, -48.f, 22.f);
	HandLocal[1] = FVector(28.f, 48.f, 22.f);
}

void ASkateGoalkeeper::SetGoal(ASkateArena* InArena, int32 InGoalIndex, int32 InTeam)
{
	Arena = InArena;
	GoalIndex = InGoalIndex;
	Team = InTeam;
}

void ASkateGoalkeeper::BeginPlay()
{
	Super::BeginPlay();
	BuildBody();
	ResetKeeper();
}

void ASkateGoalkeeper::BuildBody()
{
	using namespace SkateGoalkeeperDetail;
	if (bBuilt)
	{
		return;
	}
	const FLinearColor& Jersey = Team == 0 ? JerseyTeam0 : JerseyTeam1;
	auto Add = [this](const TCHAR* Shape, const FLinearColor& Color)
	{
		return SkateVisuals::AddPart(this, BodyPivot, Shape, Color, true);
	};
	Pelvis = Add(TEXT("Cube"), Pants);
	Torso = Add(TEXT("Cube"), Jersey);
	Head = Add(TEXT("Sphere"), Skin);
	Cap = Add(TEXT("Cube"), Dark);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		Arm[Side] = Add(TEXT("Cylinder"), Jersey);
		Glove[Side] = Add(TEXT("Sphere"), Gloves);
		Leg[Side] = Add(TEXT("Cylinder"), Pants);
		Skate[Side] = Add(TEXT("Cube"), Dark);
	}
	Pelvis->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 0.f), FVector(22.f, 34.f, 20.f) / 100.f));
	Torso->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 32.f), FVector(26.f, 44.f, 50.f) / 100.f));
	Head->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(2.f, 0.f, 72.f), FVector(0.24f)));
	Cap->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(12.f, 0.f, 77.f), FVector(8.f, 18.f, 6.f) / 100.f));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Mirror(1.f, Side == 0 ? -1.f : 1.f, 1.f);
		SkateVisuals::SetSegment(Leg[Side], Hip * Mirror, Ankle * Mirror, 15.f, 15.f);
		Skate[Side]->SetRelativeTransform(FTransform(FRotator::ZeroRotator, Ankle * Mirror + FVector(6.f, 0.f, -5.f), FVector(30.f, 9.f, 9.f) / 100.f));
	}
	bBuilt = true;
}

FSkateGoalFrame ASkateGoalkeeper::GoalFrame() const
{
	if (const ASkateArena* Rink = Arena.Get())
	{
		return Rink->GetGoalFrame(GoalIndex);
	}
	return FSkateGoalFrame();
}

FVector2D ASkateGoalkeeper::ThrowTarget() const
{
	// Roll the ball out to the nearest skater of the keeper's own team.
	const ASkateCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ASkateCharacter> It(GetWorld()); It; ++It)
	{
		const float Dist = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), GetActorLocation()));
		if (It->GetTeam() == Team && Dist < BestDist)
		{
			Best = *It;
			BestDist = Dist;
		}
	}
	if (Best)
	{
		return FVector2D(Best->GetActorLocation());
	}
	const FSkateGoalFrame Goal = GoalFrame();
	return SkateGoalkeeperDetail::ToVector2D(Goal.ToWorld(1500.f, 0.f));
}

void ASkateGoalkeeper::ResetKeeper()
{
	if (State.bHolding)
	{
		if (ASkateArena* Rink = Arena.Get())
		{
			if (ASkateBall* Ball = Rink->GetBall())
			{
				Ball->ClearHolder(this);
				Ball->DropHold();
			}
		}
	}
	FSkateKeeper::Reset(State);
	ApplyPose(FSkateKeeper::Pose(Tuning, GoalFrame(), State), 0.f);
}

void ASkateGoalkeeper::Tick(float DeltaSeconds)
{
	using namespace SkateGoalkeeperDetail;
	Super::Tick(DeltaSeconds);
	ASkateArena* Rink = Arena.Get();
	if (!Rink || DeltaSeconds <= 0.f)
	{
		return;
	}
	const FSkateGoalFrame Goal = Rink->GetGoalFrame(GoalIndex);
	ASkateBall* Ball = Rink->GetBall();

	FSkateKeeperBall KeeperBall;
	if (Ball)
	{
		KeeperBall.bValid = true;
		KeeperBall.Pos = ToSkate(Ball->GetActorLocation());
		KeeperBall.Vel = ToSkate(Ball->GetBallVelocity());
		KeeperBall.Radius = Ball->GetRadius();
		KeeperBall.bHeldBySkater = Ball->IsHeldByOther(this);
		KeeperBall.TimeSinceImpulse = Ball->GetTimeSinceGameplayImpulse();
	}
	const FVector2D Target = ThrowTarget();
	const FSkateKeeperOutput Out = FSkateKeeper::Update(Tuning, Goal, KeeperBall, FSkateVec2(static_cast<float>(Target.X), static_cast<float>(Target.Y)),
		DeltaSeconds, State);

	if (Ball)
	{
		if (Out.Action != ESkateKeeperAction::None)
		{
			UE_LOG(LogIceSkate, Verbose, TEXT("Keeper team %d: %s, ball %.0f cm/s"), Team, ANSI_TO_TCHAR(SkateKeeperActionName(Out.Action)),
				static_cast<float>(Ball->GetBallVelocity().Size()));
		}
		switch (Out.Action)
		{
		case ESkateKeeperAction::Parry:
			Ball->ApplyGameplayVelocity(ToVector(Out.BallVelocity), ESkateImpulseKind::Save, this);
			break;
		case ESkateKeeperAction::Catch:
			Ball->SetHolder(this);
			break;
		case ESkateKeeperAction::Release:
			Ball->ClearHolder(this);
			Ball->ReleaseHold(ToVector(Out.BallPosition), ToVector(Out.BallVelocity), this);
			break;
		default:
			break;
		}
		if (Out.bHolding)
		{
			Ball->SetHolder(this);
			Ball->HoldAt(ToVector(Out.HoldPosition));
		}
	}
	ApplyPose(FSkateKeeper::Pose(Tuning, Goal, State), DeltaSeconds);
}

void ASkateGoalkeeper::ApplyPose(const FSkateKeeperPose& Pose, float DeltaSeconds)
{
	using namespace SkateGoalkeeperDetail;
	if (!bBuilt)
	{
		return;
	}
	const FSkateGoalFrame Goal = GoalFrame();
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Goal.Normal.Y, Goal.Normal.X));
	SetActorLocationAndRotation(FVector(Pose.Position.X, Pose.Position.Y, Goal.IceZ), FRotator(0.f, Yaw, 0.f));

	// Dive: hips go sideways and down, the body rolls over onto that side (roll < 0 tips +Z towards +Y).
	const float A = Pose.DiveAlpha;
	const float S = Pose.DiveSign;
	// Butterfly: hips drop, knees go out, the skates stay on the ice.
	Butterfly = DeltaSeconds > 0.f ? FMath::FInterpTo(Butterfly, Pose.ButterflyAlpha, DeltaSeconds, 10.f) : Pose.ButterflyAlpha;
	const float Drop = 34.f * Butterfly;
	BodyPivot->SetRelativeLocationAndRotation(FVector(0.f, S * A * 55.f, HipHeight - A * 60.f - Drop), FRotator(0.f, 0.f, -S * 80.f * A));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Mirror(1.f, Side == 0 ? -1.f : 1.f, 1.f);
		const FVector AnkleNow = (Ankle + FVector(4.f * Butterfly, 36.f * Butterfly, Drop)) * Mirror;
		SkateVisuals::SetSegment(Leg[Side], Hip * Mirror, AnkleNow, 15.f, 15.f);
		Skate[Side]->SetRelativeTransform(FTransform(FRotator(0.f, Mirror.Y * 35.f * Butterfly, 0.f), AnkleNow + FVector(6.f, 0.f, -5.f), FVector(30.f, 9.f, 9.f) / 100.f));
	}

	// Hands: ready position, reaching for the ball, stretched over the head in a dive, or holding the ball.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float Sgn = Side == 0 ? -1.f : 1.f;
		FVector Target(28.f, 48.f * Sgn, 22.f);
		if (Pose.bHolding)
		{
			Target = FVector(30.f, 9.f * Sgn, 15.f);
		}
		else if (A > 0.01f)
		{
			Target = FMath::Lerp(Target, FVector(15.f, 10.f * Sgn + 8.f * S, 120.f), A);
		}
		else if (FMath::Abs(Pose.ReachLateral) > 1.f && (FMath::Abs(Pose.ReachLateral) < 25.f || Sgn * Pose.ReachLateral > 0.f))
		{
			const float Lateral = FMath::Abs(Pose.ReachLateral) < 25.f ? Pose.ReachLateral + 9.f * Sgn : Pose.ReachLateral;
			Target = FVector(35.f, FMath::Clamp(Lateral, -75.f, 75.f), FMath::Clamp(Pose.ReachHeight - HipHeight, -70.f, 100.f));
		}
		const float Alpha = DeltaSeconds > 0.f ? 1.f - FMath::Exp(-18.f * DeltaSeconds) : 1.f;
		HandLocal[Side] = FMath::Lerp(HandLocal[Side], Target, Alpha);
		const FVector ShoulderLocal = Shoulder * FVector(1.f, Sgn, 1.f);
		SkateVisuals::SetSegment(Arm[Side], ShoulderLocal, HandLocal[Side], 11.f, 11.f);
		Glove[Side]->SetRelativeTransform(FTransform(FRotator::ZeroRotator, HandLocal[Side], FVector(0.17f)));
	}
}

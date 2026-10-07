#include "Skate/SkateBallControlComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Skate/SkateArena.h"
#include "Skate/SkateBall.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateFeedbackComponent.h"
#include "Skate/SkateMovementComponent.h"

namespace SkateBallControlComponentDetail
{
	// Only trace for walls when the ball is close enough to possibly be touched.
	constexpr float TraceRange = 300.f;
	// Trace starts this high above the ice (shin height), ends at the ball centre.
	constexpr float TraceStartHeight = 25.f;
	// Board probes around the skater: count and length (beyond the furthest carry point).
	constexpr int32 BoardProbeCount = 8;
	constexpr float BoardProbeRange = 150.f;

	FVector ToVector(const FSkateVec3& V) { return FVector(V.X, V.Y, V.Z); }
	FSkateVec3 ToSkate(const FVector& V) { return FSkateVec3(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z)); }
}

USkateBallControlComponent::USkateBallControlComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics; // after movement (prerequisite set by the character), before the ball's physics step
}

void USkateBallControlComponent::SetTuning(const FSkateTuning& InTuning)
{
	ControlTuning = InTuning.BallControl;
	BallPhysicsTuning = InTuning.BallPhysics;
	SkaterMaxSpeed = InTuning.Movement.MaxSpeed;
	if (ASkateBall* B = Ball.Get())
	{
		B->ApplyPhysicsTuning(BallPhysicsTuning);
	}
}

void USkateBallControlComponent::QueueActions(bool bPushPress, bool bPushRelease, bool bKickPress, bool bKickRelease)
{
	PendingActions.bPushPressed |= bPushPress;
	PendingActions.bPushReleased |= bPushRelease;
	PendingActions.bKickPressed |= bKickPress;
	PendingActions.bKickReleased |= bKickRelease;
}

void USkateBallControlComponent::CancelActions()
{
	FSkateBallControl::CancelActions(ControlState);
	PendingActions = FSkateBallActionInput();
}

void USkateBallControlComponent::ResetControl()
{
	FSkateBallControl::Reset(ControlState);
	if (ASkateBall* B = Ball.Get())
	{
		if (B->GetHolder() == this)
		{
			B->ClearHolder(this);
			B->SetCarried(false);
		}
	}
	PendingActions = FSkateBallActionInput();
	Report = FSkateContactReport();
	LastImpulse = FSkateBallImpulse();
	TimeSinceSwing = 100.f;
	LastSwingKind = ESkateImpulseKind::None;
}

float USkateBallControlComponent::GetPassCharge() const
{
	return FSkateBallControl::PassChargeFraction(ControlTuning, ControlState);
}

float USkateBallControlComponent::GetKickCharge() const
{
	return FSkateBallControl::ChargeFraction(ControlTuning, ControlState);
}

ASkateBall* USkateBallControlComponent::FindBall()
{
	if (!Ball.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<ASkateBall> It(World); It; ++It)
			{
				Ball = *It;
				It->ApplyPhysicsTuning(BallPhysicsTuning);
				break;
			}
		}
	}
	return Ball.Get();
}

bool USkateBallControlComponent::HasLineOfSight(const ASkateCharacter& Skater, const ASkateBall& InBall) const
{
	const FVector BallLoc = InBall.GetActorLocation();
	const FVector SkaterLoc = Skater.GetActorLocation();
	const FVector Start(SkaterLoc.X, SkaterLoc.Y, InBall.GetIceZ() + SkateBallControlComponentDetail::TraceStartHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SkateBallLineOfSight), false, &Skater);
	Params.AddIgnoredActor(&InBall);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Start, BallLoc, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

void USkateBallControlComponent::FindBoards(const ASkateCharacter& Skater, const ASkateBall& InBall, FSkateContactQuery& Query) const
{
	using namespace SkateBallControlComponentDetail;
	const FVector SkaterLoc = Skater.GetActorLocation();
	const FVector Origin(SkaterLoc.X, SkaterLoc.Y, InBall.GetIceZ() + InBall.GetRadius());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SkateBoardProbe), false, &Skater);
	Params.AddIgnoredActor(&InBall);
	for (int32 Index = 0; Index < BoardProbeCount; ++Index)
	{
		const FVector Dir = FVector::ForwardVector.RotateAngleAxis(360.f * Index / BoardProbeCount, FVector::UpVector);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, Origin, Origin + Dir * BoardProbeRange, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			continue;
		}
		const UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (!HitComponent || !HitComponent->ComponentHasTag(ASkateArena::BoardTag()))
		{
			continue;
		}
		const FSkateVec2 Normal = FSkateVec2(static_cast<float>(Hit.ImpactNormal.X), static_cast<float>(Hit.ImpactNormal.Y)).GetSafeNormal(FSkateVec2());
		if (Normal.SizeSquared() < 0.5f)
		{
			continue;
		}
		bool bKnown = false;
		for (int32 Wall = 0; Wall < Query.NumWalls; ++Wall)
		{
			bKnown |= Query.Walls[Wall].Normal.Dot(Normal) > 0.95f;
		}
		if (!bKnown)
		{
			Query.AddWall(FSkateVec2(static_cast<float>(Hit.ImpactPoint.X), static_cast<float>(Hit.ImpactPoint.Y)), Normal);
		}
	}
}

void USkateBallControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	using namespace SkateBallControlComponentDetail;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	if (!Skater || !Skater->GetSkateMovement() || DeltaTime <= 0.f)
	{
		return;
	}
	TimeSinceSwing += DeltaTime;

	const USkateMovementComponent* Move = Skater->GetSkateMovement();
	const FVector SkaterLoc = Skater->GetActorLocation();

	FSkateContactQuery Query;
	Query.SkaterPos = FSkateVec2(static_cast<float>(SkaterLoc.X), static_cast<float>(SkaterLoc.Y));
	Query.SkaterVel = FSkateVec2(static_cast<float>(Move->Velocity.X), static_cast<float>(Move->Velocity.Y));
	Query.Heading = Move->GetSkateState().Heading;
	Query.SkaterMaxSpeed = SkaterMaxSpeed;
	Query.StickDir = Skater->GetLastStick().Direction;
	Query.StickMag = Skater->GetLastStick().Magnitude;
	Query.bInteractionEnabled = bInteractionEnabled;

	ASkateBall* B = FindBall();
	if (B)
	{
		Query.bHasBall = true;
		Query.BallPos = ToSkate(B->GetActorLocation());
		Query.BallVel = ToSkate(B->GetBallVelocity());
		Query.BallRadius = B->GetRadius();
		Query.IceZ = B->GetIceZ();
		const float PlanarDist = (Query.BallPos.XY() - Query.SkaterPos).Size();
		Query.bLineOfSightClear = PlanarDist > TraceRange || HasLineOfSight(*Skater, *B);
		Query.bBallHeldByOther = B->IsHeldByOther(this);
		Query.BallTimeSinceImpulse = B->GetTimeSinceGameplayImpulse();
		Query.bIncomingPass = B->IsPassFor(this);
		if (PlanarDist <= TraceRange)
		{
			FindBoards(*Skater, *B, Query);
		}
	}

	const FSkateBallActionInput Actions = PendingActions;
	PendingActions = FSkateBallActionInput();

	const int32 TapsBefore = ControlState.Possession.TouchPulseCount;
	FSkateBallCarry Carry;
	const FSkateBallImpulse Impulse = FSkateBallControl::Update(ControlTuning, Query, Actions, DeltaTime, ControlState, Report, &Carry);

	// Possession: a carried ball gets its velocity steered (no damping / rolling resistance while carried);
	// any release hands it back to plain physics before an impulse is applied.
	if (B)
	{
		// Only the skater that has the ball touches its carried state: the teammate leaves it alone.
		const bool bCarryingNow = ControlState.Possession.bPossessed && !Impulse.IsValid();
		if (bCarryingNow)
		{
			B->SetHolder(this);
			B->SetCarried(true);
			if (Carry.bActive)
			{
				B->SetCarriedVelocity(ToVector(Carry.Velocity));
			}
		}
		else if (B->GetHolder() == this)
		{
			B->ClearHolder(this);
			B->SetCarried(false);
		}
	}
	if (ControlState.Possession.TouchPulseCount != TapsBefore)
	{
		// Dribble tap while carrying: the feet take turns, quiet tap sound.
		TimeSinceSwing = 0.f;
		LastSwingKind = ESkateImpulseKind::Touch;
		LastSwingPower = 0.f;
		LastSwingFoot = ControlState.Possession.TapFoot;
		if (USkateFeedbackComponent* Feedback = Skater->GetFeedback())
		{
			Feedback->OnDribbleTap();
		}
	}

	if (Impulse.IsValid() && B)
	{
		B->ApplyGameplayVelocity(ToVector(Impulse.NewBallVelocity), Impulse.Kind, this);
		LastImpulse = Impulse;
		if (Impulse.Kind != ESkateImpulseKind::BodyBlock)
		{
			TimeSinceSwing = 0.f;
			LastSwingKind = Impulse.Kind;
			LastSwingPower = Impulse.Power;
			LastSwingFoot = Impulse.Foot;
		}
		if (USkateFeedbackComponent* Feedback = Skater->GetFeedback())
		{
			Feedback->OnBallImpulse(Impulse, B->GetActorLocation());
		}
	}
	else if (ControlState.TimeSinceFail == 0.f)
	{
		// A buffered push/kick expired without reaching the ball: the leg still swings (honest whiff).
		TimeSinceSwing = 0.f;
		LastSwingKind = ControlState.LastFailedAction;
		LastSwingPower = ControlState.PendingKickPower;
		LastSwingFoot = ControlState.PlannedFoot;
	}

	if (Skater->IsDebugEnabled())
	{
		DrawDebug(*Skater, Query);
	}
}

void USkateBallControlComponent::DrawDebug(const ASkateCharacter& Skater, const FSkateContactQuery& Query) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Z = Query.IceZ + 3.f;
	const FVector Heading(Query.Heading.X, Query.Heading.Y, 0.f);
	const FVector Pos(Query.SkaterPos.X, Query.SkaterPos.Y, Z);
	const FVector ReachCentre = Pos + Heading * ControlTuning.ReachForward;
	const FVector XAxis(1.f, 0.f, 0.f);
	const FVector YAxis(0.f, 1.f, 0.f);

	FColor ZoneColor(120, 120, 120);
	if (Report.Reason == ESkateContactReason::Reachable)
	{
		ZoneColor = Report.bTouchAllowed ? FColor::Green : FColor(60, 200, 120);
	}
	else if (Report.Reason == ESkateContactReason::ActionReachOnly)
	{
		ZoneColor = FColor::Cyan;
	}
	DrawDebugCircle(World, ReachCentre, ControlTuning.ReachRadius, 32, ZoneColor, false, -1.f, 0, 2.f, XAxis, YAxis, false);
	DrawDebugCircle(World, ReachCentre, ControlTuning.ReachRadius + ControlTuning.ActionReachBonus, 32, ZoneColor, false, -1.f, 0, 0.5f, XAxis, YAxis, false);
	DrawDebugCircle(World, Pos, ControlTuning.BodyRadius, 24, FColor(200, 200, 255), false, -1.f, 0, 0.5f, XAxis, YAxis, false);
	// Reach angle limits.
	const float HalfAngle = FMath::DegreesToRadians(ControlTuning.ReachHalfAngle);
	const float ZoneOuter = ControlTuning.ReachForward + ControlTuning.ReachRadius + ControlTuning.ActionReachBonus;
	DrawDebugLine(World, Pos, Pos + Heading.RotateAngleAxis(FMath::RadiansToDegrees(HalfAngle), FVector::UpVector) * ZoneOuter, ZoneColor, false, -1.f, 0, 0.5f);
	DrawDebugLine(World, Pos, Pos + Heading.RotateAngleAxis(-FMath::RadiansToDegrees(HalfAngle), FVector::UpVector) * ZoneOuter, ZoneColor, false, -1.f, 0, 0.5f);

	if (Query.bHasBall)
	{
		const FVector BallPos(Query.BallPos.X, Query.BallPos.Y, Query.BallPos.Z);
		const FVector BallVel(Query.BallVel.X, Query.BallVel.Y, Query.BallVel.Z);
		DrawDebugDirectionalArrow(World, BallPos, BallPos + BallVel * 0.3f, 15.f, FColor::White, false, -1.f, 0, 1.5f);
		DrawDebugLine(World, Pos, FVector(BallPos.X, BallPos.Y, Z), ZoneColor, false, -1.f, 0, 0.5f);
		if (ControlState.Possession.bPossessed)
		{
			const FSkateVec2 Target = ControlState.Possession.CarryTarget;
			DrawDebugCircle(World, FVector(Target.X, Target.Y, Z), 8.f, 16, FColor::Yellow, false, -1.f, 0, 2.f, XAxis, YAxis, false);
			DrawDebugLine(World, FVector(Target.X, Target.Y, Z), FVector(BallPos.X, BallPos.Y, Z), FColor::Yellow, false, -1.f, 0, 1.f);
		}
		if (LastImpulse.IsValid() && ControlState.TimeSinceImpulse < 1.f)
		{
			const FVector Dv(LastImpulse.DeltaV.X, LastImpulse.DeltaV.Y, LastImpulse.DeltaV.Z);
			DrawDebugDirectionalArrow(World, BallPos, BallPos + Dv * 0.2f, 20.f, FColor::Magenta, false, -1.f, 0, 3.f);
		}
	}
}

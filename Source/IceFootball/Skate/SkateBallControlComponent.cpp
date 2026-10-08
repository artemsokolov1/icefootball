#include "Skate/SkateBallControlComponent.h"

#include "IceFootball.h"

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

void USkateBallControlComponent::QueueActions(bool bPushPress, bool bPushRelease, bool bKickPress, bool bKickRelease, bool bThroughPress)
{
	PendingActions.bThroughPressed |= bThroughPress;
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

void USkateBallControlComponent::KnockLoose()
{
	FSkateBallControl::ReleasePossession(ControlState, ESkatePossessionLoss::Taken);
	if (ASkateBall* B = Ball.Get())
	{
		if (B->GetHolder() == this)
		{
			B->ClearHolder(this);
			B->SetCarried(false);
		}
	}
}

bool USkateBallControlComponent::TryTake(float Range, float Protect, float BallSpeed)
{
	ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	ASkateBall* B = FindBall();
	if (!Skater || !B)
	{
		return false;
	}
	USkateBallControlComponent* Other = Cast<USkateBallControlComponent>(const_cast<UObject*>(B->GetHolder()));
	const ASkateCharacter* Carrier = Other ? Cast<ASkateCharacter>(Other->GetOwner()) : nullptr;
	if (!Carrier || Carrier->GetTeam() == Skater->GetTeam() || Other->GetControlState().Possession.TimeHeld < Protect
		|| FVector::Dist2D(B->GetActorLocation(), Skater->GetActorLocation()) > Range)
	{
		return false;
	}
	// Not from behind: the taker must be in front of or beside the carrier.
	const FVector ToTaker = (Skater->GetActorLocation() - Carrier->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(Carrier->GetActorForwardVector().GetSafeNormal2D(), ToTaker) < ControlTuning.Possession.TakeBehindDot)
	{
		return false;
	}
	Other->KnockLoose();
	// The ball goes to our feet: the normal trap picks it up next frame.
	const FVector Feet = Skater->GetActorLocation() + Skater->GetActorForwardVector().GetSafeNormal2D() * 45.f;
	const FVector Dir = (Feet - B->GetActorLocation()).GetSafeNormal2D();
	B->ApplyGameplayVelocity(Dir * BallSpeed, ESkateImpulseKind::Touch, this, Skater->GetTeam());
	UE_LOG(LogIceSkate, Verbose, TEXT("TAKE: team %d slot %d takes the ball from team %d slot %d"), Skater->GetTeam(), Skater->GetTeamSlot(),
		Carrier->GetTeam(), Carrier->GetTeamSlot());
	return true;
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
	Query.bStunned = Skater->IsStunned();

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
		if (Query.bBallHeldByOther)
		{
			// Without the take button an opponent's ball is only taken when it strayed from the carrier's feet.
			const USkateBallControlComponent* Other = Cast<USkateBallControlComponent>(B->GetHolder());
			const ASkateCharacter* Carrier = Other ? Cast<ASkateCharacter>(Other->GetOwner()) : nullptr;
			const FSkatePossessionTuning& PT = ControlTuning.Possession;
			Query.bStealAllowed = Carrier && Carrier->GetTeam() != Skater->GetTeam()
				&& Other->GetControlState().Possession.TimeHeld >= PT.StealProtectTime
				&& Other->GetControlState().Possession.CarryError > PT.StealLooseDistance;
		}
		Query.BallTimeSinceImpulse = B->GetTimeSinceGameplayImpulse();
		// A teammate's pass (or the own keeper's throw-out) is received firmer, from any side and from further out;
		// an opponent's pass is intercepted like any loose ball (in front, within the normal trap zone).
		Query.bIncomingPass = B->IsPassFor(this, Skater->GetTeam());
		Query.BallDamping = BallPhysicsTuning.LinearDamping;
		Query.BallRollingResistance = BallPhysicsTuning.RollingResistance;
		if (!CachedArena.IsValid())
		{
			CachedArena = ASkateArena::Find(GetWorld());
		}
		if (const ASkateArena* Arena = CachedArena.Get())
		{
			// Shots with an idle stick go at the goal this team attacks (team 0: goal 0).
			Query.bShotTargetValid = true;
			Query.ShotTargetPos = Arena->GetGoalFrame(Skater->GetTeam() == 0 ? 0 : 1).Center;
		}
		Query.bThroughTargetValid = bThroughTargetValid;
		Query.ThroughTargetPos = FSkateVec2(static_cast<float>(ThroughTarget.X), static_cast<float>(ThroughTarget.Y));
		// A pass goes to the teammate the stick points at (within PassAssistAngle), to the nearest one with an idle
		// stick, and where the stick points when no teammate is that way (off the boards, into space).
		const bool bAimed = Query.StickMag > 0.3f;
		float BestScore = bAimed ? -FMath::Cos(FMath::DegreesToRadians(ControlTuning.PassAssistAngle)) : TNumericLimits<float>::Max();
		PassMate = nullptr;
		for (TActorIterator<ASkateCharacter> It(GetWorld()); It; ++It)
		{
			if (*It == Skater || It->GetTeam() != Skater->GetTeam())
			{
				continue;
			}
			const FVector2D ToMate(It->GetActorLocation() - SkaterLoc);
			const float Score = bAimed ? -static_cast<float>(ToMate.GetSafeNormal() | FVector2D(Query.StickDir.X, Query.StickDir.Y)) : static_cast<float>(ToMate.Size());
			if (Score < BestScore)
			{
				BestScore = Score;
				PassMate = *It;
			}
		}
		if (const ASkateCharacter* Mate = PassMate.Get())
		{
			Query.bPassTargetValid = true;
			Query.PassTargetPos = FSkateVec2(static_cast<float>(Mate->GetActorLocation().X), static_cast<float>(Mate->GetActorLocation().Y));
			Query.PassTargetVel = FSkateVec2(static_cast<float>(Mate->GetVelocity().X), static_cast<float>(Mate->GetVelocity().Y));
		}
		if (PlanarDist <= TraceRange)
		{
			FindBoards(*Skater, *B, Query);
		}
	}

	const FSkateBallActionInput Actions = PendingActions;
	PendingActions = FSkateBallActionInput();

	const int32 TapsBefore = ControlState.Possession.TouchPulseCount;
	const int32 AcquiresBefore = ControlState.Possession.AcquireCount;
	const bool bPossessedBefore = ControlState.Possession.bPossessed;
	FSkateBallCarry Carry;
	const FSkateBallImpulse Impulse = FSkateBallControl::Update(ControlTuning, Query, Actions, DeltaTime, ControlState, Report, &Carry);

	if (bPossessedBefore != ControlState.Possession.bPossessed)
	{
		UE_LOG(LogIceSkate, Verbose, TEXT("Team %d slot %d: %s (loss %s, carry error %.0f cm, speed %.0f, rel %.0f, ball h %.0f, ball %.0f cm/s %.2f s after the last impulse%s)"),
			Skater->GetTeam(), Skater->GetTeamSlot(), ControlState.Possession.bPossessed ? TEXT("TRAP") : TEXT("LOST"),
			ANSI_TO_TCHAR(SkatePossessionLossName(ControlState.Possession.LastLoss)), ControlState.Possession.CarryError,
			static_cast<float>(Move->Velocity.Size2D()), Report.RelativeSpeed, Report.BallHeight, B ? static_cast<float>(B->GetBallVelocity().Size2D()) : 0.f,
			B ? B->GetTimeSinceGameplayImpulse() : 0.f, Query.bIncomingPass ? TEXT(", a pass") : TEXT(""));
	}

	// Possession: a carried ball gets its velocity steered (no damping / rolling resistance while carried);
	// any release hands it back to plain physics before an impulse is applied.
	if (B)
	{
		// Only the skater that has the ball touches its carried state: the teammate leaves it alone.
		// A steal claims the ball on the frame it is trapped. The robbed skater sees the holder change and
		// does not reclaim it: next frame its query reports the ball as held by another -> loss "Taken".
		const bool bJustTrapped = ControlState.Possession.AcquireCount != AcquiresBefore;
		const bool bCarryingNow = ControlState.Possession.bPossessed && !Impulse.IsValid() && (bJustTrapped || !B->IsHeldByOther(this));
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
		if (Impulse.Kind == ESkateImpulseKind::Kick || Impulse.Kind == ESkateImpulseKind::Push)
		{
			UE_LOG(LogIceSkate, Verbose, TEXT("Team %d slot %d: %s power %.2f -> %.0f cm/s at (%.0f, %.0f), teammate %.0f cm away"), Skater->GetTeam(), Skater->GetTeamSlot(),
				ANSI_TO_TCHAR(SkateImpulseKindName(Impulse.Kind)), Impulse.Power, Impulse.NewBallVelocity.Size(), SkaterLoc.X, SkaterLoc.Y,
				Query.bPassTargetValid ? (Query.PassTargetPos - Query.BallPos.XY()).Size() : 0.f);
		}
		const ASkateCharacter* Mate = Impulse.Kind == ESkateImpulseKind::Push && Query.bPassTargetValid ? PassMate.Get() : nullptr;
		B->ApplyGameplayVelocity(ToVector(Impulse.NewBallVelocity), Impulse.Kind, this, Skater->GetTeam(), Mate ? Mate->GetBallControl() : nullptr);
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

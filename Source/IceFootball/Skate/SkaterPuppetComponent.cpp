#include "Skate/SkaterPuppetComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Skate/SkateBallControlComponent.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateMovementComponent.h"
#include "Skate/SkateVisuals.h"

namespace SkatePuppetDetail
{
	const FLinearColor Jersey(0.85f, 0.18f, 0.08f);
	const FLinearColor JerseyDark(0.62f, 0.12f, 0.05f);
	const FLinearColor Pants(0.06f, 0.08f, 0.22f);
	const FLinearColor Skin(0.92f, 0.72f, 0.58f);
	const FLinearColor Gloves(0.04f, 0.04f, 0.05f);
	const FLinearColor Boots(0.03f, 0.03f, 0.03f);
	const FLinearColor Steel(0.75f, 0.78f, 0.82f);

	FVector ToVector(const FSkateVec3& V) { return FVector(V.X, V.Y, V.Z); }

	// Box placed between two points, oriented by Forward (X) and the segment (Z).
	void SetBox(UStaticMeshComponent* Part, const FVector& Center, const FVector& Forward, const FVector& Up, const FVector& Size)
	{
		Part->SetRelativeTransform(FTransform(FRotationMatrix::MakeFromZX(Up, Forward).Rotator(), Center, Size / 100.0));
	}
}

USkaterPuppetComponent::USkaterPuppetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after movement and ball contact of this frame
}

void USkaterPuppetComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildParts();
	ResetPose();
}

void USkaterPuppetComponent::BuildParts()
{
	using namespace SkatePuppetDetail;
	if (bBuilt)
	{
		return;
	}
	AActor* Owner = GetOwner();
	auto Add = [this, Owner](const TCHAR* Shape, const FLinearColor& Color)
	{
		UStaticMeshComponent* Part = SkateVisuals::AddPart(Owner, this, Shape, Color, true);
		Parts.Add(Part);
		return Part;
	};
	PelvisPart = Add(TEXT("Cube"), Pants);
	ChestPart = Add(TEXT("Cube"), Jersey);
	ChestMarkPart = Add(TEXT("Cube"), MarkColor);
	NeckPart = Add(TEXT("Cylinder"), Skin);
	HeadPart = Add(TEXT("Sphere"), Skin);
	VisorPart = Add(TEXT("Cube"), Boots);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		ShoulderPart[Side] = Add(TEXT("Sphere"), Jersey);
		UpperArmPart[Side] = Add(TEXT("Cylinder"), Jersey);
		ForearmPart[Side] = Add(TEXT("Cylinder"), JerseyDark);
		HandPart[Side] = Add(TEXT("Sphere"), Gloves);
		ThighPart[Side] = Add(TEXT("Cylinder"), Pants);
		ShinPart[Side] = Add(TEXT("Cylinder"), Pants);
		BootPart[Side] = Add(TEXT("Cube"), Boots);
		BladePart[Side] = Add(TEXT("Cube"), Steel);
	}
	bBuilt = true;
}

void USkaterPuppetComponent::ResetPose()
{
	PoseState = FSkatePoseState();
	Pose = FSkatePose();
}

FTransform USkaterPuppetComponent::GetBladeWorldTransform(int32 Side) const
{
	const int32 S = Side == 0 ? 0 : 1;
	const FTransform Local(FRotator(0.f, Pose.FootYawDeg[S], 0.f), SkatePuppetDetail::ToVector(Pose.BladeContact[S]));
	return Local * GetComponentTransform();
}

void USkaterPuppetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBuilt || DeltaTime <= 0.f)
	{
		return;
	}
	const ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwner());
	const USkateMovementComponent* Move = Skater ? Skater->GetSkateMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	const FSkateMoveState& State = Move->GetSkateState();
	const FSkateMovementTuning& MoveTuning = Move->GetMovementTuning();
	const FVector Vel = Move->Velocity;

	FSkatePoseInput Input;
	Input.SpeedRatio = static_cast<float>(Vel.Size2D()) / FMath::Max(MoveTuning.MaxSpeed, 1.f);
	Input.PushAmount = State.PushAmount;
	Input.ThrustAccel = State.ThrustAccel;
	Input.BrakeAmount = FMath::Clamp(State.BrakeDecel / FMath::Max(MoveTuning.BrakeDecel, 1.f), 0.f, 1.f);
	Input.SkidAmount = FMath::Clamp(State.ScrubDecel / 900.f, 0.f, 1.f);
	Input.LateralAccel = State.LateralAccel;
	Input.SlideSide = static_cast<float>(State.Heading.X * Vel.Y - State.Heading.Y * Vel.X) >= 0.f ? 1.f : -1.f;
	if (const USkateBallControlComponent* Ball = Skater->GetBallControl())
	{
		// A charged pass winds the leg up too, a little less than a shot.
		Input.KickCharge = FMath::Max(Ball->GetKickCharge(), 0.6f * Ball->GetPassCharge());
		Input.ChargeFoot = Ball->GetPlannedFoot();
		Input.SwingKind = Ball->GetLastSwingKind();
		Input.SwingFoot = Ball->GetLastSwingFoot();
		Input.SwingTime = Ball->GetTimeSinceActionSwing();
		Input.SwingPower = Ball->GetLastSwingPower();
	}

	Pose = FSkatePoseSolver::Update(Tuning, Input, DeltaTime, PoseState);
	PoseLabel = UTF8_TO_TCHAR(Pose.Label);
	ApplyPose();
}

void USkaterPuppetComponent::ApplyPose()
{
	using namespace SkatePuppetDetail;
	const FVector Up = ToVector(Pose.TorsoUp);
	const FVector Fwd = ToVector(Pose.TorsoForward);
	const FVector Pelvis = ToVector(Pose.Pelvis);
	const FVector Chest = ToVector(Pose.Chest);

	// Torso: hips box + chest box (wider at the shoulders) + number patch on the front.
	SetBox(PelvisPart, Pelvis - Up * 2.0, FVector::ForwardVector.RotateAngleAxis(Pose.PelvisYawDeg, FVector::UpVector), FVector::UpVector,
		FVector(20.0, 30.0, 18.0));
	SetBox(ChestPart, (Pelvis + Chest) * 0.5 + Up * 3.0, Fwd, Up,
		FVector(SkateBody::ChestDepth, SkateBody::ChestWidth, SkateBody::TorsoLength + 2.0));
	SetBox(ChestMarkPart, Pelvis + Up * (SkateBody::TorsoLength * 0.62) + Fwd * (SkateBody::ChestDepth * 0.5 + 0.6), Fwd, Up, FVector(1.0, 14.0, 12.0));

	// Neck + head with a dark visor showing where the skater faces.
	const FVector Head = ToVector(Pose.Head);
	SkateVisuals::SetSegment(NeckPart, Chest, Head - (Head - Chest).GetSafeNormal() * (SkateBody::HeadRadius * 0.8), 10.f, 10.f);
	HeadPart->SetRelativeTransform(FTransform(FRotator::ZeroRotator, Head, FVector(SkateBody::HeadRadius * 2.0 / 100.0)));
	SetBox(VisorPart, Head + Fwd * (SkateBody::HeadRadius * 0.85) + Up * 1.5, Fwd, Up, FVector(4.0, 16.0, 6.0));

	for (int32 Side = 0; Side < 2; ++Side)
	{
		// Arms: shoulder ball, upper arm, forearm, glove.
		const FVector Shoulder = ToVector(Pose.Shoulder[Side]);
		const FVector Elbow = ToVector(Pose.Elbow[Side]);
		const FVector Hand = ToVector(Pose.Hand[Side]);
		ShoulderPart[Side]->SetRelativeTransform(FTransform(FRotator::ZeroRotator, Shoulder, FVector(0.14)));
		SkateVisuals::SetSegment(UpperArmPart[Side], Shoulder, Elbow, SkateBody::UpperArmThickness, SkateBody::UpperArmThickness);
		SkateVisuals::SetSegment(ForearmPart[Side], Elbow, Hand, SkateBody::ForearmThickness, SkateBody::ForearmThickness);
		HandPart[Side]->SetRelativeTransform(FTransform(FRotator::ZeroRotator, Hand, FVector(SkateBody::HandSize / 100.0)));

		// Legs.
		const FVector Hip = ToVector(Pose.Hip[Side]);
		const FVector Knee = ToVector(Pose.Knee[Side]);
		const FVector Ankle = ToVector(Pose.Ankle[Side]);
		SkateVisuals::SetSegment(ThighPart[Side], Hip, Knee, SkateBody::ThighThickness, SkateBody::ThighThickness);
		SkateVisuals::SetSegment(ShinPart[Side], Knee, Ankle, SkateBody::ShinThickness, SkateBody::ShinThickness);

		// Skate: boot + blade, edged into the turn with the lean.
		const FRotator FootRot(0.f, Pose.FootYawDeg[Side], Pose.EdgeRollDeg);
		BootPart[Side]->SetRelativeTransform(FTransform(FootRot, Ankle + FootRot.RotateVector(FVector(4.0, 0.0, -1.0)), FVector(26.0, 9.5, 10.0) / 100.0));
		BladePart[Side]->SetRelativeTransform(FTransform(FootRot, Ankle + FootRot.RotateVector(FVector(2.0, 0.0, -7.5)), FVector(32.0, 1.5, 5.0) / 100.0));
	}
}

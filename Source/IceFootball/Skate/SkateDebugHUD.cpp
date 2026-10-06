#include "Skate/SkateDebugHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Skate/Core/SkateTuningPresets.h"
#include "Skate/SkateArena.h"
#include "Skate/SkateBall.h"
#include "Skate/SkateBallControlComponent.h"
#include "Skate/SkateCameraRig.h"
#include "Skate/SkateCharacter.h"
#include "Skate/SkateMovementComponent.h"
#include "Skate/SkatePlayerController.h"
#include "Skate/SkaterPuppetComponent.h"

namespace SkateHudDetail
{
	constexpr float LineHeight = 17.f;
	constexpr float TextScale = 1.15f;
	constexpr double FaultHoldTime = 5.0;
	constexpr double GraphSeconds = 4.0;

	const FLinearColor Info(0.92f, 0.92f, 0.92f);
	const FLinearColor Dim(0.6f, 0.6f, 0.6f);
	const FLinearColor Header(1.0f, 0.85f, 0.35f);
	const FLinearColor Good(0.35f, 1.0f, 0.45f);
	const FLinearColor Note(1.0f, 0.65f, 0.25f);
	const FLinearColor Fault(1.0f, 0.15f, 0.12f);

	float YawDeg(const FSkateVec2& V) { return V.SizeSquared() > 0.f ? V.Yaw() * SkateMath::RadToDeg : 0.f; }
}

float ASkateDebugHUD::Line(const FString& Text, const FLinearColor& Color)
{
	DrawText(Text, Color, CursorX, CursorY, GEngine->GetSmallFont(), SkateHudDetail::TextScale);
	CursorY += SkateHudDetail::LineHeight;
	return CursorY;
}

void ASkateDebugHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	ASkateCharacter* Skater = Cast<ASkateCharacter>(GetOwningPawn());
	ASkateArena* Arena = ASkateArena::Find(GetWorld());
	if (!Skater)
	{
		return;
	}
	UpdateFaults(Skater, Arena);
	DrawAlwaysOn(Skater, Arena);
	if (Skater->IsDebugEnabled())
	{
		DrawDebugPanel(Skater, Arena);
	}
}

void ASkateDebugHUD::UpdateFaults(ASkateCharacter* Skater, ASkateArena* Arena)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const float Dt = GetWorld()->GetDeltaSeconds();
	if (Dt > 0.f)
	{
		SmoothedFps = FMath::Lerp(SmoothedFps, 1.0 / Dt, 0.1);
	}
	if (Arena)
	{
		if (!Arena->IsInsideRink(Skater->GetActorLocation(), 10.f))
		{
			SkaterOutTime = Now;
		}
		if (Arena->GetBall() && !Arena->IsInsideRink(Arena->GetBall()->GetActorLocation(), 10.f))
		{
			BallOutTime = Now;
		}
	}
	if (Skater->GetSkateMovement()->HadBrakeFaultThisFrame())
	{
		BrakeFaultTime = Now;
	}

	SpeedHistory.Add(TPair<double, float>(Now, static_cast<float>(Skater->GetVelocity().Size2D())));
	int32 FirstValid = 0;
	while (FirstValid < SpeedHistory.Num() && Now - SpeedHistory[FirstValid].Key > SkateHudDetail::GraphSeconds)
	{
		++FirstValid;
	}
	if (FirstValid > 0)
	{
		SpeedHistory.RemoveAt(0, FirstValid);
	}
}

void ASkateDebugHUD::DrawAlwaysOn(ASkateCharacter* Skater, ASkateArena* Arena)
{
	using namespace SkateHudDetail;
	const ASkatePlayerController* Pc = Cast<ASkatePlayerController>(PlayerOwner);
	const ASkateCameraRig* Rig = Pc ? Pc->GetCameraRig() : nullptr;
	const int32 Cap = Pc ? Pc->GetFpsCap() : 0;

	const FString Status = FString::Printf(TEXT("Preset %s  |  Ball interaction %s  |  Camera %s  |  FPS cap %s  |  Debug: View / F1"),
		ANSI_TO_TCHAR(SkateTuningPresets::Name(Skater->GetPreset())),
		Skater->IsBallInteractionEnabled() ? TEXT("ON") : TEXT("OFF (skating only)"),
		Rig && Rig->IsStaticMode() ? TEXT("STATIC") : TEXT("follow"),
		Cap > 0 ? *FString::FromInt(Cap) : TEXT("off"));
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f, 0.f, Canvas->ClipX, 24.f);
	DrawText(Status, Info, 12.f, 4.f, GEngine->GetSmallFont(), TextScale);

	// Kick charge bar (gameplay feedback, not debug).
	if (const USkateBallControlComponent* Ball = Skater->GetBallControl())
	{
		if (Ball->IsChargingKick())
		{
			const float W = 260.f;
			const float X = Canvas->ClipX * 0.5f - W * 0.5f;
			const float Y = Canvas->ClipY - 70.f;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 2.f, Y - 2.f, W + 4.f, 18.f);
			DrawRect(FLinearColor(1.f, 0.55f, 0.1f, 0.9f), X, Y, W * Ball->GetKickCharge(), 14.f);
			DrawText(TEXT("KICK"), Info, X, Y - 18.f, GEngine->GetSmallFont(), TextScale);
		}
	}

	// Goal flash.
	if (Arena && GetWorld()->GetTimeSeconds() - Arena->GetLastGoalTime() < 1.6)
	{
		const FString Text = FString::Printf(TEXT("GOAL!  (%d)"), Arena->GetGoals());
		DrawText(Text, Good, Canvas->ClipX * 0.5f - 90.f, Canvas->ClipY * 0.22f, GEngine->GetLargeFont(), 1.6f);
	}
}

void ASkateDebugHUD::DrawStickWidget(ASkateCharacter* Skater, float X, float Y, float Radius)
{
	const FSkateInputTuning& InputTuning = Skater->GetActiveTuning().Input;
	auto Circle = [this, X, Y](float R, const FLinearColor& Color)
	{
		constexpr int32 Segments = 32;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float A0 = 2.f * PI * Index / Segments;
			const float A1 = 2.f * PI * (Index + 1) / Segments;
			DrawLine(X + R * FMath::Cos(A0), Y + R * FMath::Sin(A0), X + R * FMath::Cos(A1), Y + R * FMath::Sin(A1), Color, 1.f);
		}
	};
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.4f), X - Radius - 6.f, Y - Radius - 6.f, Radius * 2.f + 12.f, Radius * 2.f + 12.f);
	Circle(Radius, SkateHudDetail::Dim);
	Circle(Radius * InputTuning.StickDeadZoneInner, SkateHudDetail::Note);   // radial dead zone
	Circle(Radius * InputTuning.StickDeadZoneOuter, SkateHudDetail::Dim);

	// Raw deflection (screen: up = stick up).
	const FVector2D Raw = Skater->GetLastFrameInput().RawStick;
	const float Rx = X + static_cast<float>(Raw.X) * Radius;
	const float Ry = Y - static_cast<float>(Raw.Y) * Radius;
	DrawRect(FLinearColor::White, Rx - 3.f, Ry - 3.f, 6.f, 6.f);

	// Shaped magnitude along the raw direction (what the movement receives).
	const FSkateStickResult& Shaped = Skater->GetLastStick();
	const float RawMag = static_cast<float>(Raw.Size());
	if (RawMag > 0.f && Shaped.Magnitude > 0.f)
	{
		const float Sx = X + static_cast<float>(Raw.X) / RawMag * Shaped.Magnitude * Radius;
		const float Sy = Y - static_cast<float>(Raw.Y) / RawMag * Shaped.Magnitude * Radius;
		DrawLine(X, Y, Sx, Sy, SkateHudDetail::Good, 2.f);
	}
	// Triggers.
	DrawRect(FLinearColor(0.3f, 0.6f, 1.f, 0.9f), X - Radius, Y + Radius + 10.f, Radius * Skater->GetLastBrake(), 6.f);
	DrawRect(FLinearColor(1.f, 0.6f, 0.2f, 0.9f), X, Y + Radius + 10.f, Radius * Skater->GetLastBoost(), 6.f);
}

void ASkateDebugHUD::DrawSpeedGraph(ASkateCharacter* Skater, float X, float Y, float W, float H)
{
	const FSkateMovementTuning& Move = Skater->GetActiveTuning().Movement;
	const float Top = Move.BoostMaxSpeed * 1.1f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X, Y, W, H);
	const float MaxLineY = Y + H - H * Move.MaxSpeed / Top;
	const float BoostLineY = Y + H - H * Move.BoostMaxSpeed / Top;
	DrawLine(X, MaxLineY, X + W, MaxLineY, SkateHudDetail::Dim, 1.f);
	DrawLine(X, BoostLineY, X + W, BoostLineY, SkateHudDetail::Note, 1.f);
	DrawText(TEXT("speed, last 4 s (grey = max, orange = boost max)"), SkateHudDetail::Dim, X + 4.f, Y + 2.f, GEngine->GetSmallFont(), 1.f);
	if (SpeedHistory.Num() < 2)
	{
		return;
	}
	const double Now = SpeedHistory.Last().Key;
	for (int32 Index = 1; Index < SpeedHistory.Num(); ++Index)
	{
		const float X0 = X + W * (1.f - static_cast<float>((Now - SpeedHistory[Index - 1].Key) / SkateHudDetail::GraphSeconds));
		const float X1 = X + W * (1.f - static_cast<float>((Now - SpeedHistory[Index].Key) / SkateHudDetail::GraphSeconds));
		const float Y0 = Y + H - H * FMath::Min(SpeedHistory[Index - 1].Value / Top, 1.f);
		const float Y1 = Y + H - H * FMath::Min(SpeedHistory[Index].Value / Top, 1.f);
		DrawLine(X0, Y0, X1, Y1, SkateHudDetail::Good, 1.5f);
	}
}

void ASkateDebugHUD::DrawDebugPanel(ASkateCharacter* Skater, ASkateArena* Arena)
{
	using namespace SkateHudDetail;
	const USkateMovementComponent* Move = Skater->GetSkateMovement();
	const FSkateMoveState& State = Move->GetSkateState();
	const FSkateStickResult& Stick = Skater->GetLastStick();
	const FSkateFrameInput& Raw = Skater->GetLastFrameInput();
	const USkateBallControlComponent* BallControl = Skater->GetBallControl();
	const USkaterPuppetComponent* Puppet = Skater->GetPuppet();
	const double Now = GetWorld()->GetTimeSeconds();

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 6.f, 30.f, 560.f, 590.f);
	CursorX = 14.f;
	CursorY = 36.f;

	// ---- Movement ----
	const float Speed = State.Velocity.Size();
	Line(TEXT("MOVEMENT"), Header);
	Line(FString::Printf(TEXT("Speed %4.0f cm/s (%4.1f km/h)   target %4.0f   state: %s"),
		Speed, Speed * 0.036f, State.TargetSpeed, ANSI_TO_TCHAR(SkatePhaseName(State.Phase))), Info);
	Line(FString::Printf(TEXT("Stick raw (%+.2f, %+.2f) |%.2f|  ->  shaped %.2f   [%s]"),
		Raw.RawStick.X, Raw.RawStick.Y, Raw.RawStick.Size(), Stick.Magnitude, Raw.bFromKeyboard ? TEXT("keyboard") : TEXT("gamepad")), Info);
	Line(FString::Printf(TEXT("LT raw %.2f -> brake %.2f    RT raw %.2f -> boost %.2f"), Raw.BrakeRaw, Skater->GetLastBrake(), Raw.BoostRaw, Skater->GetLastBoost()), Info);
	Line(FString::Printf(TEXT("Direction (deg): velocity %+6.1f  blades %+6.1f  desired %s   slip %.1f"),
		YawDeg(State.Velocity), YawDeg(State.Heading),
		Stick.Magnitude > 0.f ? *FString::Printf(TEXT("%+6.1f"), YawDeg(Stick.Direction)) : TEXT("  --  "), State.SlipAngleDeg), Info);
	Line(FString::Printf(TEXT("Accel cm/s2: thrust %4.0f  brake %4.0f  skid %4.0f  lateral %+5.0f   turn limit %3.0f deg/s"),
		State.ThrustAccel, State.BrakeDecel, State.ScrubDecel, State.LateralAccel, State.TurnRateLimitDeg), Info);
	if (Puppet)
	{
		Line(FString::Printf(TEXT("Pose: %s   lean %+.1f deg   blade twist %+.1f deg"), Puppet->GetPoseLabel(), Puppet->GetLeanDeg(), Puppet->GetBrakeTwistDeg()), Info);
	}

	// ---- Ball ----
	CursorY += 6.f;
	Line(TEXT("BALL"), Header);
	if (BallControl)
	{
		const FSkateContactReport& R = BallControl->GetReport();
		const FSkateBallControlState& C = BallControl->GetControlState();
		Line(FString::Printf(TEXT("Distance %4.0f cm (to reach zone centre %4.0f)  angle %3.0f deg  height %4.1f cm"),
			R.Distance, R.DistanceToReachCentre, R.AngleFromHeadingDeg, R.BallHeight), Info);
		Line(FString::Printf(TEXT("Relative speed %4.0f cm/s   closing %+5.0f cm/s"), R.RelativeSpeed, R.ClosingSpeed), Info);

		const bool bReach = R.Reason == ESkateContactReason::Reachable || R.Reason == ESkateContactReason::ActionReachOnly;
		FString Verdict = FString::Printf(TEXT("Contact: %s"), ANSI_TO_TCHAR(SkateContactReasonName(R.Reason)));
		if (R.Reason == ESkateContactReason::Reachable)
		{
			Verdict += R.bTouchAllowed ? (R.bOnCooldown ? TEXT(" - touch on cooldown") : TEXT(" - touching")) :
				(!R.bHasDribbleIntent ? TEXT(" - no dribble intent (stick/speed)") : TEXT(" - not closing in"));
		}
		Line(Verdict, bReach ? Good : Dim);

		const FSkateBallImpulse& Last = BallControl->GetLastImpulse();
		if (Last.IsValid())
		{
			Line(FString::Printf(TEXT("Last impulse: %s  dv %4.0f cm/s  -> ball %4.0f cm/s  %.2f s ago   (total %d)"),
				ANSI_TO_TCHAR(SkateImpulseKindName(Last.Kind)), Last.DeltaV.Size(), Last.NewBallVelocity.Size(), C.TimeSinceImpulse, C.ImpulseCount), Info);
		}
		else
		{
			Line(TEXT("Last impulse: none"), Dim);
		}
		Line(FString::Printf(TEXT("Kick charge %.2f   kick buffer %s   push buffer %s"), BallControl->GetKickCharge(),
			C.KickBuffer >= 0.f ? *FString::Printf(TEXT("%.2fs"), C.KickBuffer) : TEXT("-"),
			C.PushBuffer >= 0.f ? *FString::Printf(TEXT("%.2fs"), C.PushBuffer) : TEXT("-")), Info);
		if (C.LastFailedAction != ESkateImpulseKind::None && C.TimeSinceFail < 3.f)
		{
			Line(FString::Printf(TEXT("Whiff: %s not executed - %s (%.1f s ago)"), ANSI_TO_TCHAR(SkateImpulseKindName(C.LastFailedAction)),
				ANSI_TO_TCHAR(SkateContactReasonName(C.LastFailReason)), C.TimeSinceFail), Note);
		}
	}

	// ---- Frame ----
	CursorY += 6.f;
	Line(TEXT("FRAME"), Header);
	const float Dt = GetWorld()->GetDeltaSeconds();
	Line(FString::Printf(TEXT("FPS %.0f   frame %.1f ms"), SmoothedFps, Dt * 1000.f), Dt > 0.05f ? Note : Info);
	if (Dt > 0.05f)
	{
		Line(TEXT("Hitch: frame > 50 ms (simulation sub-steps keep results stable)"), Note);
	}

	// ---- Faults: concrete conditions only ----
	CursorY += 6.f;
	Line(TEXT("FAULT CHECKS (red = violated rule, shown 5 s)"), Header);
	bool bAnyFault = false;
	if (ASkateBall* Ball = BallControl ? BallControl->GetBall() : nullptr)
	{
		if (Ball->GetDoubleImpulseFaults() > 0 && Now - Ball->GetLastFaultTime() < FaultHoldTime)
		{
			Line(FString::Printf(TEXT("DOUBLE IMPULSE: 2 impulses %.3f s apart (< %.3f s), total %d"),
				Ball->GetLastDoubleImpulseGap(), ASkateBall::DoubleImpulseWindow, Ball->GetDoubleImpulseFaults()), Fault);
			bAnyFault = true;
		}
		if (Ball->GetPawnContactFaults() > 0 && Now - Ball->GetLastFaultTime() < FaultHoldTime)
		{
			Line(FString::Printf(TEXT("BALL-PAWN PHYSICS CONTACT: %d hit(s) (ball must ignore the Pawn channel)"), Ball->GetPawnContactFaults()), Fault);
			bAnyFault = true;
		}
	}
	if (Now - BallOutTime < FaultHoldTime)
	{
		Line(TEXT("BALL OUTSIDE RINK: ball centre beyond the boards (tunnelling)"), Fault);
		bAnyFault = true;
	}
	if (Now - SkaterOutTime < FaultHoldTime)
	{
		Line(TEXT("SKATER OUTSIDE RINK: capsule beyond the boards"), Fault);
		bAnyFault = true;
	}
	if (Now - BrakeFaultTime < FaultHoldTime)
	{
		Line(TEXT("BRAKE REVERSAL: braking flipped the velocity direction"), Fault);
		bAnyFault = true;
	}
	if (!bAnyFault)
	{
		Line(TEXT("none"), Good);
	}

	// ---- Widgets ----
	DrawStickWidget(Skater, 640.f, 110.f, 60.f);
	DrawSpeedGraph(Skater, 580.f, 210.f, 360.f, 120.f);

	// ---- Legend ----
	CursorX = 14.f;
	CursorY = Canvas->ClipY - 7.f * LineHeight - 8.f;
	Line(TEXT("World: green = velocity, blue = blades, yellow = stick, orange = lateral accel,"), Dim);
	Line(TEXT("       reach zone green/cyan = touch/A-X allowed, grey = not reachable, magenta = last impulse"), Dim);
	Line(TEXT("Pad: LS move | LT brake | RT boost | A push | X hold/release kick | Y reset | RB ball to feet"), Dim);
	Line(TEXT("     View debug | D-pad Up camera | D-pad L/R preset | D-pad Down FPS cap | Menu ball on/off"), Dim);
	Line(TEXT("Keys: WASD (+LAlt half) | Space brake | LShift boost | J push | K kick | R reset | T ball"), Dim);
	Line(TEXT("      F1 debug | F2 camera | 1/2/3 preset | F3 FPS cap | F4 ball on/off"), Dim);
}

#include "SkateCoreTests.h"

#include "../Core/SkateBallControl.h"
#include "../Core/SkateInput.h"
#include "../Core/SkateModel.h"
#include "../Core/SkateTuningPresets.h"

#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <functional>

namespace SkateCoreTestsDetail
{
	using FInputFn = std::function<FSkateMoveInput(float /*Time*/, const FSkateMoveState&)>;

	std::string Fmt(const char* Format, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Format);
		std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);
		return std::string(Buffer);
	}

	const ESkatePreset AllPresets[] = { ESkatePreset::Responsive, ESkatePreset::Balanced, ESkatePreset::Inertial };
	const float FrameRates[] = { 30.f, 60.f, 120.f };

	// Deterministic frame-time jitter (45..144 FPS) to mimic an uneven frame rate.
	struct FJitter
	{
		unsigned State = 12345u;
		float Next()
		{
			State = State * 1664525u + 1013904223u;
			const float U = static_cast<float>((State >> 8) & 0xFFFF) / 65535.f;
			return 1.f / SkateMath::Lerp(45.f, 144.f, U);
		}
	};

	struct FSkateSim
	{
		FSkateMoveState State;
		FSkateVec2 Pos;
		float Time = 0.f;

		// Same integration order as CharacterMovement walking: velocity first, then position.
		void Step(const FSkateMovementTuning& Tuning, const FSkateMoveInput& Input, float Dt)
		{
			FSkateModel::Step(Tuning, Input, Dt, State);
			Pos += State.Velocity * Dt;
			Time += Dt;
		}
	};

	FSkateMoveInput Stick(const FSkateVec2& Dir, float Mag, float Brake = 0.f, float Boost = 0.f)
	{
		FSkateMoveInput In;
		In.Direction = Dir.GetSafeNormal();
		In.Magnitude = Mag;
		In.Brake = Brake;
		In.Boost = Boost;
		return In;
	}

	// Runs until Pred returns true or MaxTime. Returns the time at which Pred became true (or -1).
	float RunUntil(FSkateSim& Sim, const FSkateMovementTuning& Tuning, float Fps, float MaxTime, const FInputFn& InputFn,
		const std::function<bool(const FSkateSim&)>& Pred, FJitter* Jitter = nullptr,
		const std::function<void(const FSkateSim&, float)>& OnStep = nullptr)
	{
		const float Start = Sim.Time;
		while (Sim.Time - Start < MaxTime)
		{
			const float Dt = Jitter ? Jitter->Next() : 1.f / Fps;
			Sim.Step(Tuning, InputFn(Sim.Time - Start, Sim.State), Dt);
			if (OnStep) { OnStep(Sim, Dt); }
			if (Pred(Sim)) { return Sim.Time - Start; }
		}
		return -1.f;
	}

	struct FBasicMetrics
	{
		float TimeTo95 = -1.f;
		float SpeedAt100ms = 0.f;
		float StopTime = -1.f;
		float StopDistance = 0.f;
		float GlideTime = -1.f;
		float GlideDistance = 0.f;
	};

	FBasicMetrics MeasureBasics(const FSkateTuning& T, float Fps, FJitter* Jitter = nullptr)
	{
		const FSkateMovementTuning& M = T.Movement;
		FBasicMetrics Out;
		const FSkateVec2 Fwd(1.f, 0.f);
		{
			FSkateSim Sim;
			bool bSampled = false;
			Out.TimeTo95 = RunUntil(Sim, M, Fps, 5.f, [&](float, const FSkateMoveState&) { return Stick(Fwd, 1.f); },
				[&](const FSkateSim& S) { return S.State.Velocity.Size() >= 0.95f * M.MaxSpeed; }, Jitter,
				[&](const FSkateSim& S, float) { if (!bSampled && S.Time >= 0.1f - 1e-4f) { Out.SpeedAt100ms = S.State.Velocity.Size(); bSampled = true; } });
		}
		{
			FSkateSim Sim;
			Sim.State.Velocity = Fwd * M.MaxSpeed;
			const FSkateVec2 P0 = Sim.Pos;
			Out.StopTime = RunUntil(Sim, M, Fps, 5.f, [&](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f, 1.f); },
				[&](const FSkateSim& S) { return S.State.Velocity.Size() <= 0.f; }, Jitter);
			Out.StopDistance = (Sim.Pos - P0).Size();
		}
		{
			FSkateSim Sim;
			Sim.State.Velocity = Fwd * M.MaxSpeed;
			const FSkateVec2 P0 = Sim.Pos;
			Out.GlideTime = RunUntil(Sim, M, Fps, 20.f, [&](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f); },
				[&](const FSkateSim& S) { return S.State.Velocity.Size() <= 0.f; }, Jitter);
			Out.GlideDistance = (Sim.Pos - P0).Size();
		}
		return Out;
	}

	float RelDiff(float A, float B) { return SkateMath::Abs(A - B) / SkateMath::Max(SkateMath::Abs(B), 1e-3f); }

	// ------------------------------------------------------------------------------------
	// Simple stand-in for the Chaos ball, ONLY for contact-logic tests: rolls on the ice with
	// exponential damping + rolling resistance (same parameters as ASkateBall uses).
	struct FSimBall
	{
		FSkateVec3 Pos;
		FSkateVec3 Vel;
		// Carried balls get their velocity set every frame (UE: damping/rolling resistance off while carried).
		void Step(const FSkateBallPhysicsTuning& T, float Dt, bool bCarried, float WallX)
		{
			FSkateVec2 V = Vel.XY();
			if (!bCarried)
			{
				V *= std::exp(-T.LinearDamping * Dt);
				const float S = V.Size();
				const float NewS = SkateMath::Max(0.f, S - T.RollingResistance * Dt);
				V = S > 0.f ? V * (NewS / S) : V;
				if (V.Size() < T.StopSpeed) { V = FSkateVec2(); }
			}
			Vel = FSkateVec3(V, 0.f);
			Pos = Pos + Vel * Dt;
			// Optional board at x = WallX (ball bounces back with the board restitution).
			if (Pos.X > WallX - T.Radius)
			{
				Pos.X = WallX - T.Radius;
				Vel.X = -Vel.X * T.BoardRestitution;
			}
		}
	};

	struct FImpulseLog
	{
		float Time;
		ESkateImpulseKind Kind;
		float Speed;
	};

	struct FPlaySim
	{
		FSkateTuning T;
		FSkateSim Skater;
		FSimBall Ball;
		FSkateBallControlState Control;
		FSkateContactReport Report;
		std::vector<FImpulseLog> Impulses;
		int MaxImpulsesInOneFrame = 0;
		bool bLineOfSight = true;
		float WallX = 1.e9f;          // optional board in front of the skater (+X)
		FSkateBallCarry LastCarry;
		int CarriedFrames = 0;

		FSkateContactQuery Query(const FSkateMoveInput& In) const
		{
			FSkateContactQuery Q;
			Q.SkaterPos = Skater.Pos;
			Q.SkaterVel = Skater.State.Velocity;
			Q.Heading = Skater.State.Heading;
			Q.SkaterMaxSpeed = T.Movement.MaxSpeed;
			Q.StickDir = In.Direction;
			Q.StickMag = In.Magnitude;
			Q.bHasBall = true;
			Q.BallPos = Ball.Pos;
			Q.BallVel = Ball.Vel;
			Q.BallRadius = T.BallPhysics.Radius;
			Q.bLineOfSightClear = bLineOfSight;
			return Q;
		}

		// One game frame, same order as in UE: skater moves (CMC), contact is evaluated and either one
		// impulse or the carry steering is applied, then the ball's physics step runs.
		FSkateBallImpulse Frame(const FSkateMoveInput& In, const FSkateBallActionInput& Actions, float Dt)
		{
			Skater.Step(T.Movement, In, Dt);
			if (Skater.Pos.X > WallX - 30.f)
			{
				// Capsule against the board: slide (CMC removes the velocity into the wall).
				Skater.Pos.X = WallX - 30.f;
				Skater.State.Velocity.X = SkateMath::Min(Skater.State.Velocity.X, 0.f);
			}
			FSkateBallImpulse Imp = FSkateBallControl::Update(T.BallControl, Query(In), Actions, Dt, Control, Report, &LastCarry);
			int Count = 0;
			if (Imp.IsValid())
			{
				Ball.Vel = Imp.NewBallVelocity;
				Impulses.push_back({ Skater.Time, Imp.Kind, Imp.NewBallVelocity.Size() });
				++Count;
			}
			else if (LastCarry.bActive)
			{
				Ball.Vel = LastCarry.Velocity;
				++CarriedFrames;
			}
			MaxImpulsesInOneFrame = Count > MaxImpulsesInOneFrame ? Count : MaxImpulsesInOneFrame;
			Ball.Step(T.BallPhysics, Dt, LastCarry.bActive && !Imp.IsValid(), WallX);
			return Imp;
		}

		bool Possessed() const { return Control.Possession.bPossessed; }

		float MinImpulseGap() const
		{
			float Gap = 1e9f;
			for (size_t Index = 1; Index < Impulses.size(); ++Index)
			{
				Gap = SkateMath::Min(Gap, Impulses[Index].Time - Impulses[Index - 1].Time);
			}
			return Gap;
		}

		int CountKind(ESkateImpulseKind Kind) const
		{
			int N = 0;
			for (const FImpulseLog& L : Impulses) { N += L.Kind == Kind ? 1 : 0; }
			return N;
		}
	};

	FPlaySim MakePlay(ESkatePreset Preset, const FSkateVec2& BallOffset)
	{
		FPlaySim P;
		P.T = SkateTuningPresets::Make(Preset);
		P.Ball.Pos = FSkateVec3(BallOffset, P.T.BallPhysics.Radius);
		return P;
	}

	// The older "free ball + dribble touches" mode (possession off).
	FPlaySim MakeLoosePlay(ESkatePreset Preset, const FSkateVec2& BallOffset)
	{
		FPlaySim P = MakePlay(Preset, BallOffset);
		P.T.BallControl.Possession.bEnabled = false;
		return P;
	}

	// ======================================================================================
	// Individual tests
	// ======================================================================================

	void TestInputDeadZone(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Input.RadialDeadZone" };
		const FSkateInputTuning T;
		const FSkateStickResult Inside = SkateInput::ShapeStick(0.1f, 0.1f, T);           // |0.141| < inner
		const FSkateStickResult JustOut = SkateInput::ShapeStick(T.StickDeadZoneInner + 0.005f, 0.f, T);
		const FSkateStickResult Diagonal = SkateInput::ShapeStick(0.68f, 0.68f, T);        // |0.96| >= outer
		const FSkateStickResult Half = SkateInput::ShapeStick(0.f, 0.5f, T);
		// Radial (not per-axis): a small diagonal deflection outside the radius must move, even though each axis < inner.
		const FSkateStickResult SmallDiag = SkateInput::ShapeStick(0.13f, 0.13f, T);       // |0.184| > inner, axes < inner

		// Monotonic & continuous over the whole range.
		bool bMonotonic = true;
		float Prev = 0.f;
		float MaxJump = 0.f;
		for (int Index = 0; Index <= 1000; ++Index)
		{
			const float Raw = static_cast<float>(Index) / 1000.f;
			const float Mag = SkateInput::ShapeStick(Raw, 0.f, T).Magnitude;
			bMonotonic &= Mag + 1e-6f >= Prev;
			MaxJump = SkateMath::Max(MaxJump, Mag - Prev);
			Prev = Mag;
		}
		const float DiagAngle = Diagonal.Direction.Yaw() * SkateMath::RadToDeg;

		// Camera relative: camera yawed 90 deg (looking +Y). Stick up -> world +Y, stick right -> world -X.
		const FSkateStickResult Up90 = SkateInput::ShapeStickWorld(0.f, 1.f, 90.f * SkateMath::DegToRad, T);
		const FSkateStickResult Right90 = SkateInput::ShapeStickWorld(1.f, 0.f, 90.f * SkateMath::DegToRad, T);

		R.bPassed = Inside.Magnitude == 0.f && JustOut.Magnitude > 0.f && JustOut.Magnitude < 0.01f
			&& Diagonal.Magnitude >= 0.999f && SkateMath::Abs(DiagAngle - 45.f) < 0.5f
			&& Half.Magnitude > 0.2f && Half.Magnitude < 0.6f
			&& SmallDiag.Magnitude > 0.f
			&& bMonotonic && MaxJump < 0.01f
			&& Up90.Direction.Y > 0.99f && Right90.Direction.X < -0.99f;
		R.Details = Fmt("inside=%.3f justOut=%.4f diag=%.3f@%.1fdeg half=%.3f smallDiag=%.3f monotonic=%d maxStep=%.4f camUp->(%.2f,%.2f) camRight->(%.2f,%.2f)",
			Inside.Magnitude, JustOut.Magnitude, Diagonal.Magnitude, DiagAngle, Half.Magnitude, SmallDiag.Magnitude, bMonotonic ? 1 : 0, MaxJump,
			Up90.Direction.X, Up90.Direction.Y, Right90.Direction.X, Right90.Direction.Y);
		Out.push_back(R);
	}

	void TestAccelerationAndFps(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R{ Fmt("Move.Accelerate[%s]", SkateTuningPresets::Name(Preset)) };
			float Times[3];
			float Early[3];
			for (int Index = 0; Index < 3; ++Index)
			{
				const FBasicMetrics M = MeasureBasics(T, FrameRates[Index]);
				Times[Index] = M.TimeTo95;
				Early[Index] = M.SpeedAt100ms;
			}
			FJitter Jitter;
			const FBasicMetrics J = MeasureBasics(T, 0.f, &Jitter);
			const float Spread = SkateMath::Max(SkateMath::Max(RelDiff(Times[0], Times[2]), RelDiff(Times[1], Times[2])), RelDiff(J.TimeTo95, Times[2]));
			R.bPassed = Times[2] >= 0.75f && Times[2] <= 1.4f && Early[2] >= 100.f && Spread < 0.06f;
			R.Details = Fmt("t95%%: 30fps=%.3fs 60fps=%.3fs 120fps=%.3fs jitter=%.3fs (spread %.1f%%), speed after 0.1s=%.0f cm/s",
				Times[0], Times[1], Times[2], J.TimeTo95, Spread * 100.f, Early[2]);
			Out.push_back(R);
		}
	}

	void TestBrakeStop(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R{ Fmt("Move.BrakeStop[%s]", SkateTuningPresets::Name(Preset)) };
			float Times[3];
			float Dists[3];
			for (int Index = 0; Index < 3; ++Index)
			{
				const FBasicMetrics M = MeasureBasics(T, FrameRates[Index]);
				Times[Index] = M.StopTime;
				Dists[Index] = M.StopDistance;
			}
			// No reversal and no fault flag during a full stop at 60 fps.
			FSkateSim Sim;
			Sim.State.Velocity = FSkateVec2(T.Movement.MaxSpeed, 0.f);
			bool bReversed = false;
			bool bFault = false;
			RunUntil(Sim, T.Movement, 60.f, 2.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f, 1.f); },
				[](const FSkateSim&) { return false; }, nullptr,
				[&](const FSkateSim& S, float) { bReversed |= S.State.Velocity.X < 0.f; bFault |= S.State.bBrakeReversalFault; });
			const float Spread = SkateMath::Max(RelDiff(Times[0], Times[2]), RelDiff(Dists[0], Dists[2]));
			R.bPassed = Times[2] >= 0.45f && Times[2] <= 0.95f && Spread < 0.08f && !bReversed && !bFault && Sim.State.Velocity.Size() == 0.f;
			R.Details = Fmt("stop from %.0f: time 30/60/120fps = %.3f/%.3f/%.3fs, distance = %.0f/%.0f/%.0f cm, reversed=%d fault=%d",
				T.Movement.MaxSpeed, Times[0], Times[1], Times[2], Dists[0], Dists[1], Dists[2], bReversed ? 1 : 0, bFault ? 1 : 0);
			Out.push_back(R);
		}
	}

	void TestAnalogBrake(std::vector<FSkateTestResult>& Out)
	{
		const FSkateTuning T;
		FSkateTestResult R{ "Move.AnalogBrake" };
		auto StopTimeFor = [&](float Trigger)
		{
			FSkateSim Sim;
			Sim.State.Velocity = FSkateVec2(T.Movement.MaxSpeed, 0.f);
			const float Brake = SkateInput::ShapeBrake(Trigger, T.Input);
			return RunUntil(Sim, T.Movement, 60.f, 10.f, [&](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f, Brake); },
				[](const FSkateSim& S) { return S.State.Velocity.Size() <= 0.f; });
		};
		const float Light = StopTimeFor(0.3f);
		const float Mid = StopTimeFor(0.6f);
		const float Full = StopTimeFor(1.f);
		R.bPassed = Light > Mid && Mid > Full && Light > 1.8f * Full;
		R.Details = Fmt("stop time with trigger 0.3/0.6/1.0 = %.2f/%.2f/%.2fs (lighter press = gentler, continuous)", Light, Mid, Full);
		Out.push_back(R);
	}

	void TestBrakeNearZero(std::vector<FSkateTestResult>& Out)
	{
		const FSkateTuning T;
		FSkateTestResult R{ "Move.BrakeNearZeroNoBackwardMotion" };
		bool bOk = true;
		// At rest, full brake + stick released: stays still.
		FSkateSim Rest;
		RunUntil(Rest, T.Movement, 60.f, 1.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f, 1.f); },
			[](const FSkateSim&) { return false; }, nullptr,
			[&](const FSkateSim& S, float) { bOk &= S.State.Velocity.Size() == 0.f; });
		// Slow (30 cm/s) in 8 directions: brake never produces motion against the original direction.
		float WorstDot = 1.f;
		for (int Index = 0; Index < 8; ++Index)
		{
			const FSkateVec2 Dir = FSkateVec2::FromYaw(static_cast<float>(Index) * SkateMath::Pi / 4.f);
			FSkateSim Sim;
			Sim.State.Velocity = Dir * 30.f;
			Sim.State.Heading = Dir.Rotated(0.7f);
			RunUntil(Sim, T.Movement, 30.f, 1.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f, 0.5f); },
				[](const FSkateSim&) { return false; }, nullptr,
				[&](const FSkateSim& S, float) { WorstDot = SkateMath::Min(WorstDot, S.State.Velocity.Dot(Dir)); });
			bOk &= Sim.State.Velocity.Size() == 0.f;
		}
		R.bPassed = bOk && WorstDot >= 0.f;
		R.Details = Fmt("rest stays still and slow skater stops in all 8 directions: %d, worst dot(velocity, initial dir) = %.3f", bOk ? 1 : 0, WorstDot);
		Out.push_back(R);
	}

	void TestGlide(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R{ Fmt("Move.GlideRelease[%s]", SkateTuningPresets::Name(Preset)) };
			FSkateSim Sim;
			Sim.State.Velocity = FSkateVec2(T.Movement.MaxSpeed, 0.f);
			bool bMonotonic = true;
			float Prev = Sim.State.Velocity.Size();
			const float StopTime = RunUntil(Sim, T.Movement, 60.f, 20.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f); },
				[](const FSkateSim& S) { return S.State.Velocity.Size() <= 0.f; }, nullptr,
				[&](const FSkateSim& S, float) { const float Sp = S.State.Velocity.Size(); bMonotonic &= Sp <= Prev + 1e-3f; Prev = Sp; });
			const FSkateVec2 StopPos = Sim.Pos;
			// No micro-sliding after the stop.
			RunUntil(Sim, T.Movement, 60.f, 1.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(), 0.f); }, [](const FSkateSim&) { return false; });
			const float Creep = (Sim.Pos - StopPos).Size();
			const FBasicMetrics M30 = MeasureBasics(T, 30.f);
			const FBasicMetrics M120 = MeasureBasics(T, 120.f);
			R.bPassed = bMonotonic && StopTime > 2.5f && StopTime < 9.f && M120.GlideDistance > 400.f && Creep == 0.f
				&& RelDiff(M30.GlideDistance, M120.GlideDistance) < 0.05f;
			R.Details = Fmt("glide from %.0f: stops after %.2fs, distance 30fps=%.0f 120fps=%.0f cm, monotonic=%d, creep after stop=%.2f cm",
				T.Movement.MaxSpeed, StopTime, M30.GlideDistance, M120.GlideDistance, bMonotonic ? 1 : 0, Creep);
			Out.push_back(R);
		}
	}

	void TestPartialStick(std::vector<FSkateTestResult>& Out)
	{
		const FSkateTuning T;
		FSkateTestResult R{ "Move.PartialStickSlowControl" };
		const FSkateStickResult S = SkateInput::ShapeStick(0.f, 0.45f, T.Input);
		FSkateSim Sim;
		float MinLate = 1e9f;
		float MaxLate = 0.f;
		RunUntil(Sim, T.Movement, 60.f, 4.f, [&](float, const FSkateMoveState&) { return Stick(FSkateVec2(1.f, 0.f), S.Magnitude); },
			[](const FSkateSim&) { return false; }, nullptr,
			[&](const FSkateSim& Sm, float) { if (Sm.Time > 2.f) { const float Sp = Sm.State.Velocity.Size(); MinLate = SkateMath::Min(MinLate, Sp); MaxLate = SkateMath::Max(MaxLate, Sp); } });
		const float Expected = S.Magnitude * T.Movement.MaxSpeed;
		R.bPassed = MaxLate < 0.5f * T.Movement.MaxSpeed && RelDiff(MaxLate, Expected) < 0.05f && (MaxLate - MinLate) < 2.f;
		R.Details = Fmt("raw stick 0.45 -> shaped %.3f, steady speed %.0f..%.0f cm/s (target %.0f), max %.0f", S.Magnitude, MinLate, MaxLate, Expected, T.Movement.MaxSpeed);
		Out.push_back(R);
	}

	void TestHighSpeedTurn(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R{ Fmt("Move.HighSpeedTurn[%s]", SkateTuningPresets::Name(Preset)) };
			FSkateSim Sim;
			Sim.State.Velocity = FSkateVec2(T.Movement.MaxSpeed, 0.f);
			float MaxLatAccel = 0.f;
			FSkateVec2 PrevVel = Sim.State.Velocity;
			const float TurnTime = RunUntil(Sim, T.Movement, 120.f, 3.f,
				[](float, const FSkateMoveState&) { return Stick(FSkateVec2(0.f, 1.f), 1.f); },
				[](const FSkateSim& S) { return S.State.Velocity.GetSafeNormal().Y > 0.999f; }, nullptr,
				[&](const FSkateSim& S, float Dt)
				{
					const FSkateVec2 D0 = PrevVel.GetSafeNormal();
					const FSkateVec2 D1 = S.State.Velocity.GetSafeNormal();
					const float Omega = SkateMath::Abs(D0.SignedAngleTo(D1)) / Dt;
					MaxLatAccel = SkateMath::Max(MaxLatAccel, Omega * S.State.Velocity.Size());
					PrevVel = S.State.Velocity;
				});
			const float SpeedKept = Sim.State.Velocity.Size() / T.Movement.MaxSpeed;
			// The 90 deg turn takes a curved path: displacement along +X while turning shows an arc, not a pivot.
			const float ArcDepth = Sim.Pos.X;
			// Grip is capped at MaxLateralAccel; thrust along a blade that already points into the turn adds a
			// small extra centripetal share, so the bound for the total is 10% above the grip cap.
			R.bPassed = TurnTime > 0.f && MaxLatAccel <= T.Movement.MaxLateralAccel * 1.1f && SpeedKept > 0.7f && ArcDepth > 150.f;
			R.Details = Fmt("90deg turn at %.0f: %.2fs, peak lateral accel %.0f (grip cap %.0f, total bound +10%%), speed kept %.0f%%, arc depth %.0f cm",
				T.Movement.MaxSpeed, TurnTime, MaxLatAccel, T.Movement.MaxLateralAccel, SpeedKept * 100.f, ArcDepth);
			Out.push_back(R);
		}
	}

	void TestLowSpeedPivot(std::vector<FSkateTestResult>& Out)
	{
		const FSkateTuning T;
		FSkateTestResult R{ "Move.LowSpeedPivot" };
		FSkateSim Sim;
		Sim.State.Velocity = FSkateVec2(40.f, 0.f);
		const float Time = RunUntil(Sim, T.Movement, 60.f, 2.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(-1.f, 0.f), 0.6f); },
			[](const FSkateSim& S) { return S.State.Heading.X < -0.98f; });
		const float Travel = Sim.Pos.Size();
		R.bPassed = Time > 0.f && Time < 0.4f && Travel < 60.f;
		R.Details = Fmt("near standstill, stick back: heading turned 180deg in %.2fs, displacement during pivot %.0f cm", Time, Travel);
		Out.push_back(R);
	}

	void TestReverseNoInstantFlip(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R{ Fmt("Move.ReverseStick[%s]", SkateTuningPresets::Name(Preset)) };
			FSkateSim Sim;
			Sim.State.Velocity = FSkateVec2(T.Movement.MaxSpeed, 0.f);
			float MaxStepChange = 0.f;
			float SlowestBeforeReverse = 1e9f;
			bool bWentNegative = false;
			bool bReverseStopSeen = false;
			float PrevX = Sim.State.Velocity.X;
			const float Dt = 1.f / 60.f;
			const float Time = RunUntil(Sim, T.Movement, 60.f, 4.f, [](float, const FSkateMoveState&) { return Stick(FSkateVec2(-1.f, 0.f), 1.f); },
				[&](const FSkateSim& S) { return S.State.Velocity.X <= -0.9f * T.Movement.MaxSpeed; }, nullptr,
				[&](const FSkateSim& S, float)
				{
					MaxStepChange = SkateMath::Max(MaxStepChange, SkateMath::Abs(S.State.Velocity.X - PrevX));
					PrevX = S.State.Velocity.X;
					bReverseStopSeen |= S.State.bReverseStop;
					if (!bWentNegative) { SlowestBeforeReverse = SkateMath::Min(SlowestBeforeReverse, S.State.Velocity.Size()); }
					bWentNegative |= S.State.Velocity.X < 0.f;
				});
			// Largest physically allowed change per frame: brake + friction or thrust from standstill.
			const float Allowed = SkateMath::Max(T.Movement.ReverseBrakeDecel + T.Movement.GlideFriction + T.Movement.GlideDrag * T.Movement.MaxSpeed,
				T.Movement.BoostMaxSpeed / T.Movement.ThrustTimeConstant * 1.6f) * Dt;
			R.bPassed = Time > 0.f && bReverseStopSeen && MaxStepChange <= Allowed && SlowestBeforeReverse < T.Movement.ReverseMinSpeed;
			R.Details = Fmt("from +%.0f to -90%% in %.2fs, reverse-stop phase=%d, max |dVx| per frame %.0f (allowed %.0f), slowest before reversing %.0f cm/s",
				T.Movement.MaxSpeed, Time, bReverseStopSeen ? 1 : 0, MaxStepChange, Allowed, SlowestBeforeReverse);
			Out.push_back(R);
		}
	}

	// Time-scripted course (slalom-like weave + brake) compared across frame rates.
	void TestCourseFpsIndependence(std::vector<FSkateTestResult>& Out)
	{
		const FSkateTuning T;
		FSkateTestResult R{ "Move.CourseFpsIndependence" };
		auto Script = [](float Time, const FSkateMoveState&)
		{
			if (Time < 6.f)
			{
				const float Yaw = 0.9f * std::sin(Time * 2.2f);
				return Stick(FSkateVec2::FromYaw(Yaw), Time < 3.f ? 1.f : 0.6f, 0.f, Time > 1.5f && Time < 3.f ? 1.f : 0.f);
			}
			if (Time < 7.f) { return Stick(FSkateVec2(), 0.f); }
			return Stick(FSkateVec2(), 0.f, 1.f);
		};
		FSkateVec2 Final[4];
		float PathLen = 0.f;
		for (int Index = 0; Index < 4; ++Index)
		{
			FSkateSim Sim;
			FJitter Jitter;
			RunUntil(Sim, T.Movement, Index < 3 ? FrameRates[Index] : 0.f, 8.f, Script, [](const FSkateSim&) { return false; },
				Index < 3 ? nullptr : &Jitter,
				[&](const FSkateSim& S, float Dt) { if (Index == 2) { PathLen += S.State.Velocity.Size() * Dt; } });
			Final[Index] = Sim.Pos;
		}
		float Worst = 0.f;
		for (int Index = 0; Index < 4; ++Index) { Worst = SkateMath::Max(Worst, (Final[Index] - Final[2]).Size()); }
		R.bPassed = Worst < 0.02f * PathLen;
		R.Details = Fmt("8 s scripted weave+boost+glide+brake, path %.0f cm. End point deviation vs 120fps: 30fps=%.1f 60fps=%.1f jitter=%.1f cm (worst %.2f%% of path)",
			PathLen, (Final[0] - Final[2]).Size(), (Final[1] - Final[2]).Size(), (Final[3] - Final[2]).Size(), 100.f * Worst / SkateMath::Max(PathLen, 1.f));
		Out.push_back(R);
	}

	// ---------------------------------- Contact -----------------------------------------

	void TestContactGating(std::vector<FSkateTestResult>& Out)
	{
		const FSkateTuning T;
		FSkateTestResult R{ "Contact.ReachGating" };
		FSkateContactQuery Q;
		Q.bHasBall = true;
		Q.BallRadius = T.BallPhysics.Radius;
		auto At = [&](float X, float Y, float Z, bool bLos = true)
		{
			FSkateContactQuery C = Q;
			C.BallPos = FSkateVec3(X, Y, Z);
			C.bLineOfSightClear = bLos;
			return FSkateBallControl::Evaluate(T.BallControl, C).Reason;
		};
		const float Ground = T.BallPhysics.Radius;
		const ESkateContactReason Front = At(45.f, 0.f, Ground);
		const ESkateContactReason Behind = At(-30.f, 0.f, Ground);
		const ESkateContactReason Side = At(5.f, 40.f, Ground);
		const ESkateContactReason Far = At(300.f, 0.f, Ground);
		const ESkateContactReason High = At(45.f, 0.f, Ground + 40.f);
		const ESkateContactReason Wall = At(45.f, 0.f, Ground, false);
		const ESkateContactReason Edge = At(45.f + T.BallControl.ReachRadius + 5.f, 0.f, Ground);
		R.bPassed = Front == ESkateContactReason::Reachable && Behind == ESkateContactReason::OutsideAngle && Side == ESkateContactReason::OutsideAngle
			&& Far == ESkateContactReason::TooFar && High == ESkateContactReason::Airborne && Wall == ESkateContactReason::BlockedByBoard
			&& Edge == ESkateContactReason::ActionReachOnly;
		R.Details = Fmt("front=%s | behind=%s | side=%s | far=%s | high=%s | through wall=%s | zone edge=%s",
			SkateContactReasonName(Front), SkateContactReasonName(Behind), SkateContactReasonName(Side), SkateContactReasonName(Far),
			SkateContactReasonName(High), SkateContactReasonName(Wall), SkateContactReasonName(Edge));
		Out.push_back(R);
	}

	void TestNoActionOutOfReach(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Contact.NoKickOrPushOutOfReach" };
		bool bOk = true;
		std::string Info;
		const FSkateVec2 Offsets[] = { FSkateVec2(-35.f, 0.f), FSkateVec2(250.f, 0.f), FSkateVec2(0.f, 60.f) };
		for (const FSkateVec2& Off : Offsets)
		{
			FPlaySim P = MakePlay(ESkatePreset::Balanced, Off);
			const float Dt = 1.f / 60.f;
			FSkateBallActionInput A;
			A.bKickPressed = true;
			P.Frame(Stick(FSkateVec2(), 0.f), A, Dt);
			for (int Index = 0; Index < 20; ++Index) { P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt); }
			A = FSkateBallActionInput();
			A.bKickReleased = true;
			P.Frame(Stick(FSkateVec2(), 0.f), A, Dt);
			A = FSkateBallActionInput();
			A.bPushPressed = true;
			for (int Index = 0; Index < 60; ++Index) { P.Frame(Stick(FSkateVec2(), 0.f), Index == 0 ? A : FSkateBallActionInput(), Dt); }
			bOk &= P.Impulses.empty() && P.Control.KickBuffer < 0.f && P.Control.PushBuffer < 0.f
				&& P.Control.LastFailedAction != ESkateImpulseKind::None;
			Info += Fmt("[ball at %.0f,%.0f: impulses=%d, last whiff=%s (%s)] ", Off.X, Off.Y, static_cast<int>(P.Impulses.size()),
				SkateImpulseKindName(P.Control.LastFailedAction), SkateContactReasonName(P.Control.LastFailReason));
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}

	void TestKickBufferExpires(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Contact.KickBufferShort" };
		const FSkateTuning T;
		// Ball rolls towards the standing skater; X released before it arrives.
		auto Run = [&](float ReleaseLead) -> bool
		{
			FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(260.f, 0.f));
			P.Ball.Vel = FSkateVec3(-400.f, 0.f, 0.f);
			const float Dt = 1.f / 60.f;
			// Find when the ball would become reachable.
			FPlaySim Probe = P;
			float ArriveTime = -1.f;
			for (int Index = 0; Index < 120 && ArriveTime < 0.f; ++Index)
			{
				Probe.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt);
				if (Probe.Report.Reason == ESkateContactReason::Reachable || Probe.Report.Reason == ESkateContactReason::ActionReachOnly) { ArriveTime = Probe.Skater.Time; }
			}
			FSkateBallActionInput Press;
			Press.bKickPressed = true;
			P.Frame(Stick(FSkateVec2(), 0.f), Press, Dt);
			while (P.Skater.Time < ArriveTime - ReleaseLead - Dt * 0.5f) { P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt); }
			FSkateBallActionInput Release;
			Release.bKickReleased = true;
			P.Frame(Stick(FSkateVec2(), 0.f), Release, Dt);
			for (int Index = 0; Index < 60; ++Index) { P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt); }
			return P.CountKind(ESkateImpulseKind::Kick) == 1;
		};
		const bool bSlightlyEarly = Run(0.06f);
		const bool bWayTooEarly = Run(0.4f);
		R.bPassed = bSlightlyEarly && !bWayTooEarly;
		R.Details = Fmt("released 0.06 s before reach -> kick=%d (buffer %.2fs); released 0.4 s before reach -> kick=%d (must whiff)",
			bSlightlyEarly ? 1 : 0, T.BallControl.KickBufferTime, bWayTooEarly ? 1 : 0);
		Out.push_back(R);
	}

	void TestKickFpsAndCharge(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Contact.KickSameAcrossFps" };
		float Speeds[3];
		for (int Index = 0; Index < 3; ++Index)
		{
			FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(45.f, 0.f));
			const float Dt = 1.f / FrameRates[Index];
			FSkateBallActionInput Press;
			Press.bKickPressed = true;
			P.Frame(Stick(FSkateVec2(), 0.f), Press, Dt);
			const float Start = P.Skater.Time;
			while (P.Skater.Time - Start < 0.5f - Dt * 0.5f) { P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt); }
			FSkateBallActionInput Release;
			Release.bKickReleased = true;
			P.Frame(Stick(FSkateVec2(), 0.f), Release, Dt);
			Speeds[Index] = P.Impulses.empty() ? 0.f : P.Impulses.back().Speed;
		}
		// Holding far longer than the max charge time gives exactly max power.
		FPlaySim Long = MakePlay(ESkatePreset::Balanced, FSkateVec2(45.f, 0.f));
		FSkateBallActionInput Press;
		Press.bKickPressed = true;
		Long.Frame(Stick(FSkateVec2(), 0.f), Press, 1.f / 60.f);
		for (int Index = 0; Index < 180; ++Index) { Long.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), 1.f / 60.f); }
		FSkateBallActionInput Release;
		Release.bKickReleased = true;
		Long.Frame(Stick(FSkateVec2(), 0.f), Release, 1.f / 60.f);
		const float MaxPower = Long.Control.LastPower;
		const float Spread = SkateMath::Max(RelDiff(Speeds[0], Speeds[2]), RelDiff(Speeds[1], Speeds[2]));
		R.bPassed = Speeds[2] > 0.f && Spread < 0.05f && MaxPower == 1.f;
		R.Details = Fmt("0.5 s charge: ball speed 30/60/120fps = %.0f/%.0f/%.0f cm/s (spread %.1f%%); 3 s hold -> power %.2f (capped)",
			Speeds[0], Speeds[1], Speeds[2], Spread * 100.f, MaxPower);
		Out.push_back(R);
	}

	// Dribble at a constant stick for N seconds. Returns stats.
	struct FDribbleStats
	{
		int Touches = 0;
		float MaxLead = 0.f;     // ball ahead of skater (cm)
		float AvgLead = 0.f;
		float MinLead = 1e9f;    // < 0 means ball got behind
		float SkaterSpeed = 0.f;
		float MinImpulseGap = 0.f;
		int MaxPerFrame = 0;
	};

	FDribbleStats Dribble(float StickMag, float Seconds, float Fps)
	{
		FPlaySim P = MakeLoosePlay(ESkatePreset::Balanced, FSkateVec2(70.f, 0.f));
		const float Dt = 1.f / Fps;
		FDribbleStats S;
		int Samples = 0;
		while (P.Skater.Time < Seconds)
		{
			P.Frame(Stick(FSkateVec2(1.f, 0.f), StickMag), FSkateBallActionInput(), Dt);
			if (P.Skater.Time > 1.5f)
			{
				const float Lead = P.Ball.Pos.X - P.Skater.Pos.X;
				S.MaxLead = SkateMath::Max(S.MaxLead, Lead);
				S.MinLead = SkateMath::Min(S.MinLead, Lead);
				S.AvgLead += Lead;
				++Samples;
			}
		}
		S.AvgLead /= SkateMath::Max(static_cast<float>(Samples), 1.f);
		S.Touches = P.CountKind(ESkateImpulseKind::Touch);
		S.SkaterSpeed = P.Skater.State.Velocity.Size();
		S.MinImpulseGap = P.MinImpulseGap();
		S.MaxPerFrame = P.MaxImpulsesInOneFrame;
		return S;
	}

	void TestDribbleStraight(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "LooseDribble.Straight" };
		const FDribbleStats Slow = Dribble(0.35f, 10.f, 60.f);
		const FDribbleStats Fast = Dribble(1.f, 10.f, 60.f);
		const FDribbleStats Fast30 = Dribble(1.f, 10.f, 30.f);
		const FSkateTuning T;
		R.bPassed = Slow.Touches >= 5 && Fast.Touches >= 5
			&& Slow.MinLead > 0.f && Fast.MinLead > 0.f        // ball stays in front
			&& Slow.MaxLead < 250.f && Fast.MaxLead < 520.f     // never lost on a straight line
			&& Fast.AvgLead > Slow.AvgLead * 1.3f               // released further ahead at speed
			&& Fast.MinImpulseGap >= T.BallControl.TouchCooldown - 1e-3f
			&& Fast30.Touches >= 5 && RelDiff(Fast30.AvgLead, Fast.AvgLead) < 0.2f;
		R.Details = Fmt("slow(%.0f cm/s): %d touches, lead avg %.0f [%.0f..%.0f] | fast(%.0f): %d touches, lead avg %.0f [%.0f..%.0f], min gap %.2fs | fast@30fps lead avg %.0f",
			Slow.SkaterSpeed, Slow.Touches, Slow.AvgLead, Slow.MinLead, Slow.MaxLead,
			Fast.SkaterSpeed, Fast.Touches, Fast.AvgLead, Fast.MinLead, Fast.MaxLead, Fast.MinImpulseGap, Fast30.AvgLead);
		Out.push_back(R);
	}

	void TestDribbleTurnKeepsBallInertia(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "LooseDribble.TurnBallKeepsInertia" };
		FPlaySim P = MakeLoosePlay(ESkatePreset::Balanced, FSkateVec2(70.f, 0.f));
		const float Dt = 1.f / 60.f;
		while (P.Skater.Time < 3.f) { P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.8f), FSkateBallActionInput(), Dt); }
		// Hard turn to +Y. Between touches the ball direction may only change from damping (i.e. not at all).
		bool bOnlyChangesOnImpulse = true;
		FSkateVec2 PrevDir = P.Ball.Vel.XY().GetSafeNormal();
		int TouchesAfterTurn = 0;
		while (P.Skater.Time < 6.f)
		{
			const FSkateBallImpulse Imp = P.Frame(Stick(FSkateVec2(0.f, 1.f), 0.8f), FSkateBallActionInput(), Dt);
			const FSkateVec2 Dir = P.Ball.Vel.XY().GetSafeNormal(PrevDir);
			if (!Imp.IsValid() && P.Ball.Vel.XY().Size() > 1.f && PrevDir.Dot(Dir) < 0.9999f) { bOnlyChangesOnImpulse = false; }
			TouchesAfterTurn += Imp.Kind == ESkateImpulseKind::Touch ? 1 : 0;
			PrevDir = Dir;
		}
		R.bPassed = bOnlyChangesOnImpulse;
		R.Details = Fmt("ball direction changed only on contact frames: %d (touches after the turn: %d; re-gaining the ball after a hard turn is the player's job)",
			bOnlyChangesOnImpulse ? 1 : 0, TouchesAfterTurn);
		Out.push_back(R);
	}

	void TestBrakeThenRecover(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "LooseDribble.BrakeThenRecover" };
		FPlaySim P = MakeLoosePlay(ESkatePreset::Balanced, FSkateVec2(70.f, 0.f));
		const float Dt = 1.f / 60.f;
		while (P.Skater.Time < 4.f) { P.Frame(Stick(FSkateVec2(1.f, 0.f), 1.f), FSkateBallActionInput(), Dt); }
		const int TouchesBefore = P.CountKind(ESkateImpulseKind::Touch);
		while (P.Skater.Time < 5.5f) { P.Frame(Stick(FSkateVec2(), 0.f, 1.f), FSkateBallActionInput(), Dt); }
		const float BallSpeedAfterStop = P.Ball.Vel.Size();
		const float SkaterSpeedAfterStop = P.Skater.State.Velocity.Size();
		const int TouchesDuringBrake = P.CountKind(ESkateImpulseKind::Touch) - TouchesBefore;
		// Chase it.
		float RecoverTime = -1.f;
		const float ChaseStart = P.Skater.Time;
		const int TouchesBeforeChase = P.CountKind(ESkateImpulseKind::Touch);
		while (P.Skater.Time < ChaseStart + 6.f && RecoverTime < 0.f)
		{
			const FSkateVec2 ToBall = (P.Ball.Pos.XY() - P.Skater.Pos).GetSafeNormal();
			P.Frame(Stick(ToBall, 1.f), FSkateBallActionInput(), Dt);
			if (P.CountKind(ESkateImpulseKind::Touch) > TouchesBeforeChase) { RecoverTime = P.Skater.Time - ChaseStart; }
		}
		R.bPassed = SkaterSpeedAfterStop == 0.f && BallSpeedAfterStop > 50.f && TouchesDuringBrake == 0 && RecoverTime > 0.f && RecoverTime < 4.f;
		R.Details = Fmt("after 1.5 s brake: skater %.0f cm/s, ball still rolling %.0f cm/s, touches while braking %d; chased and touched again after %.2fs",
			SkaterSpeedAfterStop, BallSpeedAfterStop, TouchesDuringBrake, RecoverTime);
		Out.push_back(R);
	}

	void TestSingleImpulseUnderMashing(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Contact.NoDoubleImpulse" };
		// Worst case: the ball is put back at the feet right after every impulse, rolling into the skater,
		// while A is pressed every frame and X is pressed/released every other frame and the stick asks to dribble.
		// Touch, push, kick and body block are all eligible in the same frames.
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(45.f, 0.f));
		const float Dt = 1.f / 120.f;
		int Frame = 0;
		while (P.Skater.Time < 4.f)
		{
			FSkateBallActionInput A;
			A.bPushPressed = true;
			A.bKickPressed = (Frame % 2) == 0;
			A.bKickReleased = (Frame % 2) == 1;
			const size_t Before = P.Impulses.size();
			P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.3f), A, Dt);
			if (P.Impulses.size() != Before)
			{
				P.Ball.Pos = FSkateVec3(P.Skater.Pos + P.Skater.State.Heading * 40.f, P.T.BallPhysics.Radius);
				P.Ball.Vel = FSkateVec3(P.Skater.State.Heading * -150.f, 0.f);
			}
			++Frame;
		}
		const float Gap = P.MinImpulseGap();
		R.bPassed = P.MaxImpulsesInOneFrame <= 1 && Gap >= FSkateBallControl::MinImpulseGap - 1e-4f && P.Impulses.size() > 20;
		R.Details = Fmt("4 s, every trigger active every frame: %d impulses (touch %d push %d kick %d body %d), max per frame %d, min gap %.3fs (limit %.3fs)",
			static_cast<int>(P.Impulses.size()), P.CountKind(ESkateImpulseKind::Touch), P.CountKind(ESkateImpulseKind::Push),
			P.CountKind(ESkateImpulseKind::Kick), P.CountKind(ESkateImpulseKind::BodyBlock), P.MaxImpulsesInOneFrame, Gap, FSkateBallControl::MinImpulseGap);
		Out.push_back(R);
	}

	void TestBodyBlock(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Contact.BodyBlockNoPassThrough" };
		// Ball fired at a standing skater from the side (outside the foot zone).
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(0.f, 200.f));
		P.Ball.Vel = FSkateVec3(0.f, -600.f, 0.f);
		const float Dt = 1.f / 60.f;
		float MinDist = 1e9f;
		for (int Index = 0; Index < 90; ++Index)
		{
			P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt);
			MinDist = SkateMath::Min(MinDist, (P.Ball.Pos.XY() - P.Skater.Pos).Size());
		}
		const FSkateTuning T;
		R.bPassed = P.CountKind(ESkateImpulseKind::BodyBlock) == 1 && P.Ball.Pos.Y > 0.f && P.Ball.Vel.Y >= 0.f
			&& P.Ball.Vel.Size() < 600.f * T.BallControl.BodyRestitution + 1.f && P.CountKind(ESkateImpulseKind::Touch) == 0;
		R.Details = Fmt("side shot 600 cm/s: body blocks=%d, ball ended on its own side=%d, rebound %.0f cm/s, closest centre distance %.0f cm",
			P.CountKind(ESkateImpulseKind::BodyBlock), P.Ball.Pos.Y > 0.f ? 1 : 0, P.Ball.Vel.Size(), MinDist);
		Out.push_back(R);
	}

	void TestDisabledInteraction(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Contact.StageA_InteractionOff" };
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(60.f, 0.f));
		const float Dt = 1.f / 60.f;
		for (int Index = 0; Index < 300; ++Index)
		{
			FSkateMoveInput In = Stick(FSkateVec2(1.f, 0.f), 1.f);
			FSkateContactQuery Q = P.Query(In);
			Q.bInteractionEnabled = false;
			P.Skater.Step(P.T.Movement, In, Dt);
			FSkateBallActionInput A;
			A.bPushPressed = Index % 10 == 0;
			const FSkateBallImpulse Imp = FSkateBallControl::Update(P.T.BallControl, Q, A, Dt, P.Control, P.Report);
			if (Imp.IsValid()) { P.Impulses.push_back({ P.Skater.Time, Imp.Kind, 0.f }); }
		}
		R.bPassed = P.Impulses.empty();
		R.Details = Fmt("interaction off: impulses=%d", static_cast<int>(P.Impulses.size()));
		Out.push_back(R);
	}

	// ---------------------------------- Possession -------------------------------------

	struct FCarryStats
	{
		int Frames = 0;
		int PossessedFrames = 0;
		int Losses = 0;
		float MaxAngleDeg = 0.f;      // between blade heading and skater->ball direction
		float P95AngleDeg = 0.f;
		float MinBodyDistance = 1e9f; // skater centre -> ball centre
		float MinAhead = 1e9f;        // ball position along the heading (cm)
		float MaxAhead = 0.f;
		float MeanError = 0.f;
		float MaxError = 0.f;
		std::vector<float> Angles;

		void Sample(const FPlaySim& P, bool bWasPossessed)
		{
			++Frames;
			if (!P.Possessed())
			{
				Losses += bWasPossessed ? 1 : 0;
				return;
			}
			++PossessedFrames;
			const FSkateVec2 Rel = P.Ball.Pos.XY() - P.Skater.Pos;
			const float Angle = SkateMath::Abs(P.Skater.State.Heading.SignedAngleTo(Rel.GetSafeNormal())) * SkateMath::RadToDeg;
			Angles.push_back(Angle);
			MaxAngleDeg = SkateMath::Max(MaxAngleDeg, Angle);
			MinBodyDistance = SkateMath::Min(MinBodyDistance, Rel.Size());
			const float Ahead = Rel.Dot(P.Skater.State.Heading);
			MinAhead = SkateMath::Min(MinAhead, Ahead);
			MaxAhead = SkateMath::Max(MaxAhead, Ahead);
			MeanError += P.Control.Possession.CarryError;
			MaxError = SkateMath::Max(MaxError, P.Control.Possession.CarryError);
		}

		void Finish()
		{
			MeanError /= SkateMath::Max(static_cast<float>(PossessedFrames), 1.f);
			if (!Angles.empty())
			{
				std::vector<float> Sorted = Angles;
				std::sort(Sorted.begin(), Sorted.end());
				P95AngleDeg = Sorted[static_cast<size_t>(0.95f * static_cast<float>(Sorted.size() - 1))];
			}
		}
	};

	// Skater with the ball already trapped at the feet.
	FPlaySim MakeCarryPlay(ESkatePreset Preset)
	{
		FPlaySim P = MakePlay(Preset, FSkateVec2(42.f, 0.f));
		P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), 1.f / 60.f);
		return P;
	}

	using FStickScript = std::function<FSkateMoveInput(float)>;

	FCarryStats RunCarry(FPlaySim& P, float Seconds, float Fps, const FStickScript& Script)
	{
		FCarryStats Stats;
		const float Dt = 1.f / Fps;
		const float Start = P.Skater.Time;
		while (P.Skater.Time - Start < Seconds)
		{
			const bool bWas = P.Possessed();
			P.Frame(Script(P.Skater.Time - Start), FSkateBallActionInput(), Dt);
			Stats.Sample(P, bWas);
		}
		Stats.Finish();
		return Stats;
	}

	void TestPossessionTrap(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.TrapStationaryBall" };
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(400.f, 0.f));
		const float Dt = 1.f / 60.f;
		float TrapTime = -1.f;
		float BallSpeedAtTrap = 0.f;
		while (P.Skater.Time < 4.f && TrapTime < 0.f)
		{
			P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.7f), FSkateBallActionInput(), Dt);
			if (P.Possessed()) { TrapTime = P.Skater.Time; BallSpeedAtTrap = P.Ball.Vel.Size(); }
		}
		// Hold on for a moment and check the ball sits at the feet.
		const FCarryStats S = RunCarry(P, 2.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 0.7f); });
		R.bPassed = TrapTime > 0.f && P.Impulses.empty() && S.Losses == 0 && S.MinAhead > 25.f && S.MaxAhead < 95.f;
		R.Details = Fmt("skating into a resting ball 4 m ahead: trapped after %.2fs (ball %.0f cm/s right after), impulses %d, then ahead %.0f..%.0f cm, losses %d",
			TrapTime, BallSpeedAtTrap, static_cast<int>(P.Impulses.size()), S.MinAhead, S.MaxAhead, S.Losses);
		Out.push_back(R);
	}

	void TestPossessionStraight(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			FSkateTestResult R{ Fmt("Possession.CarryStraight[%s]", SkateTuningPresets::Name(Preset)) };
			FPlaySim P = MakeCarryPlay(Preset);
			const FCarryStats S = RunCarry(P, 8.f, 60.f, [](float Time) { return Stick(FSkateVec2(1.f, 0.f), 1.f, 0.f, Time > 4.f ? 1.f : 0.f); });
			R.bPassed = S.Losses == 0 && S.PossessedFrames == S.Frames && S.MinAhead > 25.f && S.MaxAhead < 100.f
				&& S.MaxAngleDeg < 15.f && S.MeanError < 8.f && P.Impulses.empty();
			R.Details = Fmt("8 s full stick (+RT after 4 s, top %.0f cm/s): losses %d, ball ahead %.0f..%.0f cm, max angle %.1f deg, carry error mean %.1f max %.1f cm, impulses %d",
				P.Skater.State.Velocity.Size(), S.Losses, S.MinAhead, S.MaxAhead, S.MaxAngleDeg, S.MeanError, S.MaxError, static_cast<int>(P.Impulses.size()));
			Out.push_back(R);
		}
	}

	void TestPossessionSharpTurns(std::vector<FSkateTestResult>& Out)
	{
		for (ESkatePreset Preset : AllPresets)
		{
			FSkateTestResult R{ Fmt("Possession.SharpTurns[%s]", SkateTuningPresets::Name(Preset)) };
			FPlaySim P = MakeCarryPlay(Preset);
			RunCarry(P, 2.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 1.f); });
			// Every 0.7 s the stick jumps to a new direction: 90 deg, 180 deg, -135 deg, ... at full deflection.
			const float Jumps[] = { 90.f, 180.f, -135.f, 45.f, 180.f, -90.f, 150.f, -170.f, 100.f, 180.f };
			const FCarryStats S = RunCarry(P, 7.f, 60.f, [&Jumps](float Time)
			{
				float Yaw = 0.f;
				const int Count = static_cast<int>(Time / 0.7f);
				for (int Index = 0; Index <= Count && Index < 10; ++Index) { Yaw += Jumps[Index]; }
				return Stick(FSkateVec2::FromYaw(Yaw * SkateMath::DegToRad), 1.f);
			});
			const FSkateTuning& T = P.T;
			const float BodyLimit = T.BallControl.BodyRadius + T.BallPhysics.Radius * 0.5f - 1.f;
			R.bPassed = S.Losses == 0 && S.MinBodyDistance >= BodyLimit && S.P95AngleDeg < 30.f && S.MaxAngleDeg < 75.f && P.Impulses.empty();
			R.Details = Fmt("10 stick flips (90..180 deg) at full speed: losses %d, angle heading->ball p95 %.1f max %.1f deg, closest to body %.0f cm (limit %.0f), carry error mean %.1f max %.1f cm",
				S.Losses, S.P95AngleDeg, S.MaxAngleDeg, S.MinBodyDistance, BodyLimit, S.MeanError, S.MaxError);
			Out.push_back(R);
		}
	}

	void TestPossessionBrake(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.BrakeKeepsBall" };
		FPlaySim P = MakeCarryPlay(ESkatePreset::Balanced);
		RunCarry(P, 3.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 1.f); });
		const FCarryStats S = RunCarry(P, 2.f, 60.f, [](float) { return Stick(FSkateVec2(), 0.f, 1.f); });
		const float Ahead = (P.Ball.Pos.XY() - P.Skater.Pos).Dot(P.Skater.State.Heading);
		R.bPassed = S.Losses == 0 && P.Skater.State.Velocity.Size() == 0.f && P.Ball.Vel.Size() < 5.f && Ahead > 25.f && Ahead < 60.f;
		R.Details = Fmt("full stop with the ball: losses %d, skater %.0f cm/s, ball %.1f cm/s, ball rests %.0f cm in front", S.Losses,
			P.Skater.State.Velocity.Size(), P.Ball.Vel.Size(), Ahead);
		Out.push_back(R);
	}

	void TestPossessionKick(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.KickFromCarry" };
		float Speeds[3];
		bool bReleased = true;
		bool bNoQuickRetrap = true;
		for (int Index = 0; Index < 3; ++Index)
		{
			const float Dt = 1.f / FrameRates[Index];
			FPlaySim P = MakeCarryPlay(ESkatePreset::Balanced);
			RunCarry(P, 2.f, FrameRates[Index], [](float) { return Stick(FSkateVec2(1.f, 0.f), 0.8f); });
			FSkateBallActionInput Press;
			Press.bKickPressed = true;
			P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.8f), Press, Dt);
			const float Start = P.Skater.Time;
			while (P.Skater.Time - Start < 0.5f - Dt * 0.5f) { P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.8f), FSkateBallActionInput(), Dt); }
			bReleased &= P.Possessed(); // still carried while charging
			FSkateBallActionInput Release;
			Release.bKickReleased = true;
			P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.8f), Release, Dt);
			Speeds[Index] = P.Impulses.empty() ? 0.f : P.Impulses.back().Speed;
			bReleased &= !P.Possessed() && P.Control.Possession.LastLoss == ESkatePossessionLoss::Kick;
			for (int Frame = 0; Frame < static_cast<int>(P.T.BallControl.Possession.AcquireCooldownAfterAction / Dt) - 1; ++Frame)
			{
				P.Frame(Stick(FSkateVec2(1.f, 0.f), 0.8f), FSkateBallActionInput(), Dt);
				bNoQuickRetrap &= !P.Possessed();
			}
		}
		const float Spread = SkateMath::Max(RelDiff(Speeds[0], Speeds[2]), RelDiff(Speeds[1], Speeds[2]));
		R.bPassed = bReleased && bNoQuickRetrap && Speeds[2] > 1500.f && Spread < 0.05f;
		R.Details = Fmt("0.5 s charge while carrying at 80%% stick: carried during charge + released by the kick=%d, no re-trap during cooldown=%d, shot speed 30/60/120fps = %.0f/%.0f/%.0f cm/s (spread %.1f%%)",
			bReleased ? 1 : 0, bNoQuickRetrap ? 1 : 0, Speeds[0], Speeds[1], Speeds[2], Spread * 100.f);
		Out.push_back(R);
	}

	void TestPossessionKnockOn(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.KnockOnAndRecover" };
		FPlaySim P = MakeCarryPlay(ESkatePreset::Balanced);
		const float Dt = 1.f / 60.f;
		RunCarry(P, 2.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 1.f); });
		FSkateBallActionInput Push;
		Push.bPushPressed = true;
		P.Frame(Stick(FSkateVec2(1.f, 0.f), 1.f), Push, Dt);
		const bool bPushed = !P.Possessed() && P.CountKind(ESkateImpulseKind::Push) == 1;
		const float PushTime = P.Skater.Time;
		float MaxGap = 0.f;
		float RetrapTime = -1.f;
		while (P.Skater.Time - PushTime < 4.f && RetrapTime < 0.f)
		{
			P.Frame(Stick(FSkateVec2(1.f, 0.f), 1.f, 0.f, 1.f), FSkateBallActionInput(), Dt);
			MaxGap = SkateMath::Max(MaxGap, P.Ball.Pos.X - P.Skater.Pos.X);
			if (P.Possessed()) { RetrapTime = P.Skater.Time - PushTime; }
		}
		R.bPassed = bPushed && RetrapTime >= P.T.BallControl.Possession.AcquireCooldownAfterAction && RetrapTime < 3.f;
		R.Details = Fmt("A at full speed: released=%d, ball up to %.0f cm ahead, caught again with RT after %.2fs (cooldown %.2fs)",
			bPushed ? 1 : 0, MaxGap, RetrapTime, P.T.BallControl.Possession.AcquireCooldownAfterAction);
		Out.push_back(R);
	}

	void TestPossessionBoard(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.BoardAndObstacle" };
		const float Dt = 1.f / 60.f;

		// 1) Into the board, then turn away along it: the ball never goes through the board.
		FPlaySim P = MakeCarryPlay(ESkatePreset::Balanced);
		P.WallX = 700.f;
		float MaxBallX = 0.f;
		while (P.Skater.Time < 6.f)
		{
			const FSkateVec2 Dir = P.Skater.Time < 3.f ? FSkateVec2(1.f, 0.f) : FSkateVec2(-0.3f, 1.f);
			P.Frame(Stick(Dir, 0.8f), FSkateBallActionInput(), Dt);
			MaxBallX = SkateMath::Max(MaxBallX, P.Ball.Pos.X + P.T.BallPhysics.Radius);
		}
		const bool bBoardOk = MaxBallX <= P.WallX + 0.01f;
		const bool bKeptAlongBoard = P.Possessed();

		// 2) An obstacle (post, R 30 cm) catches the ball while the skater slides past it: possession is lost.
		FPlaySim Q = MakeCarryPlay(ESkatePreset::Balanced);
		const FSkateVec2 Post(500.f, 6.f); // dead ahead of the carried ball
		const float PostRadius = 30.f;
		bool bLost = false;
		float MinPostGap = 1e9f;
		while (Q.Skater.Time < 4.f && !bLost)
		{
			Q.Frame(Stick(FSkateVec2(1.f, 0.f), 0.7f), FSkateBallActionInput(), Dt);
			// Ball vs post: push out, reflect the normal velocity (as the physics engine would).
			FSkateVec2 Rel = Q.Ball.Pos.XY() - Post;
			const float Dist = Rel.Size();
			const float MinDist = PostRadius + Q.T.BallPhysics.Radius;
			if (Dist < MinDist)
			{
				const FSkateVec2 N = Rel.GetSafeNormal();
				const FSkateVec2 Fixed = Post + N * MinDist;
				Q.Ball.Pos = FSkateVec3(Fixed, Q.Ball.Pos.Z);
				const FSkateVec2 V = Q.Ball.Vel.XY();
				const float Vn = V.Dot(N);
				if (Vn < 0.f) { Q.Ball.Vel = FSkateVec3(V - N * (Vn * 1.6f), Q.Ball.Vel.Z); }
			}
			MinPostGap = SkateMath::Min(MinPostGap, (Q.Ball.Pos.XY() - Post).Size() - MinDist);
			bLost = !Q.Possessed() && Q.Control.Possession.LastLoss == ESkatePossessionLoss::Blocked;
		}
		R.bPassed = bBoardOk && bLost && MinPostGap > -0.5f;
		R.Details = Fmt("board: ball never through it=%d (max x %.1f / %.0f), still carried after turning along it=%d | post in the ball's path: ball taken off the feet=%d (%s) after %.2fs",
			bBoardOk ? 1 : 0, MaxBallX, P.WallX, bKeptAlongBoard ? 1 : 0, bLost ? 1 : 0, SkatePossessionLossName(Q.Control.Possession.LastLoss), Q.Skater.Time);
		Out.push_back(R);
	}

	void TestPossessionTrapLimits(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.TrapLimits" };
		const float Dt = 1.f / 60.f;
		auto Incoming = [&](float Speed)
		{
			FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(300.f, 0.f));
			P.Ball.Vel = FSkateVec3(-Speed, 0.f, 0.f);
			bool bTrapped = false;
			for (int Index = 0; Index < 60; ++Index)
			{
				P.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt);
				bTrapped |= P.Possessed();
			}
			return bTrapped;
		};
		const bool bSlow = Incoming(450.f);
		const bool bFast = Incoming(1600.f);
		// Ball in the reach zone but 40 cm in the air.
		FPlaySim High = MakePlay(ESkatePreset::Balanced, FSkateVec2(42.f, 0.f));
		High.Ball.Pos.Z += 40.f;
		FSkateContactQuery Q = High.Query(Stick(FSkateVec2(), 0.f));
		FSkateBallCarry Carry;
		FSkateBallControl::Update(High.T.BallControl, Q, FSkateBallActionInput(), Dt, High.Control, High.Report, &Carry);
		const bool bHighTrapped = High.Possessed();
		// Behind the skater.
		FPlaySim Behind = MakePlay(ESkatePreset::Balanced, FSkateVec2(-40.f, 0.f));
		for (int Index = 0; Index < 30; ++Index) { Behind.Frame(Stick(FSkateVec2(), 0.f), FSkateBallActionInput(), Dt); }
		R.bPassed = bSlow && !bFast && !bHighTrapped && !Behind.Possessed();
		R.Details = Fmt("incoming 450 cm/s trapped=%d, incoming 1600 cm/s trapped=%d (must bounce), airborne 40 cm trapped=%d, behind the back trapped=%d",
			bSlow ? 1 : 0, bFast ? 1 : 0, bHighTrapped ? 1 : 0, Behind.Possessed() ? 1 : 0);
		Out.push_back(R);
	}

	void TestPossessionFps(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.FpsIndependence" };
		auto Weave = [](float Time) { return Stick(FSkateVec2::FromYaw(1.1f * std::sin(Time * 2.4f)), Time < 4.f ? 1.f : 0.6f); };
		FCarryStats S[3];
		for (int Index = 0; Index < 3; ++Index)
		{
			FPlaySim P = MakeCarryPlay(ESkatePreset::Balanced);
			S[Index] = RunCarry(P, 8.f, FrameRates[Index], Weave);
		}
		bool bOk = true;
		for (const FCarryStats& Stat : S) { bOk &= Stat.Losses == 0 && Stat.MaxAngleDeg < 45.f; }
		bOk &= SkateMath::Abs(S[0].MeanError - S[2].MeanError) < 5.f && SkateMath::Abs(S[0].P95AngleDeg - S[2].P95AngleDeg) < 8.f;
		R.bPassed = bOk;
		R.Details = Fmt("8 s weave with the ball, 30/60/120fps: losses %d/%d/%d, angle p95 %.1f/%.1f/%.1f deg, carry error mean %.1f/%.1f/%.1f cm",
			S[0].Losses, S[1].Losses, S[2].Losses, S[0].P95AngleDeg, S[1].P95AngleDeg, S[2].P95AngleDeg, S[0].MeanError, S[1].MeanError, S[2].MeanError);
		Out.push_back(R);
	}

	void TestPossessionDribbleTaps(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.DribbleTapsFollowSpeed" };
		FPlaySim Still = MakeCarryPlay(ESkatePreset::Balanced);
		RunCarry(Still, 3.f, 60.f, [](float) { return Stick(FSkateVec2(), 0.f); });
		FPlaySim Fast = MakeCarryPlay(ESkatePreset::Balanced);
		RunCarry(Fast, 2.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 1.f); });
		const int Before = Fast.Control.Possession.TouchPulseCount;
		const FCarryStats S = RunCarry(Fast, 4.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 1.f); });
		const int Taps = Fast.Control.Possession.TouchPulseCount - Before;
		const float Expected = 4.f * Fast.T.BallControl.Possession.DribbleCadenceFast;
		R.bPassed = Still.Control.Possession.TouchPulseCount == 0 && SkateMath::Abs(static_cast<float>(Taps) - Expected) <= 2.f && S.MaxAhead - S.MinAhead > 8.f;
		R.Details = Fmt("standing: %d taps (ball still at the feet); full speed 4 s: %d taps (cadence %.1f/s), ball swings %.0f..%.0f cm ahead",
			Still.Control.Possession.TouchPulseCount, Taps, Fast.T.BallControl.Possession.DribbleCadenceFast, S.MinAhead, S.MaxAhead);
		Out.push_back(R);
	}

	void TestPossessionReverse(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R{ "Possession.ReverseStopWithBall" };
		FPlaySim P = MakeCarryPlay(ESkatePreset::Balanced);
		RunCarry(P, 3.f, 60.f, [](float) { return Stick(FSkateVec2(1.f, 0.f), 1.f); });
		const FCarryStats S = RunCarry(P, 3.f, 60.f, [](float) { return Stick(FSkateVec2(-1.f, 0.f), 1.f); });
		const FSkateTuning& T = P.T;
		const float BodyLimit = T.BallControl.BodyRadius + T.BallPhysics.Radius * 0.5f - 1.f;
		// Pivot on the spot with the ball: it must travel AROUND the skater, not through the legs.
		FPlaySim Pivot = MakeCarryPlay(ESkatePreset::Balanced);
		const FCarryStats PS = RunCarry(Pivot, 1.5f, 60.f, [](float) { return Stick(FSkateVec2(-1.f, 0.f), 0.4f); });
		const bool bPivotOk = PS.Losses == 0 && PS.MinBodyDistance >= BodyLimit && Pivot.Ball.Pos.X < Pivot.Skater.Pos.X - 25.f;
		R.bPassed = S.Losses == 0 && P.Skater.State.Velocity.X < -0.8f * T.Movement.MaxSpeed && P.Ball.Pos.X < P.Skater.Pos.X && S.MinBodyDistance >= BodyLimit && bPivotOk;
		R.Details = Fmt("full speed then stick back: losses %d, skater now %.0f cm/s the other way, ball in front again=%d, closest to body %.0f cm | pivot on the spot: losses %d, closest to body %.0f cm (limit %.0f), ball in front after=%d",
			S.Losses, -P.Skater.State.Velocity.X, P.Ball.Pos.X < P.Skater.Pos.X ? 1 : 0, S.MinBodyDistance, PS.Losses, PS.MinBodyDistance, BodyLimit, bPivotOk ? 1 : 0);
		Out.push_back(R);
	}
}

std::vector<FSkateTestResult> RunSkateCoreTests()
{
	using namespace SkateCoreTestsDetail;
	std::vector<FSkateTestResult> Results;
	TestInputDeadZone(Results);
	TestAccelerationAndFps(Results);
	TestPartialStick(Results);
	TestGlide(Results);
	TestBrakeStop(Results);
	TestAnalogBrake(Results);
	TestBrakeNearZero(Results);
	TestHighSpeedTurn(Results);
	TestLowSpeedPivot(Results);
	TestReverseNoInstantFlip(Results);
	TestCourseFpsIndependence(Results);
	TestContactGating(Results);
	TestNoActionOutOfReach(Results);
	TestKickBufferExpires(Results);
	TestKickFpsAndCharge(Results);
	TestDribbleStraight(Results);
	TestDribbleTurnKeepsBallInertia(Results);
	TestBrakeThenRecover(Results);
	TestSingleImpulseUnderMashing(Results);
	TestBodyBlock(Results);
	TestDisabledInteraction(Results);
	TestPossessionTrap(Results);
	TestPossessionStraight(Results);
	TestPossessionSharpTurns(Results);
	TestPossessionReverse(Results);
	TestPossessionBrake(Results);
	TestPossessionKick(Results);
	TestPossessionKnockOn(Results);
	TestPossessionBoard(Results);
	TestPossessionTrapLimits(Results);
	TestPossessionFps(Results);
	TestPossessionDribbleTaps(Results);
	return Results;
}

std::string SkateCoreMetricsReport()
{
	using namespace SkateCoreTestsDetail;
	std::string Report = "preset         fps | t95%   v@0.1s | stop t  stop d | glide t glide d | min arc R @max\n";
	for (ESkatePreset Preset : AllPresets)
	{
		const FSkateTuning T = SkateTuningPresets::Make(Preset);
		for (float Fps : FrameRates)
		{
			const FBasicMetrics M = MeasureBasics(T, Fps);
			const float MinRadius = T.Movement.MaxSpeed * T.Movement.MaxSpeed / T.Movement.MaxLateralAccel;
			Report += Fmt("%-13s %4.0f | %.2fs %4.0f    | %.2fs %4.0fcm | %.2fs %5.0fcm | %4.0f cm\n",
				SkateTuningPresets::Name(Preset), Fps, M.TimeTo95, M.SpeedAt100ms, M.StopTime, M.StopDistance, M.GlideTime, M.GlideDistance, MinRadius);
		}
	}
	return Report;
}

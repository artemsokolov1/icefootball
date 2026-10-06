#include "SkateCoreTests.h"

#include "../Core/SkateBallControl.h"
#include "../Core/SkateInput.h"
#include "../Core/SkateModel.h"
#include "../Core/SkateTuningPresets.h"

#include <cstdarg>
#include <cstdio>
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
		void Step(const FSkateBallPhysicsTuning& T, float Dt)
		{
			FSkateVec2 V = Vel.XY();
			V *= std::exp(-T.LinearDamping * Dt);
			const float S = V.Size();
			const float NewS = SkateMath::Max(0.f, S - T.RollingResistance * Dt);
			V = S > 0.f ? V * (NewS / S) : V;
			if (V.Size() < T.StopSpeed) { V = FSkateVec2(); }
			Vel = FSkateVec3(V, 0.f);
			Pos = Pos + Vel * Dt;
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

		// One game frame: skater moves, contact is evaluated, impulse applied, ball steps.
		FSkateBallImpulse Frame(const FSkateMoveInput& In, const FSkateBallActionInput& Actions, float Dt)
		{
			Skater.Step(T.Movement, In, Dt);
			FSkateBallImpulse Imp = FSkateBallControl::Update(T.BallControl, Query(In), Actions, Dt, Control, Report);
			int Count = 0;
			if (Imp.IsValid())
			{
				Ball.Vel = Imp.NewBallVelocity;
				Impulses.push_back({ Skater.Time, Imp.Kind, Imp.NewBallVelocity.Size() });
				++Count;
			}
			MaxImpulsesInOneFrame = Count > MaxImpulsesInOneFrame ? Count : MaxImpulsesInOneFrame;
			Ball.Step(T.BallPhysics, Dt);
			return Imp;
		}

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
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(70.f, 0.f));
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
		FSkateTestResult R{ "Dribble.Straight" };
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
		FSkateTestResult R{ "Dribble.TurnBallKeepsInertia" };
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(70.f, 0.f));
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
		FSkateTestResult R{ "Dribble.BrakeThenRecover" };
		FPlaySim P = MakePlay(ESkatePreset::Balanced, FSkateVec2(70.f, 0.f));
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

#include "SkateCourseTests.h"

#include "../Core/SkateBallControl.h"
#include "../Core/SkateModel.h"
#include "../Core/SkateTuningPresets.h"

#include <cstdarg>
#include <cstdio>

namespace SkateCourseTestsDetail
{
	// Mirrors the FSkateArenaLayout defaults (SkateArena.h).
	constexpr float AccelStartX = -2000.f;
	constexpr float AccelLaneY = -1100.f;
	constexpr float StopZoneCenterX = -50.f;
	constexpr float StopZoneLength = 400.f;
	constexpr float SlalomY = -250.f;
	constexpr float SlalomStartX = -2000.f;
	constexpr float SlalomSpacing = 400.f;
	constexpr int SlalomCones = 7;
	constexpr float TurnCircleRadius = 450.f;
	constexpr float FigureEightRadius = 400.f;
	// Skater capsule radius + cone base radius: closer than this = the skater hits the cone.
	constexpr float ConeClearance = 30.f + 15.f;

	const ESkatePreset Presets[] = { ESkatePreset::Responsive, ESkatePreset::Balanced, ESkatePreset::Inertial };

	std::string Fmt(const char* Format, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Format);
		std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);
		return std::string(Buffer);
	}

	struct FPolyline
	{
		std::vector<FSkateVec2> Points;

		// Closest point parameter (segment index + fraction) searched forward from Hint.
		float Project(const FSkateVec2& P, float Hint, float* OutDistance) const
		{
			float Best = Hint;
			float BestDist = 1e9f;
			const int Start = static_cast<int>(Hint);
			const int End = Start + 12 < static_cast<int>(Points.size()) - 1 ? Start + 12 : static_cast<int>(Points.size()) - 1;
			for (int Index = Start; Index < End; ++Index)
			{
				const FSkateVec2 A = Points[Index];
				const FSkateVec2 B = Points[Index + 1];
				const FSkateVec2 Ab = B - A;
				const float T = SkateMath::Clamp01((P - A).Dot(Ab) / SkateMath::Max(Ab.SizeSquared(), 1e-3f));
				const float D = (A + Ab * T - P).Size();
				if (D < BestDist)
				{
					BestDist = D;
					Best = static_cast<float>(Index) + T;
				}
			}
			if (OutDistance) { *OutDistance = BestDist; }
			return Best;
		}

		FSkateVec2 At(float Param) const
		{
			const int Last = static_cast<int>(Points.size()) - 1;
			if (Param >= static_cast<float>(Last)) { return Points[Last]; }
			const int Index = static_cast<int>(Param);
			const float T = Param - static_cast<float>(Index);
			return Points[Index] + (Points[Index + 1] - Points[Index]) * T;
		}

		// Moves Distance cm forward along the polyline from Param.
		float Advance(float Param, float Distance) const
		{
			const int Last = static_cast<int>(Points.size()) - 1;
			while (Distance > 0.f && Param < static_cast<float>(Last))
			{
				const int Index = static_cast<int>(Param);
				const float SegLen = (Points[Index + 1] - Points[Index]).Size();
				const float Remaining = SegLen * (1.f - (Param - static_cast<float>(Index)));
				if (Distance < Remaining)
				{
					return Param + Distance / SkateMath::Max(SegLen, 1e-3f);
				}
				Distance -= Remaining;
				Param = static_cast<float>(Index + 1);
			}
			return SkateMath::Min(Param, static_cast<float>(Last));
		}
	};

	void AddArc(FPolyline& Line, const FSkateVec2& Centre, float Radius, float StartAngle, float Sweep, int Steps)
	{
		for (int Index = 0; Index <= Steps; ++Index)
		{
			const float A = StartAngle + Sweep * static_cast<float>(Index) / static_cast<float>(Steps);
			Line.Points.push_back(Centre + FSkateVec2(std::cos(A), std::sin(A)) * Radius);
		}
	}

	struct FDriveResult
	{
		bool bFinished = false;
		float Time = 0.f;
		float MaxDeviation = 0.f;
		float AvgSpeed = 0.f;
		std::vector<FSkateVec2> Trace;
	};

	// Pure pursuit: aim at a point ahead on the path, constant stick magnitude.
	FDriveResult Drive(const FSkateTuning& T, const FPolyline& Path, float StickMag, float Fps, float MaxTime, float StartDeviationGrace = 1.5f)
	{
		FDriveResult R;
		FSkateMoveState State;
		State.Heading = (Path.Points[1] - Path.Points[0]).GetSafeNormal();
		FSkateVec2 Pos = Path.Points[0];
		float Param = 0.f;
		float Time = 0.f;
		float Distance = 0.f;
		const float Dt = 1.f / Fps;
		const float Last = static_cast<float>(Path.Points.size() - 1);
		while (Time < MaxTime)
		{
			float Deviation = 0.f;
			Param = Path.Project(Pos, Param, &Deviation);
			if (Time > StartDeviationGrace) { R.MaxDeviation = SkateMath::Max(R.MaxDeviation, Deviation); }
			if (Param >= Last - 0.05f) { R.bFinished = true; break; }
			const float Speed = State.Velocity.Size();
			const FSkateVec2 Aim = Path.At(Path.Advance(Param, 80.f + 0.2f * Speed));
			FSkateMoveInput In;
			In.Direction = (Aim - Pos).GetSafeNormal(State.Heading);
			In.Magnitude = StickMag;
			FSkateModel::Step(T.Movement, In, Dt, State);
			Pos += State.Velocity * Dt;
			Distance += State.Velocity.Size() * Dt;
			Time += Dt;
			R.Trace.push_back(Pos);
		}
		R.Time = Time;
		R.AvgSpeed = Distance / SkateMath::Max(Time, 1e-3f);
		return R;
	}

	FPolyline SlalomPath()
	{
		FPolyline Line;
		Line.Points.push_back(FSkateVec2(SlalomStartX - 500.f, SlalomY));
		for (int Index = 0; Index < SlalomCones; ++Index)
		{
			const float Side = (Index % 2 == 0) ? 1.f : -1.f;
			Line.Points.push_back(FSkateVec2(SlalomStartX + static_cast<float>(Index) * SlalomSpacing, SlalomY + Side * 120.f));
		}
		Line.Points.push_back(FSkateVec2(SlalomStartX + SlalomCones * SlalomSpacing + 300.f, SlalomY));
		// Densify so the pursuit point moves smoothly.
		FPolyline Dense;
		for (size_t Index = 0; Index + 1 < Line.Points.size(); ++Index)
		{
			for (int Step = 0; Step < 8; ++Step)
			{
				Dense.Points.push_back(Line.Points[Index] + (Line.Points[Index + 1] - Line.Points[Index]) * (static_cast<float>(Step) / 8.f));
			}
		}
		Dense.Points.push_back(Line.Points.back());
		return Dense;
	}

	FPolyline FigureEightPath(int Laps)
	{
		// Left loop counter-clockwise, right loop clockwise, crossing at the centre.
		const FSkateVec2 Left(-FigureEightRadius, 0.f);
		const FSkateVec2 Right(FigureEightRadius, 0.f);
		FPolyline Line;
		for (int Lap = 0; Lap < Laps; ++Lap)
		{
			AddArc(Line, Left, FigureEightRadius, 0.f, 2.f * SkateMath::Pi, 64);
			AddArc(Line, Right, FigureEightRadius, SkateMath::Pi, -2.f * SkateMath::Pi, 64);
		}
		return Line;
	}

	void TestStopZone(std::vector<FSkateTestResult>& Out)
	{
		const float ZoneStart = StopZoneCenterX - StopZoneLength * 0.5f;
		const float ZoneEnd = StopZoneCenterX + StopZoneLength * 0.5f;
		for (ESkatePreset Preset : Presets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R(Fmt("Course.StopZone[%s]", SkateTuningPresets::Name(Preset)));
			bool bOk = true;
			std::string Info;
			for (float Boost : { 0.f, 1.f })
			{
				FSkateMoveState State;
				FSkateVec2 Pos(AccelStartX, AccelLaneY);
				float EntrySpeed = 0.f;
				bool bBraking = false;
				for (int Frame = 0; Frame < 60 * 20; ++Frame)
				{
					if (!bBraking && Pos.X >= ZoneStart) { bBraking = true; EntrySpeed = State.Velocity.Size(); }
					FSkateMoveInput In;
					if (!bBraking) { In.Direction = FSkateVec2(1.f, 0.f); In.Magnitude = 1.f; In.Boost = Boost; }
					else { In.Brake = 1.f; }
					FSkateModel::Step(T.Movement, In, 1.f / 60.f, State);
					Pos += State.Velocity * (1.f / 60.f);
					if (bBraking && State.Velocity.Size() == 0.f) { break; }
				}
				const bool bInside = Pos.X >= ZoneStart && Pos.X <= ZoneEnd;
				bOk &= bInside;
				Info += Fmt("%s: enter %.0f cm/s, stop at x=%.0f (zone %.0f..%.0f) %s; ", Boost > 0.f ? "boost" : "normal",
					EntrySpeed, Pos.X, ZoneStart, ZoneEnd, bInside ? "inside" : "OUTSIDE");
			}
			R.bPassed = bOk;
			R.Details = Info;
			Out.push_back(R);
		}
	}

	float MinConeDistance(const FDriveResult& Result)
	{
		float MinDist = 1e9f;
		for (const FSkateVec2& P : Result.Trace)
		{
			for (int Index = 0; Index < SlalomCones; ++Index)
			{
				MinDist = SkateMath::Min(MinDist, (P - FSkateVec2(SlalomStartX + static_cast<float>(Index) * SlalomSpacing, SlalomY)).Size());
			}
		}
		return MinDist;
	}

	void TestSlalom(std::vector<FSkateTestResult>& Out)
	{
		const FPolyline Path = SlalomPath();
		for (ESkatePreset Preset : Presets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R(Fmt("Course.Slalom[%s]", SkateTuningPresets::Name(Preset)));
			std::string Info;
			bool bModerateClean = false;
			for (float Mag : { 0.6f, 0.8f, 1.f })
			{
				const FDriveResult D = Drive(T, Path, Mag, 60.f, 20.f);
				const float Clearance = MinConeDistance(D);
				const bool bClean = D.bFinished && Clearance >= ConeClearance;
				if (Mag == 0.8f) { bModerateClean = bClean; }
				Info += Fmt("stick %.1f: %.1fs avg %.0f cm/s, closest cone %.0f cm %s; ", Mag, D.Time, D.AvgSpeed, Clearance, bClean ? "clean" : "HIT");
			}
			// A scripted driver at 80% stick must get through; full speed is allowed to need braking.
			R.bPassed = bModerateClean;
			R.Details = Info;
			Out.push_back(R);
		}
	}

	void TestCircleAndFigureEight(std::vector<FSkateTestResult>& Out)
	{
		FPolyline Circle;
		AddArc(Circle, FSkateVec2(), TurnCircleRadius, -SkateMath::Pi * 0.5f, 4.f * SkateMath::Pi, 128);
		const FPolyline Eight = FigureEightPath(2);
		for (ESkatePreset Preset : Presets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R(Fmt("Course.CircleAndEight[%s]", SkateTuningPresets::Name(Preset)));
			const FDriveResult C = Drive(T, Circle, 1.f, 60.f, 30.f, 2.5f);
			const FDriveResult E = Drive(T, Eight, 1.f, 60.f, 40.f, 2.5f);
			const FDriveResult E30 = Drive(T, Eight, 1.f, 30.f, 40.f, 2.5f);
			// Full stick, no braking: the arcs must be skateable at speed with small deviation.
			R.bPassed = C.bFinished && E.bFinished && C.MaxDeviation < 70.f && E.MaxDeviation < 110.f
				&& SkateMath::Abs(E30.Time - E.Time) < 0.05f * E.Time;
			R.Details = Fmt("circle R%.0f: 2 laps %.1fs avg %.0f cm/s max dev %.0f cm | figure 8 R%.0f: 2 laps %.1fs avg %.0f cm/s max dev %.0f cm (30fps: %.1fs, dev %.0f)",
				TurnCircleRadius, C.Time, C.AvgSpeed, C.MaxDeviation, FigureEightRadius, E.Time, E.AvgSpeed, E.MaxDeviation, E30.Time, E30.MaxDeviation);
			Out.push_back(R);
		}
	}
}

namespace SkateCourseTestsDetail
{
	// Slalom with the ball carried (possession). Pure pursuit driver, simple ball that follows the carry steering.
	void TestSlalomWithBall(std::vector<FSkateTestResult>& Out)
	{
		const FPolyline Path = SlalomPath();
		const float Dt = 1.f / 60.f;
		for (ESkatePreset Preset : Presets)
		{
			const FSkateTuning T = SkateTuningPresets::Make(Preset);
			FSkateTestResult R(Fmt("Course.SlalomWithBall[%s]", SkateTuningPresets::Name(Preset)));
			std::string Info;
			bool bModerateOk = false;
			for (float Mag : { 0.6f, 0.8f, 1.f })
			{
				FSkateMoveState State;
				State.Heading = FSkateVec2(1.f, 0.f);
				FSkateVec2 Pos = Path.Points[0];
				FSkateVec3 BallPos(Pos + FSkateVec2(42.f, 0.f), T.BallPhysics.Radius);
				FSkateVec3 BallVel;
				FSkateBallControlState Control;
				FSkateContactReport Report;
				float Param = 0.f;
				float Time = 0.f;
				int Losses = 0;
				float MinBallCone = 1e9f;
				const float Last = static_cast<float>(Path.Points.size() - 1);
				while (Time < 20.f && Param < Last - 0.05f)
				{
					Param = Path.Project(Pos, Param, nullptr);
					const FSkateVec2 Aim = Path.At(Path.Advance(Param, 80.f + 0.2f * State.Velocity.Size()));
					FSkateMoveInput In;
					In.Direction = (Aim - Pos).GetSafeNormal(State.Heading);
					In.Magnitude = Mag;
					FSkateModel::Step(T.Movement, In, Dt, State);
					Pos += State.Velocity * Dt;

					FSkateContactQuery Q;
					Q.SkaterPos = Pos;
					Q.SkaterVel = State.Velocity;
					Q.Heading = State.Heading;
					Q.SkaterMaxSpeed = T.Movement.MaxSpeed;
					Q.StickDir = In.Direction;
					Q.StickMag = In.Magnitude;
					Q.bHasBall = true;
					Q.BallPos = BallPos;
					Q.BallVel = BallVel;
					Q.BallRadius = T.BallPhysics.Radius;
					const bool bWas = Control.Possession.bPossessed;
					FSkateBallCarry Carry;
					const FSkateBallImpulse Imp = FSkateBallControl::Update(T.BallControl, Q, FSkateBallActionInput(), Dt, Control, Report, &Carry);
					if (Imp.IsValid()) { BallVel = Imp.NewBallVelocity; }
					else if (Carry.bActive) { BallVel = Carry.Velocity; }
					BallPos = BallPos + BallVel * Dt;
					Losses += (bWas && !Control.Possession.bPossessed) ? 1 : 0;
					for (int Index = 0; Index < SlalomCones; ++Index)
					{
						MinBallCone = SkateMath::Min(MinBallCone, (BallPos.XY() - FSkateVec2(SlalomStartX + static_cast<float>(Index) * SlalomSpacing, SlalomY)).Size());
					}
					Time += Dt;
				}
				// Ball (r 11) must clear the cone base (r 15).
				const bool bClean = Losses == 0 && Control.Possession.bPossessed && MinBallCone > 15.f + T.BallPhysics.Radius;
				if (Mag == 0.8f) { bModerateOk = bClean; }
				Info += Fmt("stick %.1f: %.1fs, losses %d, ball closest to a cone %.0f cm %s; ", Mag, Time, Losses, MinBallCone, bClean ? "clean" : "TOUCHES/LOST");
			}
			R.bPassed = bModerateOk;
			R.Details = Info;
			Out.push_back(R);
		}
	}
}

void RunSkateCourseTests(std::vector<FSkateTestResult>& Out)
{
	using namespace SkateCourseTestsDetail;
	TestStopZone(Out);
	TestSlalom(Out);
	TestCircleAndFigureEight(Out);
	TestSlalomWithBall(Out);
}

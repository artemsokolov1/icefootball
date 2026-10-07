#include "SkateTeamTests.h"

#include "../Core/SkateBallControl.h"
#include "../Core/SkateKeeper.h"
#include "../Core/SkateModel.h"
#include "../Core/SkateHit.h"
#include "../Core/SkateSkaterAI.h"
#include "../Core/SkateTuningPresets.h"

#include <cstdarg>
#include <cstdio>
#include <functional>

namespace SkateTeamTestsDetail
{
	std::string Fmt(const char* Format, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Format);
		std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);
		return std::string(Buffer);
	}

	constexpr int NoHolder = -1;
	constexpr int KeeperHolder = 2;
	constexpr float Gravity = 980.f;
	constexpr float SkaterRadius = 30.f;

	struct FSkater
	{
		FSkateMoveState State;
		FSkateVec2 Pos;
		FSkateBallControlState Control;
		FSkateContactReport Report;
		/** Driven by FSkateSkaterAI (attacks Goal). Team[] decides who is a teammate. */
		bool bAI = false;
		FSkateSkaterBrain Brain;
		ESkateSkaterMode Mode = ESkateSkaterMode::Wait;
		/** Stun left after a body check (s), check-button window left (s). */
		float Stun = 0.f;
		float CheckLeft = 0.f;
		int Hits = 0;
		int Impulses = 0;
		int BodyBlocks = 0;
	};

	enum class EShotResult { Open, Goal, Wide, Saved };

	const char* ShotResultName(EShotResult R)
	{
		switch (R)
		{
		case EShotResult::Open: return "open";
		case EShotResult::Goal: return "GOAL";
		case EShotResult::Wide: return "wide";
		case EShotResult::Saved: return "saved";
		}
		return "?";
	}

	// Two skaters, one ball, optional keeper, same per-frame order as in UE:
	// skaters move -> keeper (pre-physics) -> each skater's ball control -> ball physics step.
	struct FTeamSim
	{
		FSkateTuning T;
		FSkater S[2];
		FSkateVec3 BallPos;
		FSkateVec3 BallVel;
		int Holder = NoHolder;
		float BallSinceImpulse = 100.f;
		ESkateImpulseKind LastKind = ESkateImpulseKind::None;
		int LastSource = -1;
		int DoubleImpulses = 0;
		int Controlled = 0;
		int Team[2] = { 0, 0 };
		/** AI role: the chaser goes for the ball, the other one defends / supports. */
		bool Chaser[2] = { true, true };

		bool bKeeper = false;
		FSkateKeeperTuning KT;
		FSkateKeeperState K;
		FSkateGoalFrame Goal;
		int Parries = 0;
		int Catches = 0;
		int Releases = 0;

		EShotResult Result = EShotResult::Open;
		float Time = 0.f;

		explicit FTeamSim(ESkatePreset Preset = ESkatePreset::Balanced)
		{
			T = SkateTuningPresets::Make(Preset);
			Goal.Center = FSkateVec2(2380.f, 600.f);
			Goal.Normal = FSkateVec2(-1.f, 0.f);
			Goal.HalfWidth = 260.f;
			Goal.Height = 180.f;
			BallPos = FSkateVec3(0.f, 0.f, T.BallPhysics.Radius);
		}

		void Place(int Index, const FSkateVec2& Pos, const FSkateVec2& Heading)
		{
			S[Index].Pos = Pos;
			FSkateModel::Reset(S[Index].State, Heading);
		}

		void Impulse(ESkateImpulseKind Kind, int Source)
		{
			if (BallSinceImpulse < FSkateBallControl::MinImpulseGap)
			{
				++DoubleImpulses;
			}
			BallSinceImpulse = 0.f;
			LastKind = Kind;
			LastSource = Source;
		}

		FSkateContactQuery Query(int Index, const FSkateMoveInput& In) const
		{
			FSkateContactQuery Q;
			Q.SkaterPos = S[Index].Pos;
			Q.SkaterVel = S[Index].State.Velocity;
			Q.Heading = S[Index].State.Heading;
			Q.SkaterMaxSpeed = T.Movement.MaxSpeed;
			Q.StickDir = In.Direction;
			Q.StickMag = In.Magnitude;
			Q.bHasBall = true;
			Q.bStunned = S[Index].Stun > 0.f;
			Q.BallPos = BallPos;
			Q.BallVel = BallVel;
			Q.BallRadius = T.BallPhysics.Radius;
			Q.bBallHeldByOther = Holder != NoHolder && Holder != Index;
			// Same rule as USkateBallControlComponent: an opponent's ball may be taken after the protection time.
			Q.bStealAllowed = Q.bBallHeldByOther && Holder != KeeperHolder && Team[Holder] != Team[Index]
				&& S[Holder].Control.Possession.TimeHeld >= T.BallControl.Possession.StealProtectTime;
			Q.BallTimeSinceImpulse = BallSinceImpulse;
			Q.bIncomingPass = LastKind == ESkateImpulseKind::Push && LastSource != Index;
			return Q;
		}

		FSkateSkaterView AIView(int Index) const
		{
			FSkateSkaterView View;
			View.Pos = S[Index].Pos;
			View.Vel = S[Index].State.Velocity;
			View.Heading = S[Index].State.Heading;
			View.bBallValid = true;
			View.BallPos = BallPos.XY();
			View.BallVel = BallVel.XY();
			if (Holder == Index) { View.BallOwner = ESkateBallOwner::Me; }
			else if (Holder == KeeperHolder) { View.BallOwner = ESkateBallOwner::Keeper; }
			else if (Holder != NoHolder) { View.BallOwner = Team[Holder] == Team[Index] ? ESkateBallOwner::Teammate : ESkateBallOwner::Opponent; }
			View.AttackGoal = Goal.Center;
			View.OwnGoal = FSkateVec2(-Goal.Center.X, Goal.Center.Y);
			View.GoalHalfWidth = Goal.HalfWidth;
			View.bBallIsMyPass = LastKind == ESkateImpulseKind::Push && LastSource == Index;
			View.bBallIsPassToMe = LastKind == ESkateImpulseKind::Push && LastSource != Index;
			View.bChaser = Chaser[Index];
			View.bMateValid = Team[1 - Index] == Team[Index];
			View.MatePos = S[1 - Index].Pos;
			View.MateVel = S[1 - Index].State.Velocity;
			View.bThreatValid = Team[1 - Index] != Team[Index];
			View.ThreatPos = S[1 - Index].Pos;
			return View;
		}

		void Frame(const FSkateMoveInput In[2], const FSkateBallActionInput Act[2], float Dt)
		{
			Time += Dt;
			BallSinceImpulse += Dt;
			FSkateMoveInput Used[2] = { In[0], In[1] };
			FSkateBallActionInput UsedAct[2] = { Act[0], Act[1] };
			for (int Index = 0; Index < 2; ++Index)
			{
				S[Index].Stun = SkateMath::Max(0.f, S[Index].Stun - Dt);
				S[Index].CheckLeft -= Dt;
				if (S[Index].bAI)
				{
					const FSkateSkaterDecision Dec = FSkateSkaterAI::Think(AIView(Index), T.AI, S[Index].Brain, Dt);
					Used[Index] = Dec.Move;
					UsedAct[Index] = Dec.Actions;
					S[Index].Mode = Dec.Mode;
					if (Dec.bCheck && S[Index].CheckLeft <= 0.f)
					{
						S[Index].CheckLeft = T.Hit.CheckWindow;
					}
				}
				if (S[Index].Stun > 0.f)
				{
					Used[Index] = FSkateMoveInput(); // no stick while stunned
				}
				FSkateModel::Step(T.Movement, Used[Index], Dt, S[Index].State);
				S[Index].Pos += S[Index].State.Velocity * Dt;
			}
			// Capsules block each other (as the CMC would): push apart, drop the closing velocity.
			{
				const FSkateVec2 Delta = S[1].Pos - S[0].Pos;
				const float Dist = Delta.Size();
				if (Dist < 2.f * SkaterRadius && Dist > 0.01f)
				{
					// Opponents: a body check before the capsules push apart.
					const FSkateHitResult Hit = Team[0] != Team[1] && S[0].Stun <= 0.f && S[1].Stun <= 0.f
						? FSkateHit::Resolve(T.Hit, S[0].Pos, S[0].State.Velocity, S[0].CheckLeft > 0.f, S[1].Pos, S[1].State.Velocity, S[1].CheckLeft > 0.f)
						: FSkateHitResult();
					if (Hit.bHit)
					{
						const int Victim = 1 - Hit.Hitter;
						S[Hit.Hitter].State.Velocity = Hit.HitterVelocity;
						S[Victim].State.Velocity = Hit.VictimVelocity;
						S[Victim].Stun = T.Hit.StunTime;
						++S[Hit.Hitter].Hits;
					}
					const FSkateVec2 N = Delta * (1.f / Dist);
					const float Push = (2.f * SkaterRadius - Dist) * 0.5f;
					S[0].Pos -= N * Push;
					S[1].Pos += N * Push;
					const float Closing = (S[0].State.Velocity - S[1].State.Velocity).Dot(N);
					if (Closing > 0.f)
					{
						S[0].State.Velocity -= N * (Closing * 0.5f);
						S[1].State.Velocity += N * (Closing * 0.5f);
					}
				}
			}

			if (bKeeper)
			{
				FSkateKeeperBall KB;
				KB.bValid = true;
				KB.Pos = BallPos;
				KB.Vel = BallVel;
				KB.Radius = T.BallPhysics.Radius;
				KB.bHeldBySkater = Holder == 0 || Holder == 1;
				KB.TimeSinceImpulse = BallSinceImpulse;
				const FSkateKeeperOutput Out = FSkateKeeper::Update(KT, Goal, KB, S[Controlled].Pos, Dt, K);
				switch (Out.Action)
				{
				case ESkateKeeperAction::Parry:
					BallVel = Out.BallVelocity;
					Impulse(ESkateImpulseKind::Save, KeeperHolder);
					++Parries;
					if (Result == EShotResult::Open) { Result = EShotResult::Saved; }
					break;
				case ESkateKeeperAction::Catch:
					Impulse(ESkateImpulseKind::Save, KeeperHolder);
					++Catches;
					if (Result == EShotResult::Open) { Result = EShotResult::Saved; }
					break;
				case ESkateKeeperAction::Release:
					Holder = NoHolder;
					BallPos = Out.BallPosition;
					BallVel = Out.BallVelocity;
					Impulse(ESkateImpulseKind::Push, KeeperHolder);
					++Releases;
					break;
				default:
					break;
				}
				if (Out.bHolding)
				{
					Holder = KeeperHolder;
					BallPos = Out.HoldPosition;
					BallVel = FSkateVec3();
				}
			}

			for (int Index = 0; Index < 2; ++Index)
			{
				FSkateBallCarry Carry;
				const FSkateBallImpulse Imp = FSkateBallControl::Update(T.BallControl, Query(Index, Used[Index]), UsedAct[Index], Dt, S[Index].Control, S[Index].Report, &Carry);
				if (Imp.IsValid())
				{
					BallVel = Imp.NewBallVelocity;
					Impulse(Imp.Kind, Index);
					++S[Index].Impulses;
					S[Index].BodyBlocks += Imp.Kind == ESkateImpulseKind::BodyBlock ? 1 : 0;
				}
				else if (Carry.bActive)
				{
					BallVel = Carry.Velocity;
				}
				if (S[Index].Control.Possession.bPossessed)
				{
					Holder = Index;
				}
				else if (Holder == Index)
				{
					Holder = NoHolder;
				}
			}

			if (Holder != KeeperHolder)
			{
				StepBall(Dt, Holder != NoHolder);
			}
		}

		void StepBall(float Dt, bool bCarried)
		{
			const FSkateBallPhysicsTuning& BP = T.BallPhysics;
			const float R = BP.Radius;
			const bool bOnIce = BallPos.Z - R <= 0.5f && SkateMath::Abs(BallVel.Z) < 30.f;
			if (!bCarried)
			{
				BallVel = BallVel * std::exp(-BP.LinearDamping * Dt);
				if (bOnIce)
				{
					FSkateVec2 V = BallVel.XY();
					const float Speed = V.Size();
					const float NewSpeed = SkateMath::Max(0.f, Speed - BP.RollingResistance * Dt);
					V = Speed > 0.f ? V * (NewSpeed / Speed) : V;
					if (V.Size() < BP.StopSpeed) { V = FSkateVec2(); }
					BallVel = FSkateVec3(V, BallVel.Z);
				}
			}
			if (!bOnIce || BallVel.Z > 0.f)
			{
				BallVel.Z -= Gravity * Dt;
			}
			BallPos = BallPos + BallVel * Dt;
			if (BallPos.Z < R)
			{
				BallPos.Z = R;
				if (BallVel.Z < 0.f)
				{
					BallVel.Z = -BallVel.Z * BP.IceRestitution;
					if (BallVel.Z < 60.f) { BallVel.Z = 0.f; }
				}
			}
			// Goal line: in the mouth under the bar = goal (the net stops it), outside = wide.
			if (bKeeper && (Result == EShotResult::Open || Result == EShotResult::Saved))
			{
				const float Along = Goal.Along(BallPos.XY());
				if (Along < -R)
				{
					const bool bInMouth = SkateMath::Abs(Goal.Lateral(BallPos.XY())) < Goal.HalfWidth && BallPos.Z < Goal.Height;
					if (bInMouth || Result == EShotResult::Open)
					{
						Result = bInMouth ? EShotResult::Goal : EShotResult::Wide;
					}
					BallVel = FSkateVec3();
				}
			}
		}

		void Run(float Seconds, float Dt, const std::function<void(float, FSkateMoveInput*, FSkateBallActionInput*)>& Script = nullptr)
		{
			const float End = Time + Seconds - 0.5f * Dt;
			while (Time < End)
			{
				FSkateMoveInput In[2];
				FSkateBallActionInput Act[2];
				if (Script) { Script(Time, In, Act); }
				Frame(In, Act, Dt);
			}
		}

		// Ball trapped at the feet of skater Index.
		void GiveBall(int Index, float Dt = 1.f / 60.f)
		{
			BallPos = FSkateVec3(S[Index].Pos + S[Index].State.Heading * 42.f, T.BallPhysics.Radius);
			BallVel = FSkateVec3();
			Run(Dt, Dt);
		}
	};

	FSkateMoveInput Stick(const FSkateVec2& Dir, float Mag, float Brake = 0.f)
	{
		FSkateMoveInput In;
		In.Direction = Dir.GetSafeNormal();
		In.Magnitude = Mag;
		In.Brake = Brake;
		return In;
	}

	// ---------------------------------- Passing --------------------------------------------

	struct FPassOutcome
	{
		bool bReceived = false;
		float Time = -1.f;
		float SpeedAtTrap = 0.f;
		int ReceiverBounces = 0;
		bool bPasserRetook = false;
		int Doubles = 0;
		float PassSpeed = 0.f;
	};

	// A (index 0) passes along +X with the given charge time; B (index 1, AI) receives.
	FPassOutcome Pass(const FSkateVec2& ReceiverPos, const FSkateVec2& ReceiverHeading, float ChargeSeconds, float Dt)
	{
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(1.f, 0.f));
		Sim.Place(1, ReceiverPos, ReceiverHeading);
		Sim.GiveBall(0, Dt);
		Sim.Run(0.5f, Dt, [](float, FSkateMoveInput* In, FSkateBallActionInput*) { In[0] = Stick(FSkateVec2(1.f, 0.f), 0.f, 1.f); });
		// Press A, hold, release - stick pointing at +X.
		bool bPressed = false;
		bool bReleased = false;
		const float Start = Sim.Time;
		FPassOutcome Out;
		const float Horizon = 5.f;
		float PrevBallSpeed = 0.f;
		while (Sim.Time - Start < Horizon && !Out.bReceived)
		{
			FSkateMoveInput In[2];
			FSkateBallActionInput Act[2];
			In[0] = Stick(FSkateVec2(1.f, 0.f), 0.2f, 1.f);
			if (!bPressed) { Act[0].bPushPressed = true; bPressed = true; }
			if (!bReleased && Sim.Time - Start >= ChargeSeconds - 0.5f * Dt) { Act[0].bPushReleased = true; bReleased = true; }
			Sim.S[1].bAI = bReleased; // keeps its heading until the pass is played, then receives it
			const bool bHadBall = Sim.S[1].Control.Possession.bPossessed;
			const int Before = Sim.S[0].Impulses;
			Sim.Frame(In, Act, Dt);
			if (Sim.S[0].Impulses != Before) { Out.PassSpeed = Sim.BallVel.XY().Size(); }
			if (!bHadBall && Sim.S[1].Control.Possession.bPossessed)
			{
				Out.bReceived = true;
				Out.Time = Sim.Time - Start;
				Out.SpeedAtTrap = PrevBallSpeed;
			}
			PrevBallSpeed = Sim.BallVel.XY().Size();
			Out.bPasserRetook |= bReleased && Sim.S[0].Control.Possession.bPossessed && Sim.S[0].Control.Possession.TimeHeld < 0.5f && Sim.Time - Start > 0.1f;
		}
		Out.ReceiverBounces = Sim.S[1].BodyBlocks;
		Out.Doubles = Sim.DoubleImpulses;
		return Out;
	}

	void TestPassAndReceive(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Team.PassAndReceive");
		const float Dt = 1.f / 60.f;
		struct FCase { const char* Name; FSkateVec2 Pos; FSkateVec2 Heading; float Charge; };
		const FCase Cases[] = {
			{ "12 m tap", FSkateVec2(1200.f, 0.f), FSkateVec2(-1.f, 0.f), 0.f },
			{ "12 m half", FSkateVec2(1200.f, 0.f), FSkateVec2(-1.f, 0.f), 0.3f },
			{ "12 m full", FSkateVec2(1200.f, 0.f), FSkateVec2(-1.f, 0.f), 0.7f },
			{ "12 m full, receiver side-on", FSkateVec2(1200.f, 0.f), FSkateVec2(0.f, 1.f), 0.7f },
			{ "12 m full, receiver facing away", FSkateVec2(1200.f, 0.f), FSkateVec2(1.f, 0.f), 0.7f },
			{ "2.5 m off target", FSkateVec2(1200.f, 250.f), FSkateVec2(-1.f, 0.f), 0.f },
			{ "25 m full", FSkateVec2(2500.f, 0.f), FSkateVec2(-1.f, 0.f), 0.7f },
			{ "5 m full (hard, close)", FSkateVec2(500.f, 0.f), FSkateVec2(-1.f, 0.f), 0.7f },
		};
		bool bOk = true;
		std::string Info;
		for (const FCase& C : Cases)
		{
			const FPassOutcome P = Pass(C.Pos, C.Heading, C.Charge, Dt);
			const bool bCase = P.bReceived && P.ReceiverBounces == 0 && !P.bPasserRetook && P.Doubles == 0;
			bOk &= bCase;
			Info += Fmt("%s: pass %.0f cm/s, received %s after %.2fs at %.0f cm/s, bounces off the receiver %d%s; ", C.Name, P.PassSpeed,
				P.bReceived ? "yes" : "NO", P.Time, P.SpeedAtTrap, P.ReceiverBounces, bCase ? "" : " FAIL");
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}

	void TestNoStealFromTeammate(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Team.NoStealFromTeammate");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(1.f, 0.f));
		Sim.Place(1, FSkateVec2(500.f, 0.f), FSkateVec2(-1.f, 0.f));
		Sim.GiveBall(0, Dt);
		int LostFrames = 0;
		bool bBTook = false;
		Sim.Run(3.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0] = Stick(FSkateVec2(1.f, 0.f), 0.5f);
			In[1] = Stick(FSkateVec2(-1.f, 0.05f), 0.6f); // skates straight through the ball
			LostFrames += Sim.S[0].Control.Possession.bPossessed ? 0 : 1;
			bBTook |= Sim.S[1].Control.Possession.bPossessed;
		});
		R.bPassed = !bBTook && Sim.S[1].Impulses == 0 && Sim.DoubleImpulses == 0;
		R.Details = Fmt("teammate skates through the carried ball: teammate took it=%d, teammate impulses %d, double impulses %d, carrier still has it=%d (lost frames %d - the carrier may lose it to the body, never to a steal)",
			bBTook ? 1 : 0, Sim.S[1].Impulses, Sim.DoubleImpulses, Sim.S[0].Control.Possession.bPossessed ? 1 : 0, LostFrames);
		Out.push_back(R);
	}

	void TestTeammateSupportsAhead(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Team.AIGetsOpenAheadOfCarrier");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(-1000.f, 0.f), FSkateVec2(1.f, 0.f));
		Sim.Place(1, FSkateVec2(-1300.f, 150.f), FSkateVec2(1.f, 0.f)); // behind the carrier
		Sim.GiveBall(0, Dt);
		Sim.S[1].bAI = true;
		Sim.Run(4.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*) { In[0].Brake = 1.f; });
		const FSkateVec2 Rel = Sim.S[1].Pos - Sim.S[0].Pos;
		const bool bAhead = Rel.X > 300.f; // towards the goal at +X
		const bool bWide = SkateMath::Abs(Rel.Y) > 300.f;
		R.bPassed = bAhead && bWide && Sim.S[1].Mode == ESkateSkaterMode::Support && !Sim.S[1].Control.Possession.bPossessed;
		R.Details = Fmt("player holds the ball, teammate starts 3 m behind: after 4 s it is %.0f cm ahead and %.0f cm to the side (mode %s)",
			Rel.X, Rel.Y, SkateSkaterModeName(Sim.S[1].Mode));
		Out.push_back(R);
	}

	void TestTeammateIgnoresOwnPass(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Team.AIDoesNotChaseItsOwnPass");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(1200.f, 0.f), FSkateVec2(-1.f, 0.f)); // receiver, faces the passer
		Sim.Place(1, FSkateVec2(0.f, 0.f), FSkateVec2(1.f, 0.f));    // AI passer
		Sim.GiveBall(1, Dt);
		Sim.S[1].bAI = true; // the mate is 12 m nearer the goal: the AI passes
		bool bPassed = false;
		float MaxPasserX = 0.f;
		Sim.Run(3.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			bPassed |= Sim.LastKind == ESkateImpulseKind::Push && Sim.LastSource == 1;
			if (bPassed && Sim.Holder == NoHolder) { MaxPasserX = SkateMath::Max(MaxPasserX, Sim.S[1].Pos.X); }
		});
		R.bPassed = bPassed && Sim.S[0].Control.Possession.bPossessed && MaxPasserX < 700.f;
		R.Details = Fmt("AI passed %d, receiver has the ball %d, passer went at most to x = %.0f (stays behind the pass)",
			bPassed ? 1 : 0, Sim.S[0].Control.Possession.bPossessed ? 1 : 0, MaxPasserX);
		Out.push_back(R);
	}

	void TestTeammateFetches(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Team.AIFetchesLooseBall");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(-1500.f, 0.f), FSkateVec2(1.f, 0.f));
		Sim.Place(1, FSkateVec2(0.f, 0.f), FSkateVec2(-1.f, 0.f));
		Sim.S[1].bAI = true;
		Sim.BallPos = FSkateVec3(500.f, -250.f, Sim.T.BallPhysics.Radius);
		Sim.Run(1.f, Dt); // the ball has been loose for a while: not a pass
		float GotIt = -1.f;
		Sim.Run(5.f, Dt, [&](float Time, FSkateMoveInput*, FSkateBallActionInput*)
		{
			if (GotIt < 0.f && Sim.S[1].Control.Possession.bPossessed) { GotIt = Time; }
		});
		R.bPassed = GotIt > 0.f && GotIt < 3.5f && Sim.S[1].BodyBlocks == 0;
		R.Details = Fmt("resting ball 5.6 m away (behind the teammate): fetched and trapped after %.2fs, bounces %d", GotIt, Sim.S[1].BodyBlocks);
		Out.push_back(R);
	}

	void TestSkatesBackwards(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Move.SkatesBackwardsFacingAway");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(-1.f, 0.f)); // faces -X ...
		Sim.Place(1, FSkateVec2(-2000.f, -1200.f), FSkateVec2(1.f, 0.f));
		Sim.BallPos = FSkateVec3(2000.f, 1200.f, Sim.T.BallPhysics.Radius);
		Sim.Run(2.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0] = Stick(FSkateVec2(1.f, 0.f), 1.f); // ... and pushes towards +X with the backward button held
			In[0].bBackward = true;
		});
		const FSkateVec2 V = Sim.S[0].State.Velocity;
		const FSkateVec2 Hd = Sim.S[0].State.Heading;
		const float Forward = Sim.T.Movement.MaxSpeed;
		R.bPassed = V.X > 0.4f * Forward && V.X < 0.75f * Forward && Hd.X < -0.9f && Sim.S[0].Pos.X > 300.f;
		R.Details = Fmt("stick +X with the backward button: after 2 s moving at %.0f cm/s along +X (forward max %.0f), blades point %.2f along X, travelled %.0f cm",
			V.X, Forward, Hd.X, Sim.S[0].Pos.X);
		Out.push_back(R);
	}

	void TestDefenderFacesTheBall(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Team.DefenderRetreatsFacingTheBall");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Team[1] = 1;
		Sim.Chaser[1] = false; // the other opponent is pressing: this one defends
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(1.f, 0.f));    // carrier
		Sim.Place(1, FSkateVec2(-600.f, 0.f), FSkateVec2(1.f, 0.f)); // defender, between the ball and its goal at -X
		Sim.GiveBall(0, Dt);
		Sim.S[1].bAI = true;
		int FramesMoving = 0;
		int FramesBackTurned = 0;
		Sim.Run(2.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			const FSkateVec2 ToBall = (Sim.BallPos.XY() - Sim.S[1].Pos).GetSafeNormal();
			if (Sim.S[1].State.Velocity.Size() > 150.f)
			{
				++FramesMoving;
				FramesBackTurned += Sim.S[1].State.Heading.Dot(ToBall) < 0.f ? 1 : 0;
			}
		});
		const FSkateVec2 ToBall = (Sim.BallPos.XY() - Sim.S[1].Pos).GetSafeNormal();
		const bool bRetreated = Sim.S[1].Pos.X < -900.f;
		R.bPassed = Sim.S[1].Mode == ESkateSkaterMode::Defend && bRetreated && FramesMoving > 30 && FramesBackTurned == 0
			&& Sim.S[1].State.Heading.Dot(ToBall) > 0.7f;
		R.Details = Fmt("defender drops back from x = -600 to %.0f (mode %s): moving frames %d, frames with the back to the ball %d, final facing %.2f",
			Sim.S[1].Pos.X, SkateSkaterModeName(Sim.S[1].Mode), FramesMoving, FramesBackTurned, Sim.S[1].State.Heading.Dot(ToBall));
		Out.push_back(R);
	}

	// ---------------------------------- Opponents ------------------------------------------

	void TestOpponentSteals(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Match.OpponentStealsHeldBall");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Team[1] = 1;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(1.f, 0.f));
		Sim.Place(1, FSkateVec2(900.f, 0.f), FSkateVec2(-1.f, 0.f));
		Sim.GiveBall(0, Dt);
		const bool bHeld = Sim.S[0].Control.Possession.bPossessed;
		Sim.S[1].bAI = true; // chaser: presses the carrier
		float Stolen = -1.f;
		Sim.Run(5.f, Dt, [&](float Time, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f; // the carrier just stands there
			if (Stolen < 0.f && Sim.S[1].Control.Possession.bPossessed) { Stolen = Time; }
		});
		const bool bTaken = Sim.S[0].Control.Possession.LastLoss == ESkatePossessionLoss::Taken || Sim.S[0].Control.Possession.LastLoss == ESkatePossessionLoss::Hit;
		R.bPassed = bHeld && Stolen > Sim.T.BallControl.Possession.StealProtectTime && Stolen < 4.f && bTaken && !Sim.S[0].Control.Possession.bPossessed && Sim.DoubleImpulses == 0;
		R.Details = Fmt("opponent 9 m away presses a standing carrier: ball taken after %.2fs (loss %s), carrier still has it %d, double impulses %d",
			Stolen, SkatePossessionLossName(Sim.S[0].Control.Possession.LastLoss), Sim.S[0].Control.Possession.bPossessed ? 1 : 0, Sim.DoubleImpulses);
		Out.push_back(R);
	}

	void TestOpponentAttacksAndShoots(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Match.OpponentCarriesAndShootsOnTarget");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Team[1] = 1;
		Sim.Place(0, FSkateVec2(-2000.f, -1200.f), FSkateVec2(1.f, 0.f)); // out of the way
		Sim.Place(1, FSkateVec2(-500.f, -300.f), FSkateVec2(1.f, 0.f));
		Sim.GiveBall(1, Dt);
		Sim.S[1].bAI = true;
		float ShotTime = -1.f;
		float ShotDist = 0.f;
		float CrossLateral = 1e9f;
		Sim.Run(8.f, Dt, [&](float Time, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			if (ShotTime < 0.f && Sim.LastKind == ESkateImpulseKind::Kick && Sim.LastSource == 1 && Sim.BallSinceImpulse < Dt)
			{
				ShotTime = Time;
				ShotDist = (Sim.Goal.Center - Sim.S[1].Pos).Size();
				// Where the shot crosses the goal line (straight-line path on the ice).
				const FSkateVec2 V = Sim.BallVel.XY();
				const float Closing = -V.Dot(Sim.Goal.Normal);
				const float Along = Sim.Goal.Along(Sim.BallPos.XY());
				if (Closing > 1.f)
				{
					CrossLateral = Sim.Goal.Lateral(Sim.BallPos.XY() + V * (Along / Closing));
				}
			}
		});
		const bool bOnTarget = SkateMath::Abs(CrossLateral) < Sim.Goal.HalfWidth;
		R.bPassed = ShotTime > 0.f && ShotTime < 7.f && ShotDist < Sim.T.AI.ShootDistance + 100.f && bOnTarget;
		R.Details = Fmt("AI with the ball 31 m out: shot at %.2fs from %.0f cm, crosses the line %.0f cm off centre (mouth +-%.0f)",
			ShotTime, ShotDist, CrossLateral < 1e8f ? CrossLateral : -1.f, Sim.Goal.HalfWidth);
		Out.push_back(R);
	}

	void TestOpponentDodgesBlocker(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Match.OpponentSkatesAroundBlocker");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Team[1] = 1;
		Sim.Place(0, FSkateVec2(300.f, 0.f), FSkateVec2(-1.f, 0.f)); // stands in the way, facing the carrier
		Sim.Place(1, FSkateVec2(0.f, 0.f), FSkateVec2(1.f, 0.f));
		Sim.GiveBall(1, Dt);
		Sim.S[1].bAI = true;
		float Passed = -1.f;
		Sim.Run(4.f, Dt, [&](float Time, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			if (Passed < 0.f && Sim.S[1].Pos.X > 600.f && Sim.S[1].Control.Possession.bPossessed) { Passed = Time; }
		});
		R.bPassed = Passed > 0.f && Passed < 3.5f && Sim.S[0].Control.Possession.LastLoss == ESkatePossessionLoss::None;
		R.Details = Fmt("blocker 3 m ahead of the AI carrier: got past with the ball after %.2fs, blocker ever had it %d",
			Passed, Sim.S[0].Control.Possession.AcquireCount);
		Out.push_back(R);
	}

	void TestBodyCheck(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Match.BodyCheckKnocksBallLoose");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Team[1] = 1;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(0.f, 1.f));    // carrier, side-on to the hitter
		Sim.Place(1, FSkateVec2(600.f, 0.f), FSkateVec2(-1.f, 0.f)); // hitter sprints in from 6 m
		Sim.GiveBall(0, Dt);
		float HitTime = -1.f;
		float PushedSpeed = 0.f;
		bool bLostToHit = false;
		Sim.Run(3.f, Dt, [&](float Time, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			In[1] = Stick(FSkateVec2(-1.f, 0.f), 1.f);
			In[1].Boost = 1.f;
			Sim.S[1].CheckLeft = 1.f; // check button held
			if (HitTime < 0.f && Sim.S[1].Hits > 0) { HitTime = Time; PushedSpeed = Sim.S[0].State.Velocity.Size(); }
			bLostToHit |= Sim.S[0].Control.Possession.LastLoss == ESkatePossessionLoss::Hit;
		});
		R.bPassed = HitTime > 0.f && PushedSpeed > 300.f && bLostToHit && !Sim.S[0].Control.Possession.bPossessed;
		R.Details = Fmt("hitter sprints into a standing carrier with the check button: check after %.2fs, victim shoved at %.0f cm/s, lost the ball to the hit %d, still has it %d",
			HitTime, PushedSpeed, bLostToHit ? 1 : 0, Sim.S[0].Control.Possession.bPossessed ? 1 : 0);
		Out.push_back(R);
	}

	void TestNoCheckWithoutButton(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Match.NoCheckWithoutTheButton");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Team[1] = 1;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(0.f, 1.f));
		Sim.Place(1, FSkateVec2(600.f, 0.f), FSkateVec2(-1.f, 0.f));
		Sim.GiveBall(0, Dt);
		Sim.Run(2.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			In[1] = Stick(FSkateVec2(-1.f, 0.f), 1.f);
			In[1].Boost = 1.f;
		});
		R.bPassed = Sim.S[1].Hits == 0 && Sim.S[0].Control.Possession.LastLoss != ESkatePossessionLoss::Hit;
		R.Details = Fmt("opponent sprints into the carrier without the button: checks %d, loss %s (a bump, not a hit)", Sim.S[1].Hits,
			SkatePossessionLossName(Sim.S[0].Control.Possession.LastLoss));
		Out.push_back(R);
	}

	void TestNoCheckBetweenTeammates(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Match.NoCheckBetweenTeammates");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim;
		Sim.Place(0, FSkateVec2(0.f, 0.f), FSkateVec2(0.f, 1.f));
		Sim.Place(1, FSkateVec2(600.f, 0.f), FSkateVec2(-1.f, 0.f));
		Sim.GiveBall(0, Dt);
		Sim.Run(2.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*)
		{
			In[0].Brake = 1.f;
			In[1] = Stick(FSkateVec2(-1.f, 0.f), 1.f);
			In[1].Boost = 1.f;
			Sim.S[1].CheckLeft = 1.f;
		});
		R.bPassed = Sim.S[1].Hits == 0 && Sim.S[0].Control.Possession.LastLoss != ESkatePossessionLoss::Hit;
		R.Details = Fmt("teammate sprints into the carrier with the check button: checks %d, loss %s", Sim.S[1].Hits, SkatePossessionLossName(Sim.S[0].Control.Possession.LastLoss));
		Out.push_back(R);
	}

	// ---------------------------------- Keeper ---------------------------------------------

	struct FShot
	{
		float FromAlong = 1400.f;
		float FromLateral = 0.f;
		float AimLateral = 0.f;
		float Speed = 2500.f;
		float Lift = 0.f;
	};

	struct FShotOutcome
	{
		EShotResult Result = EShotResult::Open;
		float KeeperStart = 0.f;
		bool bDived = false;
		ESkateKeeperAction Action = ESkateKeeperAction::None;
		float BallAlongVelAfterSave = 0.f;
	};

	FTeamSim MakeKeeperSim()
	{
		FTeamSim Sim;
		Sim.bKeeper = true;
		// Skaters far away so they do not interfere.
		Sim.Place(0, FSkateVec2(-1500.f, -1200.f), FSkateVec2(1.f, 0.f));
		Sim.Place(1, FSkateVec2(-1500.f, 1200.f), FSkateVec2(1.f, 0.f));
		return Sim;
	}

	FShotOutcome Shoot(const FShot& Shot, float Dt)
	{
		FTeamSim Sim = MakeKeeperSim();
		const FSkateVec2 From = Sim.Goal.ToWorld(Shot.FromAlong, Shot.FromLateral);
		Sim.BallPos = FSkateVec3(From, Sim.T.BallPhysics.Radius);
		Sim.Run(1.5f, Dt); // keeper takes position for a ball resting there
		FShotOutcome Out;
		Out.KeeperStart = Sim.K.Lateral;
		const FSkateVec2 Dir = (Sim.Goal.ToWorld(0.f, Shot.AimLateral) - From).GetSafeNormal();
		Sim.BallVel = FSkateVec3(Dir * Shot.Speed, Shot.Lift);
		Sim.Impulse(ESkateImpulseKind::Kick, 0);
		const float Start = Sim.Time;
		while (Sim.Time - Start < 3.f && Sim.Result != EShotResult::Goal && Sim.Result != EShotResult::Wide)
		{
			FSkateMoveInput In[2];
			FSkateBallActionInput Act[2];
			const ESkateKeeperAction Before = Sim.K.LastAction;
			const float BeforeT = Sim.K.TimeSinceAction;
			Sim.Frame(In, Act, Dt);
			Out.bDived |= Sim.K.DiveTime >= 0.f;
			if (Sim.K.TimeSinceAction < BeforeT || Sim.K.LastAction != Before)
			{
				if (Out.Action == ESkateKeeperAction::None)
				{
					Out.Action = Sim.K.LastAction;
					Out.BallAlongVelAfterSave = Sim.BallVel.XY().Dot(Sim.Goal.Normal);
				}
			}
			if (Sim.Result == EShotResult::Saved && Sim.Time - Start > 2.f) { break; }
		}
		Out.Result = Sim.Result;
		return Out;
	}

	void TestKeeperPositioning(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.Positioning");
		const float Dt = 1.f / 60.f;
		bool bOk = true;
		std::string Info;
		const float Spots[][2] = { { 1400.f, 0.f }, { 1400.f, 600.f }, { 1400.f, -600.f }, { 600.f, 900.f }, { 300.f, -1200.f }, { 2500.f, 200.f } };
		for (const auto& Spot : Spots)
		{
			FTeamSim Sim = MakeKeeperSim();
			Sim.BallPos = FSkateVec3(Sim.Goal.ToWorld(Spot[0], Spot[1]), Sim.T.BallPhysics.Radius);
			Sim.Run(2.f, Dt);
			const float Lat = Sim.K.Lateral;
			const bool bInside = SkateMath::Abs(Lat) <= Sim.Goal.HalfWidth - 25.f + 0.5f;
			const bool bSide = SkateMath::Abs(Spot[1]) < 100.f ? SkateMath::Abs(Lat) < 20.f : Lat * Spot[1] > 0.f;
			const bool bCase = bInside && bSide;
			bOk &= bCase;
			Info += Fmt("ball %.0f m out, %.0f m to the side -> keeper at %+.0f cm%s; ", Spot[0] / 100.f, Spot[1] / 100.f, Lat, bCase ? "" : " FAIL");
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}

	void TestKeeperSaves(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.SavesReachableShots");
		const float Dt = 1.f / 60.f;
		bool bOk = true;
		std::string Info;
		for (float Speed : { 1800.f, 2500.f, 3300.f })
		{
			for (float Aim : { 0.f, 100.f, -100.f })
			{
				FShot Shot;
				Shot.Speed = Speed;
				Shot.AimLateral = Aim;
				const FShotOutcome O = Shoot(Shot, Dt);
				const bool bCase = O.Result == EShotResult::Saved;
				bOk &= bCase;
				Info += Fmt("%.0f cm/s at %+.0f: %s (%s)%s; ", Speed, Aim, ShotResultName(O.Result), SkateKeeperActionName(O.Action), bCase ? "" : " FAIL");
			}
		}
		// Medium power into the corners: the dive gets there.
		for (float Aim : { 225.f, -225.f })
		{
			FShot Shot;
			Shot.Speed = 2200.f;
			Shot.AimLateral = Aim;
			const FShotOutcome O = Shoot(Shot, Dt);
			const bool bCase = O.Result == EShotResult::Saved && O.bDived;
			bOk &= bCase;
			Info += Fmt("2200 cm/s corner %+.0f: %s, dived=%d%s; ", Aim, ShotResultName(O.Result), O.bDived ? 1 : 0, bCase ? "" : " FAIL");
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}

	void TestKeeperCanBeBeaten(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.PowerCornersScore");
		const float Dt = 1.f / 60.f;
		bool bOk = true;
		std::string Info;
		for (float Aim : { 225.f, -225.f })
		{
			FShot Shot;
			Shot.Speed = 3300.f;
			Shot.AimLateral = Aim;
			const FShotOutcome O = Shoot(Shot, Dt);
			const bool bCase = O.Result == EShotResult::Goal && O.bDived;
			bOk &= bCase;
			Info += Fmt("full power (3300) low into the corner %+.0f from 14 m: %s, keeper dived=%d%s; ", Aim, ShotResultName(O.Result), O.bDived ? 1 : 0, bCase ? "" : " FAIL");
		}
		// Information only: lofted full-power shots (no pass/fail).
		for (float Aim : { 150.f, 210.f })
		{
			FShot Shot;
			Shot.Speed = 3300.f;
			Shot.Lift = 530.f;
			Shot.AimLateral = Aim;
			const FShotOutcome O = Shoot(Shot, Dt);
			Info += Fmt("[info] lofted full power at %+.0f: %s; ", Aim, ShotResultName(O.Result));
		}
		// Information only: the same corner shot from closer in.
		{
			FShot Shot;
			Shot.FromAlong = 900.f;
			Shot.Speed = 2600.f;
			Shot.AimLateral = 200.f;
			const FShotOutcome O = Shoot(Shot, Dt);
			Info += Fmt("[info] 9 m, 2600 cm/s at +200: %s; ", ShotResultName(O.Result));
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}

	void TestKeeperCloseRange(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.ClosesDownCloseShots");
		const float Dt = 1.f / 60.f;
		// Ball resting 5 m out: the keeper has come out and dropped into the butterfly.
		FTeamSim Sim = MakeKeeperSim();
		Sim.BallPos = FSkateVec3(Sim.Goal.ToWorld(500.f, 0.f), Sim.T.BallPhysics.Radius);
		Sim.Run(2.f, Dt);
		const float Depth = Sim.K.Depth;
		const bool bButterfly = Sim.K.bButterfly;
		// Low shot at the corner from 5 m: saved by the pads. The same shot lifted over them: goal.
		FShot Low; Low.FromAlong = 500.f; Low.AimLateral = 170.f; Low.Speed = 2800.f;
		FShot High = Low; High.Lift = 900.f; // over the pads, under the bar
		const FShotOutcome L = Shoot(Low, Dt);
		const FShotOutcome H = Shoot(High, Dt);
		// Far ball: the keeper stays home.
		FTeamSim Far = MakeKeeperSim();
		Far.BallPos = FSkateVec3(Far.Goal.ToWorld(1800.f, 0.f), Far.T.BallPhysics.Radius);
		Far.Run(2.f, Dt);
		R.bPassed = Depth > 100.f && bButterfly && L.Result == EShotResult::Saved && H.Result == EShotResult::Goal && Far.K.Depth < 1.f;
		R.Details = Fmt("ball 5 m out: keeper %.0f cm off the line, butterfly %d; low shot 2800 at +170 from 5 m: %s; lifted: %s; ball 18 m out: keeper %.0f cm off the line",
			Depth, bButterfly ? 1 : 0, ShotResultName(L.Result), ShotResultName(H.Result), Far.K.Depth);
		Out.push_back(R);
	}

	void TestKeeperParryGoesOut(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.ParryAwayFromGoal");
		const float Dt = 1.f / 60.f;
		bool bOk = true;
		std::string Info;
		for (float Aim : { 50.f, -50.f, 120.f })
		{
			FShot Shot;
			Shot.Speed = 3000.f;
			Shot.AimLateral = Aim;
			const FShotOutcome O = Shoot(Shot, Dt);
			const bool bCase = O.Action == ESkateKeeperAction::Parry && O.BallAlongVelAfterSave > 200.f && O.Result == EShotResult::Saved;
			bOk &= bCase;
			Info += Fmt("3000 cm/s at %+.0f: %s, ball leaves the goal at %.0f cm/s, end %s%s; ", Aim, SkateKeeperActionName(O.Action), O.BallAlongVelAfterSave,
				ShotResultName(O.Result), bCase ? "" : " FAIL");
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}

	void TestKeeperCatchAndThrow(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.CatchAndThrowOut");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim = MakeKeeperSim();
		// The controlled skater waits 12 m out, a bit to the side.
		Sim.Place(0, Sim.Goal.ToWorld(1200.f, 300.f), FSkateVec2(1.f, 0.f));
		const FSkateVec2 From = Sim.Goal.ToWorld(1000.f, -150.f);
		Sim.BallPos = FSkateVec3(From, Sim.T.BallPhysics.Radius);
		Sim.Run(1.f, Dt);
		Sim.BallVel = FSkateVec3((Sim.Goal.ToWorld(0.f, 0.f) - From).GetSafeNormal() * 1000.f, 0.f);
		Sim.Impulse(ESkateImpulseKind::Kick, 1);
		float CaughtAt = -1.f;
		float ReleasedAt = -1.f;
		float ReceivedAt = -1.f;
		const float Start = Sim.Time;
		Sim.Run(6.f, Dt, [&](float Time, FSkateMoveInput*, FSkateBallActionInput*)
		{
			if (CaughtAt < 0.f && Sim.K.bHolding) { CaughtAt = Time - Start; }
			if (ReleasedAt < 0.f && Sim.Releases > 0) { ReleasedAt = Time - Start; }
			if (ReceivedAt < 0.f && Sim.S[0].Control.Possession.bPossessed) { ReceivedAt = Time - Start; }
		});
		const float Held = ReleasedAt - CaughtAt;
		R.bPassed = CaughtAt > 0.f && SkateMath::Abs(Held - Sim.KT.HoldTime) < 0.1f && ReceivedAt > ReleasedAt && Sim.Result != EShotResult::Goal
			&& Sim.DoubleImpulses == 0 && Sim.S[0].BodyBlocks == 0;
		R.Details = Fmt("slow shot (1000 cm/s): caught after %.2fs, held %.2fs, rolled out to the skater 12 m away, received %.2fs after the throw (bounces %d)",
			CaughtAt, Held, ReceivedAt - ReleasedAt, Sim.S[0].BodyBlocks);
		Out.push_back(R);
	}

	void TestKeeperSmothersDribble(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.TakesBallDribbledIntoHim");
		const float Dt = 1.f / 60.f;
		FTeamSim Sim = MakeKeeperSim();
		Sim.Place(0, Sim.Goal.ToWorld(700.f, 0.f), Sim.Goal.Normal * -1.f);
		Sim.GiveBall(0, Dt);
		Sim.Run(4.f, Dt, [&](float, FSkateMoveInput* In, FSkateBallActionInput*) { In[0] = Stick(Sim.Goal.Normal * -1.f, 0.4f); });
		const bool bTaken = Sim.K.Catches > 0 && Sim.S[0].Control.Possession.LastLoss == ESkatePossessionLoss::Taken;
		R.bPassed = bTaken && Sim.Result != EShotResult::Goal && Sim.DoubleImpulses == 0;
		R.Details = Fmt("skater dribbles straight into the keeper: keeper took the ball=%d (skater's loss: %s), goal=%d",
			bTaken ? 1 : 0, SkatePossessionLossName(Sim.S[0].Control.Possession.LastLoss), Sim.Result == EShotResult::Goal ? 1 : 0);
		Out.push_back(R);
	}

	void TestKeeperFps(std::vector<FSkateTestResult>& Out)
	{
		FSkateTestResult R("Keeper.SameAcrossFps");
		const FShot Shots[] = {
			{ 1400.f, 0.f, 225.f, 3300.f, 0.f },
			{ 1400.f, 0.f, -225.f, 2200.f, 0.f },
			{ 1400.f, 0.f, 100.f, 3300.f, 0.f },
			{ 1400.f, 300.f, 0.f, 1800.f, 0.f },
		};
		bool bOk = true;
		std::string Info;
		for (const FShot& Shot : Shots)
		{
			EShotResult Results[3];
			int Index = 0;
			for (float Fps : { 30.f, 60.f, 120.f })
			{
				Results[Index++] = Shoot(Shot, 1.f / Fps).Result;
			}
			const bool bSame = Results[0] == Results[1] && Results[1] == Results[2];
			bOk &= bSame;
			Info += Fmt("%.0f cm/s at %+.0f: %s/%s/%s%s; ", Shot.Speed, Shot.AimLateral, ShotResultName(Results[0]), ShotResultName(Results[1]),
				ShotResultName(Results[2]), bSame ? "" : " DIFFERENT");
		}
		R.bPassed = bOk;
		R.Details = Info;
		Out.push_back(R);
	}
}

void RunSkateTeamTests(std::vector<FSkateTestResult>& Out)
{
	using namespace SkateTeamTestsDetail;
	TestPassAndReceive(Out);
	TestNoStealFromTeammate(Out);
	TestTeammateSupportsAhead(Out);
	TestTeammateIgnoresOwnPass(Out);
	TestTeammateFetches(Out);
	TestBodyCheck(Out);
	TestNoCheckWithoutButton(Out);
	TestNoCheckBetweenTeammates(Out);
	TestSkatesBackwards(Out);
	TestDefenderFacesTheBall(Out);
	TestOpponentSteals(Out);
	TestOpponentAttacksAndShoots(Out);
	TestOpponentDodgesBlocker(Out);
	TestKeeperPositioning(Out);
	TestKeeperSaves(Out);
	TestKeeperCanBeBeaten(Out);
	TestKeeperCloseRange(Out);
	TestKeeperParryGoesOut(Out);
	TestKeeperCatchAndThrow(Out);
	TestKeeperSmothersDribble(Out);
	TestKeeperFps(Out);
}

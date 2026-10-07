// Ice skating prototype - what a teammate does while the player controls the other skater (engine independent).
//
// Kept deliberately simple, so the player always stays in charge:
//   - has the ball         -> stops and keeps it at the feet (waits for the player to switch to it)
//   - a pass is coming     -> skates to the point where the ball's path passes closest, stops there
//                             facing the ball, so the trap zone catches it
//   - loose ball close by  -> goes to fetch it when it has (almost) stopped
//   - otherwise            -> glides to a stop and turns to face the ball
// Output is a normal skate input, so the teammate moves exactly like a player-controlled skater.
#pragma once

#include "SkateMath.h"
#include "SkateModel.h"

/** Shared steering primitives for the AI skaters (teammate and opponents). */
namespace SkateSteer
{
	/** Stop and turn the skates towards Target. */
	FSkateMoveInput Face(const FSkateVec2& Pos, const FSkateVec2& Heading, const FSkateVec2& Target);
	/** Skate to Point and stop there facing LookAt (brakes in time, eases off on arrival). */
	FSkateMoveInput GoTo(const FSkateVec2& Pos, const FSkateVec2& Vel, const FSkateVec2& Heading, const FSkateVec2& Point, const FSkateVec2& LookAt);
}

struct FSkateTeammateView
{
	FSkateVec2 Pos;
	FSkateVec2 Vel;
	FSkateVec2 Heading = FSkateVec2(1.f, 0.f);
	bool bHasBall = false;

	bool bBallValid = false;
	FSkateVec2 BallPos;
	FSkateVec2 BallVel;
	/** Someone (the controlled skater, the keeper) has the ball: nothing to chase. */
	bool bBallHeld = false;
};

enum class ESkateTeammateMode : unsigned char
{
	Wait,
	HoldBall,
	Receive,
	Fetch,
};

const char* SkateTeammateModeName(ESkateTeammateMode Mode);

class FSkateTeammateAI
{
public:
	static FSkateMoveInput Think(const FSkateTeammateView& View, ESkateTeammateMode* OutMode = nullptr);

	/** A pass is "for me" when its path passes within this distance (cm) ... */
	static constexpr float ReceiveRadius = 500.f;
	/** ... and gets there within this time (s). */
	static constexpr float ReceiveHorizon = 3.f;
	/** A loose ball slower than this (cm/s) and closer than FetchRadius is fetched. */
	static constexpr float FetchMaxBallSpeed = 250.f;
	static constexpr float FetchRadius = 900.f;
};

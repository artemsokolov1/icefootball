// Ice skating prototype - minimal HUD + toggleable debug panel (View/Back or F1).
//
// Red is used ONLY for concrete, rule-based faults with the condition printed next to them:
//   * DOUBLE IMPULSE      - two gameplay impulses on the ball closer than 40 ms
//   * BALL-PAWN CONTACT   - a physics hit between ball and skater (must be impossible: channel ignored)
//   * BALL OUTSIDE RINK   - ball centre beyond the boards (tunnelling)
//   * SKATER OUTSIDE RINK - skater beyond the boards
//   * BRAKE REVERSAL      - braking flipped the velocity direction
// Everything else (contact refused because "too far" etc.) is information, never red.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SkateDebugHUD.generated.h"

class ASkateCharacter;
class ASkateArena;

UCLASS()
class ICEFOOTBALL_API ASkateDebugHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawAlwaysOn(ASkateCharacter* Skater, ASkateArena* Arena);
	void DrawDebugPanel(ASkateCharacter* Skater, ASkateArena* Arena);
	void DrawStickWidget(ASkateCharacter* Skater, float X, float Y, float Radius);
	void DrawSpeedGraph(ASkateCharacter* Skater, float X, float Y, float W, float H);
	void UpdateFaults(ASkateCharacter* Skater, ASkateArena* Arena);

	float Line(const FString& Text, const FLinearColor& Color);

	float CursorX = 0.f;
	float CursorY = 0.f;

	// Speed history for the graph (cm/s), sampled per frame with timestamps.
	TArray<TPair<double, float>> SpeedHistory;

	// Latched fault timestamps (shown for FaultHoldTime after the last occurrence).
	double BallOutTime = -1000.0;
	double SkaterOutTime = -1000.0;
	double BrakeFaultTime = -1000.0;
	double SmoothedFps = 60.0;
};

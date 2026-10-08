// Ice skating prototype - procedural placeholder sounds (no audio assets needed).
//   glide   - soft band-limited hiss, loudness follows speed
//   brake   - brighter, louder scrape with crackle, follows braking/skid deceleration
//   impact  - short low thump + click (ball contact, board bounce)
// Parameters are written from the game thread and read on the audio render thread.
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include <atomic>
#include "SkateAudio.generated.h"

UCLASS(ClassGroup = (Skate))
class ICEFOOTBALL_API USkateIceSynth : public USynthComponent
{
	GENERATED_BODY()

public:
	USkateIceSynth(const FObjectInitializer& ObjectInitializer);

	/** 0..1 levels, smoothed on the audio thread (~30 ms). */
	void SetLevels(float InGlide, float InBrake);

	/** Strength 0..1, Pitch ~0.5..2. */
	void TriggerImpact(float Strength, float Pitch);
	/** The goal horn: a low two-note chord, about a second. */
	void TriggerHorn();

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	std::atomic<float> GlideTarget{ 0.f };
	std::atomic<float> BrakeTarget{ 0.f };
	std::atomic<int32> ImpactSerial{ 0 };
	std::atomic<float> ImpactStrength{ 0.f };
	std::atomic<float> ImpactPitch{ 1.f };
	std::atomic<int32> HornSerial{ 0 };

	// Audio thread state
	float Rate = 48000.f;
	float Glide = 0.f;
	float Brake = 0.f;
	float LpGlideHi = 0.f;
	float LpGlideLo = 0.f;
	float LpBrakeHi = 0.f;
	float LpBrakeLo = 0.f;
	float Texture = 0.f;
	uint32 Rng = 0x12345678u;
	int32 SeenImpact = 0;
	float ImpactEnv = 0.f;
	float ClickEnv = 0.f;
	float ImpactPhase = 0.f;
	float ImpactFreq = 120.f;
	int32 SeenHorn = 0;
	float HornLeft = 0.f;
	float HornEnv = 0.f;
	float HornPhaseA = 0.f;
	float HornPhaseB = 0.f;
};

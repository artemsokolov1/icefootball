#include "Skate/SkateAudio.h"

namespace SkateAudioDetail
{
	inline float OnePole(float Cutoff, float SampleRate)
	{
		return 1.f - FMath::Exp(-2.f * UE_PI * Cutoff / SampleRate);
	}

	inline float SoftClip(float X)
	{
		return X / (1.f + FMath::Abs(X));
	}
}

USkateIceSynth::USkateIceSynth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 2D (non spatialized) - the camera is fixed and close to the skater.
	bAutoActivate = true;
}

bool USkateIceSynth::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = static_cast<float>(SampleRate > 0 ? SampleRate : 48000);
	return true;
}

void USkateIceSynth::SetLevels(float InGlide, float InBrake)
{
	GlideTarget.store(FMath::Clamp(InGlide, 0.f, 2.f), std::memory_order_relaxed);
	BrakeTarget.store(FMath::Clamp(InBrake, 0.f, 2.f), std::memory_order_relaxed);
}

void USkateIceSynth::TriggerImpact(float Strength, float Pitch)
{
	ImpactStrength.store(FMath::Clamp(Strength, 0.f, 1.5f), std::memory_order_relaxed);
	ImpactPitch.store(FMath::Clamp(Pitch, 0.25f, 4.f), std::memory_order_relaxed);
	ImpactSerial.fetch_add(1, std::memory_order_release);
}

int32 USkateIceSynth::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	using namespace SkateAudioDetail;
	const float GlideT = GlideTarget.load(std::memory_order_relaxed);
	const float BrakeT = BrakeTarget.load(std::memory_order_relaxed);

	const int32 Serial = ImpactSerial.load(std::memory_order_acquire);
	if (Serial != SeenImpact)
	{
		SeenImpact = Serial;
		const float Strength = ImpactStrength.load(std::memory_order_relaxed);
		ImpactEnv = FMath::Max(ImpactEnv, Strength);
		ClickEnv = FMath::Max(ClickEnv, Strength);
		ImpactFreq = 110.f * ImpactPitch.load(std::memory_order_relaxed);
		ImpactPhase = 0.f;
	}

	const float LevelSmooth = OnePole(30.f, Rate);
	const float AGlideHi = OnePole(2400.f, Rate);
	const float AGlideLo = OnePole(350.f, Rate);
	const float ABrakeHi = OnePole(7500.f, Rate);
	const float ABrakeLo = OnePole(900.f, Rate);
	const float ATexture = OnePole(9.f, Rate);
	const float ImpactDecay = FMath::Exp(-1.f / (0.07f * Rate));
	const float ClickDecay = FMath::Exp(-1.f / (0.006f * Rate));
	const float PhaseStep = 2.f * UE_PI / Rate;

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		Glide += (GlideT - Glide) * LevelSmooth;
		Brake += (BrakeT - Brake) * LevelSmooth;

		Rng ^= Rng << 13;
		Rng ^= Rng >> 17;
		Rng ^= Rng << 5;
		const float White = static_cast<float>(Rng & 0xFFFFFF) / 8388607.5f - 1.f;

		// Slow random texture so the glide is not a static hiss.
		Texture += (White - Texture) * ATexture;

		LpGlideHi += (White - LpGlideHi) * AGlideHi;
		LpGlideLo += (White - LpGlideLo) * AGlideLo;
		const float GlideSig = (LpGlideHi - LpGlideLo) * (0.8f + 6.f * FMath::Abs(Texture));

		LpBrakeHi += (White - LpBrakeHi) * ABrakeHi;
		LpBrakeLo += (White - LpBrakeLo) * ABrakeLo;
		const float Crackle = (White > 0.985f) ? 3.f : 1.f;
		const float BrakeSig = (LpBrakeHi - LpBrakeLo) * Crackle;

		ImpactPhase += PhaseStep * ImpactFreq;
		ImpactFreq *= 0.99995f;
		const float Thump = FMath::Sin(ImpactPhase) * ImpactEnv;
		ImpactEnv *= ImpactDecay;
		const float Click = White * ClickEnv;
		ClickEnv *= ClickDecay;

		const float Mix = GlideSig * Glide * 0.9f + BrakeSig * Brake * 1.1f + Thump * 0.8f + Click * 0.35f;
		OutAudio[Index] = SoftClip(Mix) * 0.7f;
	}
	if (ImpactPhase > 2.f * UE_PI * 1000.f)
	{
		ImpactPhase = FMath::Fmod(ImpactPhase, 2.f * UE_PI);
	}
	return NumSamples;
}

// Ice skating prototype - the three switchable tuning presets.
// All presets drive the same movement/contact code; only numbers differ.
#pragma once

#include "SkateTuning.h"

namespace SkateTuningPresets
{
	/** Builds a preset. Balanced == FSkateTuning defaults. */
	FSkateTuning Make(ESkatePreset Preset);

	const char* Name(ESkatePreset Preset);

	constexpr int Count = 3;
}

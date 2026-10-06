// Ice skating prototype - behavioural scenario tests of the skating / contact core.
// Shared by the standalone runner (Tools/SkateSim) and the Unreal automation test
// "IceFootball.Skate.Core". The tests check behaviour (timings, monotonicity, frame-rate
// independence, contact gating, single impulses) - not that constants equal themselves.
#pragma once

#include <string>
#include <vector>

struct FSkateTestResult
{
	explicit FSkateTestResult(const std::string& InName) : Name(InName) {}

	std::string Name;
	bool bPassed = false;
	std::string Details;
};

/** Runs all scenarios. Never aborts on failure; every result carries measured numbers. */
std::vector<FSkateTestResult> RunSkateCoreTests();

/** Human readable metrics table (time to speed, stop time/distance, glide, turn radius...) per preset and FPS. */
std::string SkateCoreMetricsReport();

// Ice skating prototype - layout / speed consistency checks on the test rink courses.
// A simple pure-pursuit "driver" steers the skating model through the slalom, the turning
// circle and the figure eight, and the stop-zone drill brakes at the zone entry. This checks
// that rink sizes, braking distance and turning limits fit together; it is NOT a judgement of
// how the controls feel to a human.
#pragma once

#include "SkateCoreTests.h"

void RunSkateCourseTests(std::vector<FSkateTestResult>& Out);

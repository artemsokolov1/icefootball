// Ice skating prototype - two skaters + goalkeeper checks (passing, receiving, no stealing between
// teammates, skater AI for teammates and opponents, steals, body checks, keeper positioning / saves / goals). Engine independent: a simple 3D ball
// (gravity, ice bounce, damping, net) stands in for Chaos.
#pragma once

#include "SkateCoreTests.h"

void RunSkateTeamTests(std::vector<FSkateTestResult>& Out);

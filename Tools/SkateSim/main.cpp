// Standalone runner for the skating core scenario tests (no Unreal needed).
//   ./build.sh && ./skatesim            -> runs tests, prints metrics, exit code 1 on failure
#include "SkateCoreTests.h"
#include "SkateCourseTests.h"
#include "SkatePoseTests.h"

#include <cstdio>

int main()
{
	std::vector<FSkateTestResult> Results = RunSkateCoreTests();
	RunSkateCourseTests(Results);
	RunSkatePoseTests(Results);
	int Failed = 0;
	for (const FSkateTestResult& R : Results)
	{
		std::printf("[%s] %s\n       %s\n", R.bPassed ? "PASS" : "FAIL", R.Name.c_str(), R.Details.c_str());
		Failed += R.bPassed ? 0 : 1;
	}
	std::printf("\n%s\n", SkateCoreMetricsReport().c_str());
	std::printf("%d/%d passed\n", static_cast<int>(Results.size()) - Failed, static_cast<int>(Results.size()));
	return Failed == 0 ? 0 : 1;
}

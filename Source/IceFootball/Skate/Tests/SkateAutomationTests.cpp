// Unreal automation wrapper for the skating core scenarios.
// Run: Session Frontend > Automation > "IceFootball.Skate.Core", or
//   UnrealEditor-Cmd IceFootball.uproject -ExecCmds="Automation RunTests IceFootball.Skate; Quit" -unattended -nullrhi
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Skate/Tests/SkateCoreTests.h"
#include "Skate/Tests/SkateCourseTests.h"
#include "Skate/Tests/SkatePoseTests.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkateCoreScenarioTest, "IceFootball.Skate.Core",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSkateCoreScenarioTest::RunTest(const FString& Parameters)
{
	std::vector<FSkateTestResult> Results = RunSkateCoreTests();
	RunSkateCourseTests(Results);
	RunSkatePoseTests(Results);
	for (const FSkateTestResult& Result : Results)
	{
		const FString Message = FString::Printf(TEXT("%s: %s"), UTF8_TO_TCHAR(Result.Name.c_str()), UTF8_TO_TCHAR(Result.Details.c_str()));
		if (Result.bPassed)
		{
			AddInfo(Message);
		}
		else
		{
			AddError(Message);
		}
	}
	AddInfo(UTF8_TO_TCHAR(SkateCoreMetricsReport().c_str()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

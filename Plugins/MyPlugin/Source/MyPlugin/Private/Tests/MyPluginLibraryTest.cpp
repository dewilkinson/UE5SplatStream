#include "Misc/AutomationTest.h"
#include "MyPluginLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyPluginAddNumbersTest, "MyPlugin.Library.AddNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMyPluginAddNumbersTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("2 + 3"), UMyPluginLibrary::AddNumbers(2, 3), 5);
	TestEqual(TEXT("negative"), UMyPluginLibrary::AddNumbers(-4, 1), -3);
	return true;
}

#endif

// SplatPakPackageTests.cpp
// Unreal automation tests for the package format core (.gstx). Run with Scripts/Test.ps1 -Filter SplatPak.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SplatPakPackageCoreTests.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplatPakPackageSyntheticTest, "SplatPak.Package.Synthetic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSplatPakPackageSyntheticTest::RunTest(const FString& Parameters)
{
	std::vector<std::string> Failures;
	SplatCoreTests::RunSyntheticPackageTests(Failures);
	for (const std::string& Failure : Failures)
	{
		AddError(UTF8_TO_TCHAR(Failure.c_str()));
	}
	return Failures.empty();
}

// Real scans from the 3DGSStream repo, when it's checked out next to this project (or SPLATPAK_REPO is set).
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FSplatPakPackageSampleTest, "SplatPak.Package.Samples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FSplatPakPackageSampleTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	FString Repo = FPlatformMisc::GetEnvironmentVariable(TEXT("SPLATPAK_REPO"));
	if (Repo.IsEmpty())
	{
		Repo = TEXT("C:/github/3DGSStream");
	}
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(Repo, TEXT("assets")), TEXT("*.gstx"), true, false);
	for (const FString& File : Files)
	{
		OutBeautifiedNames.Add(FPaths::GetBaseFilename(File));
		OutTestCommands.Add(File);
	}
}

bool FSplatPakPackageSampleTest::RunTest(const FString& File)
{
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File))
	{
		AddError(FString::Printf(TEXT("Could not read %s"), *File));
		return false;
	}
	std::vector<std::string> Failures;
	SplatCoreTests::RunRealPackageChecks(Bytes.GetData(), Bytes.Num(), Failures);
	for (const std::string& Failure : Failures)
	{
		AddError(FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(File), UTF8_TO_TCHAR(Failure.c_str())));
	}
	return Failures.empty();
}

#endif

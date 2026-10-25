// SurfelPackageCoreTests.h
// Engine-independent tests of SurfelPackageCore, shared by the Unreal automation test
// (SurfelPackageTests.cpp) and the native harness. Each returns true on success and appends
// one line per failed check to OutFailures.

#pragma once

#include <string>
#include <vector>

namespace SurfelsCoreTests
{
	// Builds small surfel8 and splat20 packages in memory with the codec's own encoder, then checks
	// level selection, meshlet splitting, record round-trips and rejection of corrupt files.
	bool RunSyntheticPackageTests(std::vector<std::string>& OutFailures);

	// Decodes the coarsest and finest levels of a real package and checks every position against
	// the header bounds. Data is the whole package file.
	bool RunRealPackageChecks(const unsigned char* Data, unsigned long long Size, std::vector<std::string>& OutFailures);
}

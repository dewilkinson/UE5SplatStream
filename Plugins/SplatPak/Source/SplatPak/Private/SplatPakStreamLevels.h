// SplatPakStreamLevels.h
// Every level of a package, as the streaming runtime (StreamRuntime.h, in the GSTx codec library) takes them: the
// 20-byte records and their positions, each level sorted along one Morton curve over the package bounds, so the
// runtime can nest each level's blocks under the coarser level's (StreamRuntime::InitStreamingSimulation). Built once
// per asset (USplatPakAsset::GetStreamLevels) and shared by every component that streams it.

#pragma once

#include "CoreMinimal.h"
#include <vector>
#include <StreamRuntime.h>

struct FSplatPakStreamLevels
{
	std::vector<Splat::ResidentLOD> Levels; // Finest first
	Splat::XMFLOAT3 BoundsMin = { 0, 0, 0 }, BoundsMax = { 0, 0, 0 };
	float ScaleLog2Min = 0.0f, ScaleLog2Max = 0.0f;
	uint32 SHRecordBytes = 0; // The package's harmonics per splat (counted in the simulated download)
	uint32 SHDegree = 0;      // Of the harmonics in the levels' splatSH; 0 when they were not read (bWithSH)
	double Seconds = 0.0;     // Time taken to build
};
using FSplatPakStreamLevelsPtr = TSharedPtr<const FSplatPakStreamLevels, ESPMode::ThreadSafe>;

// Decodes every level of the package in Data (a whole .gstx) into Out. False with a reason when it does not decode.
// bWithSH: also each level's spherical harmonics (ResidentLOD::splatSH), for view-dependent colour.
bool BuildSplatStreamLevels(const uint8* Data, uint64 Size, FSplatPakStreamLevels& Out, FString& OutError, bool bWithSH);

// SplatPakStreamLevels.cpp

#include "SplatPakStreamLevels.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "SplatPakPackageCore.h"
#include <algorithm>
#include <cstring>

namespace
{
	// The runtime's Morton code (SpatialOctree::MortonCodeInFrame in 3DGSStream): the levels must be sorted with exactly
	// the code the runtime cuts and nests them by.
	uint32 Dilate10Bit(uint32 X)
	{
		X &= 0x000003ff;
		X = (X ^ (X << 16)) & 0xff0000ff;
		X = (X ^ (X << 8)) & 0x0300f00f;
		X = (X ^ (X << 4)) & 0x030c30c3;
		X = (X ^ (X << 2)) & 0x09249249;
		return X;
	}

	uint32 MortonCodeInFrame(const SplatCore::FVec3& P, const Splat::XMFLOAT3& FrameMin, const Splat::XMFLOAT3& FrameExtent)
	{
		const float X = std::max(0.0f, std::min(1.0f, (P.X - FrameMin.x) / FrameExtent.x));
		const float Y = std::max(0.0f, std::min(1.0f, (P.Y - FrameMin.y) / FrameExtent.y));
		const float Z = std::max(0.0f, std::min(1.0f, (P.Z - FrameMin.z) / FrameExtent.z));
		return (Dilate10Bit((uint32)(Z * 1023.0f)) << 2) | (Dilate10Bit((uint32)(Y * 1023.0f)) << 1) | Dilate10Bit((uint32)(X * 1023.0f));
	}
}

bool BuildSplatStreamLevels(const uint8* Data, uint64 Size, FSplatPakStreamLevels& Out, FString& OutError)
{
	const double Start = FPlatformTime::Seconds();
	Out = FSplatPakStreamLevels();
	SplatCore::FPackageInfo Info;
	std::string Error;
	if (!SplatCore::ReadPackageInfo(Data, Size, Info, Error))
	{
		OutError = UTF8_TO_TCHAR(Error.c_str());
		return false;
	}
	if (Info.Format != SplatCore::ERecordFormat::Splat20)
	{
		OutError = TEXT("Only 3D Gaussian packages (20-byte records) stream.");
		return false;
	}
	const SplatCore::FFileHeader& H = Info.Header;
	Out.BoundsMin = Splat::XMFLOAT3(H.BoundsMin.X, H.BoundsMin.Y, H.BoundsMin.Z);
	Out.BoundsMax = Splat::XMFLOAT3(H.BoundsMax.X, H.BoundsMax.Y, H.BoundsMax.Z);
	Out.ScaleLog2Min = H.SplatScaleLog2Min;
	Out.ScaleLog2Max = H.SplatScaleLog2Max;
	Out.SHRecordBytes = H.SHRecordBytes;
	const Splat::XMFLOAT3 FrameExtent(std::max(1e-4f, Out.BoundsMax.x - Out.BoundsMin.x), std::max(1e-4f, Out.BoundsMax.y - Out.BoundsMin.y),
		std::max(1e-4f, Out.BoundsMax.z - Out.BoundsMin.z));

	const int32 NumLevels = (int32)Info.MaxLevel + 1;
	Out.Levels.resize(NumLevels);
	std::vector<std::string> Errors(NumLevels);
	std::vector<bool> Ok(NumLevels, true);
	// Each level on its own core: decode the chunks that carry it, then sort it along the curve
	ParallelFor(NumLevels, [&](int32 L)
	{
		SplatCore::FDecodedLevel Level;
		if (!SplatCore::DecodeLevel(Data, Size, Info, (uint32)L, Level, Errors[L], true))
		{
			Ok[L] = false;
			return;
		}
		const uint32 N = Level.SplatCount;
		std::vector<SplatCore::FVec3> Positions(N);
		std::vector<std::pair<uint32, uint32>> Order(N); // (code, index)
		for (uint32 i = 0; i < N; i++)
		{
			Positions[i] = SplatCore::DecodePosition(Level, i);
			Order[i] = { MortonCodeInFrame(Positions[i], Out.BoundsMin, FrameExtent), i };
		}
		std::stable_sort(Order.begin(), Order.end(), [](const std::pair<uint32, uint32>& A, const std::pair<uint32, uint32>& B) { return A.first < B.first; });

		Splat::ResidentLOD& R = Out.Levels[L];
		R.rawSplats.resize(N);
		R.splats.resize(N);
		for (uint32 k = 0; k < N; k++)
		{
			const uint32 i = Order[k].second;
			Splat::SplatPoint& P = R.rawSplats[k];
			P.position = Splat::XMFLOAT3(Positions[i].X, Positions[i].Y, Positions[i].Z);
			memcpy(&R.splats[k], Level.Records.data() + (size_t)i * Level.RecordBytes, sizeof(Splat::PackedSplatGPU));
		}
	});
	for (int32 L = 0; L < NumLevels; L++)
	{
		// A level no chunk carries is empty, which the runtime skips; a payload that does not decode fails the package
		if (!Ok[L] && Errors[L] != "The requested level holds no splats.")
		{
			OutError = FString::Printf(TEXT("Level %d: %s"), L, UTF8_TO_TCHAR(Errors[L].c_str()));
			return false;
		}
	}
	Out.Seconds = FPlatformTime::Seconds() - Start;
	return true;
}

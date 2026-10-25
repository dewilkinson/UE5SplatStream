// SurfelPackageCoreTests.cpp

#include "SurfelPackageCoreTests.h"

#include "SurfelPackageCore.h"
#include "ByteShuffle.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace SurfelsCoreTests
{
	using namespace SurfelsCore;

	namespace
	{
		struct FCheck
		{
			std::vector<std::string>& Failures;
			bool Ok = true;
			void operator()(bool Condition, const std::string& What)
			{
				if (!Condition) { Failures.push_back(What); Ok = false; }
			}
		};

		// Deterministic xorshift so failures reproduce.
		struct FRng
		{
			uint32_t S = 0x9E3779B9u;
			uint32_t Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
			float Unit() { return (Next() & 0xFFFFFF) / float(0xFFFFFF); }
		};

		struct FLodSpec { uint32_t Level; uint32_t Count; };

		// One package: header + payloads + embedded manifest, laid out the way StreamPackager writes it.
		std::vector<uint8_t> BuildPackage(ERecordFormat Format, const std::vector<std::vector<FLodSpec>>& Chunks,
			std::vector<std::vector<std::vector<uint8_t>>>& OutRecords)
		{
			const uint32_t RecordBytes = (Format == ERecordFormat::Splat20) ? 20u : 8u;
			FFileHeader H = {};
			H.Magic = kMagic;
			H.Version = kMaxSupportedVersion;
			H.PayloadCodec = 0; // The run-length stage (ByteShuffle::CompressShuffled below); v9+ writers choose per package
			H.NumChunks = (uint32_t)Chunks.size();
			H.BoundsMin = { -1.0f, 0.0f, -2.0f };
			H.BoundsMax = { 3.0f, 2.0f, 1.0f };
			H.SplatRadius = 1.0f;
			H.SurfelFormat = (uint32_t)Format;
			H.SplatRecordBytes = RecordBytes;
			H.SplatScaleLog2Min = -10.0f;
			H.SplatScaleLog2Max = 0.0f;

			std::vector<uint8_t> File(sizeof(FFileHeader));
			std::vector<FChunk> Manifest(Chunks.size());
			FRng Rng;
			OutRecords.assign(Chunks.size(), {});
			for (size_t c = 0; c < Chunks.size(); c++)
			{
				Manifest[c].Record.ChunkId = (uint32_t)c;
				Manifest[c].Record.NumLODs = (uint32_t)Chunks[c].size();
				for (const FLodSpec& Spec : Chunks[c])
				{
					std::vector<uint8_t> Records((size_t)Spec.Count * RecordBytes);
					for (uint32_t i = 0; i < Spec.Count; i++)
					{
						uint32_t W[5] = { Rng.Next(), Rng.Next(), Rng.Next(), Rng.Next(), Rng.Next() };
						if (Format == ERecordFormat::Surfel8)
						{
							W[0] = (Rng.Next() % 1024) | ((Rng.Next() % 1024) << 10) | ((Rng.Next() % 1024) << 20) | ((Rng.Next() & 3) << 30);
						}
						memcpy(&Records[(size_t)i * RecordBytes], W, RecordBytes);
					}
					std::vector<uint8_t> Shuffled = Surfels::ByteShuffle::Shuffle(Records.data(), Spec.Count, RecordBytes);
					std::vector<uint8_t> Compressed = Surfels::ByteShuffle::CompressShuffled(Shuffled);

					FChunkLODHeader Lod = {};
					Lod.LodLevel = Spec.Level;
					Lod.SurfelCount = Spec.Count;
					Lod.UncompressedByteSize = Spec.Count * RecordBytes;
					Lod.CompressedByteSize = (uint32_t)Compressed.size();
					Lod.FileOffset = File.size();
					Manifest[c].Lods.push_back(Lod);
					File.insert(File.end(), Compressed.begin(), Compressed.end());
					OutRecords[c].push_back(std::move(Records));
				}
			}

			H.ManifestOffset = File.size();
			for (const FChunk& Chunk : Manifest)
			{
				const uint8_t* Rec = reinterpret_cast<const uint8_t*>(&Chunk.Record);
				File.insert(File.end(), Rec, Rec + sizeof(FChunkManifestRecord));
				const uint8_t* Lods = reinterpret_cast<const uint8_t*>(Chunk.Lods.data());
				File.insert(File.end(), Lods, Lods + sizeof(FChunkLODHeader) * Chunk.Lods.size());
			}
			memcpy(File.data(), &H, sizeof(H));
			return File;
		}

		void CheckFormat(ERecordFormat Format, FCheck& Check)
		{
			const char* Name = (Format == ERecordFormat::Surfel8) ? "surfel8" : "splat20";
			auto Tag = [Name](const std::string& S) { return std::string(Name) + ": " + S; };
			const uint32_t RecordBytes = (Format == ERecordFormat::Splat20) ? 20u : 8u;

			// Chunk 0 has levels 0..2, chunk 1 only level 0 (as packages' last chunk often does).
			std::vector<std::vector<std::vector<uint8_t>>> Records;
			const std::vector<uint8_t> File = BuildPackage(Format, { { { 0, 200 }, { 1, 100 }, { 2, 30 } }, { { 0, 70 } } }, Records);

			FPackageInfo Info;
			std::string Error;
			Check(ReadPackageInfo(File.data(), File.size(), Info, Error), Tag("ReadPackageInfo failed: " + Error));
			Check(Info.Format == Format && Info.RecordBytes == RecordBytes, Tag("format/record size not read back"));
			Check(Info.MaxLevel == 2, Tag("MaxLevel should be 2"));

			// Coarsest: chunk 0 level 2 (30 surfels, 1 meshlet) + chunk 1 level 0 (70 surfels, 2 meshlets).
			FDecodedLevel Coarse;
			Check(DecodeLevel(File.data(), File.size(), Info, 99, Coarse, Error), Tag("DecodeLevel(coarsest) failed: " + Error));
			Check(Coarse.SurfelCount == 100, Tag("coarsest should hold 100 surfels"));
			Check(Coarse.Meshlets.size() == 3, Tag("coarsest should split into 3 meshlets"));
			Check(Coarse.Records.size() == 100 * RecordBytes, Tag("coarsest record bytes"));
			Check(Coarse.Records.size() >= 30 * RecordBytes && memcmp(Coarse.Records.data(), Records[0][2].data(), 30 * RecordBytes) == 0, Tag("chunk 0 level 2 records do not round-trip"));
			Check(Coarse.Records.size() >= 100 * RecordBytes && memcmp(Coarse.Records.data() + 30 * RecordBytes, Records[1][0].data(), 70 * RecordBytes) == 0, Tag("chunk 1 level 0 records do not round-trip"));

			// Finest: 200 + 70 surfels, ceil(200/64) + ceil(70/64) = 4 + 2 meshlets, each at most 64.
			FDecodedLevel Fine;
			Check(DecodeLevel(File.data(), File.size(), Info, 0, Fine, Error), Tag("DecodeLevel(0) failed: " + Error));
			Check(Fine.SurfelCount == 270 && Fine.Meshlets.size() == 6, Tag("level 0 should hold 270 surfels in 6 meshlets"));
			uint32_t Expected = 0;
			bool bContiguous = true, bBoundsHold = true, bSizeOk = true;
			for (const FMeshlet& M : Fine.Meshlets)
			{
				bContiguous &= (M.SurfelOffset == Expected);
				bSizeOk &= (M.SurfelCount >= 1 && M.SurfelCount <= kSurfelsPerMeshlet);
				for (uint32_t i = 0; i < M.SurfelCount; i++)
				{
					const FVec3 P = DecodePosition(Fine, M.SurfelOffset + i);
					const float Eps = 1e-4f;
					bBoundsHold &= P.X >= M.AabbMin.X - Eps && P.X <= M.AabbMin.X + M.AabbExtents.X + Eps
						&& P.Y >= M.AabbMin.Y - Eps && P.Y <= M.AabbMin.Y + M.AabbExtents.Y + Eps
						&& P.Z >= M.AabbMin.Z - Eps && P.Z <= M.AabbMin.Z + M.AabbExtents.Z + Eps;
				}
				Expected += M.SurfelCount;
			}
			Check(bContiguous && Expected == Fine.SurfelCount, Tag("meshlets must tile the records contiguously"));
			Check(bSizeOk, Tag("meshlet sizes must be 1..64"));
			Check(bBoundsHold, Tag("every surfel must lie inside its meshlet's bounds"));
			Check(Fine.PointSpacing > 0.0f, Tag("point spacing should be measured"));

			// Corrupt files must be rejected, not decoded.
			std::vector<uint8_t> Bad = File;
			Bad[0] ^= 0xFF;
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("bad magic accepted"));

			Bad = File;
			Bad.resize(File.size() - 8);
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("truncated manifest accepted"));

			Bad = File;
			FFileHeader H;
			memcpy(&H, Bad.data(), sizeof(H));
			FChunkLODHeader Lod;
			const size_t LodAt = (size_t)H.ManifestOffset + sizeof(FChunkManifestRecord);
			memcpy(&Lod, Bad.data() + LodAt, sizeof(Lod));
			Lod.SurfelCount += 1; // Payload size no longer matches
			memcpy(Bad.data() + LodAt, &Lod, sizeof(Lod));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("mismatched payload size accepted"));

			Bad = File;
			FFileHeader Future = H;
			Future.Version = kMaxSupportedVersion + 1;
			memcpy(Bad.data(), &Future, sizeof(Future));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("future format version accepted"));

			// An older .sflw (signature SFLW, version 8) still reads; GSTX with a pre-10 version, or SFLW with 10, does not
			Bad = File;
			FFileHeader Legacy = H;
			Legacy.Magic = kMagicSFLW;
			Legacy.Version = 8;
			memcpy(Bad.data(), &Legacy, sizeof(Legacy));
			Check(ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("SFLW version 8 package rejected: ") + Error);
			Legacy.Magic = kMagic;
			memcpy(Bad.data(), &Legacy, sizeof(Legacy));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("GSTX signature with version 8 accepted"));
			Legacy.Magic = kMagicSFLW;
			Legacy.Version = kMaxSupportedVersion;
			memcpy(Bad.data(), &Legacy, sizeof(Legacy));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("SFLW signature with version 10 accepted"));
		}
	}

	bool RunSyntheticPackageTests(std::vector<std::string>& OutFailures)
	{
		FCheck Check{ OutFailures };

		// Oct16: the signed encoding PackNormalOct16 writes, for the six axis directions.
		const struct { int8_t X, Y; float Ex, Ey, Ez; } Axes[] = {
			{ 0, 0, 0, 0, 1 }, { 127, 0, 1, 0, 0 }, { -127, 0, -1, 0, 0 }, { 0, 127, 0, 1, 0 }, { 0, -127, 0, -1, 0 },
		};
		for (const auto& A : Axes)
		{
			const FVec3 N = DecodeOct16((uint16_t)(((uint8_t)A.Y << 8) | (uint8_t)A.X));
			Check(std::fabs(N.X - A.Ex) < 1e-3f && std::fabs(N.Y - A.Ey) < 1e-3f && std::fabs(N.Z - A.Ez) < 1e-3f, "DecodeOct16 axis mismatch");
		}

		CheckFormat(ERecordFormat::Surfel8, Check);
		CheckFormat(ERecordFormat::Splat20, Check);
		return Check.Ok;
	}

	bool RunRealPackageChecks(const unsigned char* Data, unsigned long long Size, std::vector<std::string>& OutFailures)
	{
		FCheck Check{ OutFailures };
		FPackageInfo Info;
		std::string Error;
		if (!ReadPackageInfo(Data, Size, Info, Error))
		{
			Check(false, "ReadPackageInfo failed: " + Error);
			return false;
		}
		for (uint32_t Level : { Info.MaxLevel, 0u })
		{
			FDecodedLevel L;
			if (!DecodeLevel(Data, Size, Info, Level, L, Error))
			{
				Check(false, "DecodeLevel failed: " + Error);
				continue;
			}
			const FFileHeader& H = Info.Header;
			uint32_t Outside = 0;
			uint64_t InMeshlets = 0;
			for (uint32_t i = 0; i < L.SurfelCount; i++)
			{
				const FVec3 P = DecodePosition(L, i);
				const float Eps = 1e-3f;
				Outside += (P.X < H.BoundsMin.X - Eps || P.X > H.BoundsMax.X + Eps || P.Y < H.BoundsMin.Y - Eps || P.Y > H.BoundsMax.Y + Eps
					|| P.Z < H.BoundsMin.Z - Eps || P.Z > H.BoundsMax.Z + Eps) ? 1 : 0;
			}
			for (const FMeshlet& M : L.Meshlets) InMeshlets += M.SurfelCount;
			char Buf[160];
			snprintf(Buf, sizeof(Buf), "level %u: %u of %u positions outside the header bounds", Level, Outside, L.SurfelCount);
			Check(Outside == 0, Buf);
			Check(InMeshlets == L.SurfelCount, "meshlets do not cover every surfel");
			Check(L.PointSpacing > 0.0f, "point spacing not measured");
		}
		return Check.Ok;
	}
}

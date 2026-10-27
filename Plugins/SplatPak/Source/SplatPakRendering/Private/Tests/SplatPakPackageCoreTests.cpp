// SplatPakPackageCoreTests.cpp

#include "SplatPakPackageCoreTests.h"

#include "SplatPakPackageCore.h"
#include "ByteShuffle.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace SplatCoreTests
{
	using namespace SplatCore;

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
			H.Version = kFormatVersion;
			H.PayloadCodec = 0; // The run-length stage (ByteShuffle::CompressShuffled below); v9+ writers choose per package
			H.NumChunks = (uint32_t)Chunks.size();
			H.BoundsMin = { -1.0f, 0.0f, -2.0f };
			H.BoundsMax = { 3.0f, 2.0f, 1.0f };
			H.SplatRadius = 1.0f;
			H.RecordFormat = (uint32_t)Format;
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
						if (Format == ERecordFormat::Point8)
						{
							W[0] = (Rng.Next() % 1024) | ((Rng.Next() % 1024) << 10) | ((Rng.Next() % 1024) << 20) | ((Rng.Next() & 3) << 30);
						}
						memcpy(&Records[(size_t)i * RecordBytes], W, RecordBytes);
					}
					std::vector<uint8_t> Shuffled = Splat::ByteShuffle::Shuffle(Records.data(), Spec.Count, RecordBytes);
					std::vector<uint8_t> Compressed = Splat::ByteShuffle::CompressShuffled(Shuffled);

					FChunkLODHeader Lod = {};
					Lod.LodLevel = Spec.Level;
					Lod.SplatCount = Spec.Count;
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
			const char* Name = (Format == ERecordFormat::Point8) ? "point8" : "splat20";
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

			// Coarsest: chunk 0 level 2 (30 splats, 1 meshlet) + chunk 1 level 0 (70 splats, 2 meshlets).
			FDecodedLevel Coarse;
			Check(DecodeLevel(File.data(), File.size(), Info, 99, Coarse, Error), Tag("DecodeLevel(coarsest) failed: " + Error));
			Check(Coarse.SplatCount == 100, Tag("coarsest should hold 100 splats"));
			Check(Coarse.Meshlets.size() == 3, Tag("coarsest should split into 3 meshlets"));
			Check(Coarse.Records.size() == 100 * RecordBytes, Tag("coarsest record bytes"));
			Check(Coarse.Records.size() >= 30 * RecordBytes && memcmp(Coarse.Records.data(), Records[0][2].data(), 30 * RecordBytes) == 0, Tag("chunk 0 level 2 records do not round-trip"));
			Check(Coarse.Records.size() >= 100 * RecordBytes && memcmp(Coarse.Records.data() + 30 * RecordBytes, Records[1][0].data(), 70 * RecordBytes) == 0, Tag("chunk 1 level 0 records do not round-trip"));

			// Finest: 200 + 70 splats, ceil(200/64) + ceil(70/64) = 4 + 2 meshlets, each at most 64.
			FDecodedLevel Fine;
			Check(DecodeLevel(File.data(), File.size(), Info, 0, Fine, Error), Tag("DecodeLevel(0) failed: " + Error));
			Check(Fine.SplatCount == 270 && Fine.Meshlets.size() == 6, Tag("level 0 should hold 270 splats in 6 meshlets"));
			uint32_t Expected = 0;
			bool bContiguous = true, bBoundsHold = true, bSizeOk = true;
			for (const FMeshlet& M : Fine.Meshlets)
			{
				bContiguous &= (M.SplatOffset == Expected);
				bSizeOk &= (M.SplatCount >= 1 && M.SplatCount <= kSplatsPerMeshlet);
				for (uint32_t i = 0; i < M.SplatCount; i++)
				{
					const FVec3 P = DecodePosition(Fine, M.SplatOffset + i);
					const float Eps = 1e-4f;
					bBoundsHold &= P.X >= M.AabbMin.X - Eps && P.X <= M.AabbMin.X + M.AabbExtents.X + Eps
						&& P.Y >= M.AabbMin.Y - Eps && P.Y <= M.AabbMin.Y + M.AabbExtents.Y + Eps
						&& P.Z >= M.AabbMin.Z - Eps && P.Z <= M.AabbMin.Z + M.AabbExtents.Z + Eps;
				}
				Expected += M.SplatCount;
			}
			Check(bContiguous && Expected == Fine.SplatCount, Tag("meshlets must tile the records contiguously"));
			Check(bSizeOk, Tag("meshlet sizes must be 1..64"));
			Check(bBoundsHold, Tag("every splat must lie inside its meshlet's bounds"));
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
			Lod.SplatCount += 1; // Payload size no longer matches
			memcpy(Bad.data() + LodAt, &Lod, sizeof(Lod));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("mismatched payload size accepted"));

			Bad = File;
			FFileHeader Future = H;
			Future.Version = kFormatVersion + 1;
			memcpy(Bad.data(), &Future, sizeof(Future));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("future format version accepted"));

			// Earlier versions are no longer read, nor the old SFLW signature
			Bad = File;
			FFileHeader Old = H;
			Old.Version = kFormatVersion - 1;
			memcpy(Bad.data(), &Old, sizeof(Old));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("earlier format version accepted"));
			Old = H;
			Old.Magic = 0x574C4653; // "SFLW"
			memcpy(Bad.data(), &Old, sizeof(Old));
			Check(!ReadPackageInfo(Bad.data(), Bad.size(), Info, Error), Tag("SFLW signature accepted"));
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
			for (uint32_t i = 0; i < L.SplatCount; i++)
			{
				const FVec3 P = DecodePosition(L, i);
				const float Eps = 1e-3f;
				Outside += (P.X < H.BoundsMin.X - Eps || P.X > H.BoundsMax.X + Eps || P.Y < H.BoundsMin.Y - Eps || P.Y > H.BoundsMax.Y + Eps
					|| P.Z < H.BoundsMin.Z - Eps || P.Z > H.BoundsMax.Z + Eps) ? 1 : 0;
			}
			for (const FMeshlet& M : L.Meshlets) InMeshlets += M.SplatCount;
			char Buf[160];
			snprintf(Buf, sizeof(Buf), "level %u: %u of %u positions outside the header bounds", Level, Outside, L.SplatCount);
			Check(Outside == 0, Buf);
			Check(InMeshlets == L.SplatCount, "meshlets do not cover every splat");
			Check(L.PointSpacing > 0.0f, "point spacing not measured");
		}

		// Shadow mesh: present, three levels each at most half the previous, valid indices, inside the bounds
		{
			std::vector<FShadowMeshLevel> Mesh;
			Check(HasShadowMesh(Info), "a package without a shadow mesh section");
			if (HasShadowMesh(Info))
			{
				const bool bRead = ReadShadowMesh(Data, Size, Info, Mesh, Error);
				Check(bRead, "ReadShadowMesh failed: " + Error);
				Check(Mesh.size() == 3, "the shadow mesh does not have three levels");
				const FFileHeader& H = Info.Header;
				const float Dx = H.BoundsMax.X - H.BoundsMin.X, Dy = H.BoundsMax.Y - H.BoundsMin.Y, Dz = H.BoundsMax.Z - H.BoundsMin.Z;
				const float Margin = 0.05f * std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
				for (size_t l = 0; l < Mesh.size(); l++)
				{
					const FShadowMeshLevel& M = Mesh[l];
					const size_t Triangles = M.Indices.size() / 3;
					char Buf[160];
					snprintf(Buf, sizeof(Buf), "shadow mesh level %zu has %zu triangles", l, Triangles);
					Check(Triangles > 0 && M.Indices.size() % 3 == 0, Buf);
					if (l > 0) Check(Triangles * 2 <= Mesh[l - 1].Indices.size() / 3 + 2, std::string(Buf) + ", more than half the previous level");
					uint32_t Bad = 0, Outside = 0;
					for (uint32_t i : M.Indices) Bad += i >= M.Positions.size() ? 1 : 0;
					for (const FVec3& P : M.Positions)
						Outside += (P.X < H.BoundsMin.X - Margin || P.X > H.BoundsMax.X + Margin || P.Y < H.BoundsMin.Y - Margin || P.Y > H.BoundsMax.Y + Margin ||
							P.Z < H.BoundsMin.Z - Margin || P.Z > H.BoundsMax.Z + Margin) ? 1 : 0;
					Check(Bad == 0, "shadow mesh indices past the vertex count");
					Check(Outside == 0, "shadow mesh vertices outside the model bounds");
					Check(M.Colours.size() == M.Positions.size(), "shadow mesh colours do not match the vertices");
				}
			}
		}

		// Occluder mesh: decoded with the level, triangles with valid indices
		if (HasOccluderMesh(Info))
		{
			FDecodedLevel L;
			Check(DecodeLevel(Data, Size, Info, 99, L, Error), "DecodeLevel(coarsest) failed: " + Error);
			Check(!L.OccluderIndices.empty() && L.OccluderIndices.size() % 3 == 0, "the occluder mesh did not decode with the level");
			uint32_t Bad = 0;
			for (uint32_t i : L.OccluderIndices) Bad += i >= L.OccluderPositions.size() ? 1 : 0;
			Check(Bad == 0, "occluder mesh indices past the vertex count");
		}
		return Check.Ok;
	}
}

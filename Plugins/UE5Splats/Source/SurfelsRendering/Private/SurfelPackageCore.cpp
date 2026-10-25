// SurfelPackageCore.cpp
// See SurfelPackageCore.h. Decoding mirrors StreamPackager::LoadPackage in the 3DGSStream repo: each chunk
// LOD payload goes through the codec library's PayloadDecode (run-length or Brotli, per the header), then is
// unshuffled back to interleaved records.

#include "SurfelPackageCore.h"

#include "ByteShuffle.h"
#include "PayloadDecode.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace SurfelsCore
{
	namespace
	{
		// Bytes a writer of each format version put in its header (the struct grew by appending).
		size_t HeaderBytesForVersion(uint32_t Version)
		{
			if (Version < 2) return offsetof(FFileHeader, SplatRadius);
			if (Version < 3) return offsetof(FFileHeader, OcclusionVoxelCount);
			if (Version < 4) return offsetof(FFileHeader, ManifestOffset);
			if (Version < 5) return offsetof(FFileHeader, SourceFileBytes);
			if (Version < 6) return offsetof(FFileHeader, OcclusionMipCount);
			if (Version < 7) return offsetof(FFileHeader, DetailGridDims);
			if (Version < 8) return offsetof(FFileHeader, SurfelFormat);
			if (Version < 9) return offsetof(FFileHeader, PayloadCodec);
			return sizeof(FFileHeader);
		}

		std::string Format(const char* Fmt, unsigned long long A = 0, unsigned long long B = 0, unsigned long long C = 0)
		{
			char Buf[256];
			snprintf(Buf, sizeof(Buf), Fmt, A, B, C);
			return Buf;
		}

		FVec3 Sub(FVec3 A, FVec3 B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }

		// Median nearest-neighbour distance over a sample of the level (ComputeLodSpacing in SurfelsApp.cpp).
		float ComputePointSpacing(const FDecodedLevel& Level)
		{
			const uint32_t Total = Level.SurfelCount;
			if (Total < 8) return 0.0f;
			std::vector<FVec3> Pts(Total);
			for (uint32_t i = 0; i < Total; i++) Pts[i] = DecodePosition(Level, i);

			const FVec3 E = Sub(Level.BoundsMax, Level.BoundsMin);
			const float Ex = std::max(1e-6f, E.X), Ey = std::max(1e-6f, E.Y), Ez = std::max(1e-6f, E.Z);
			const float Area = 2.0f * (Ex * Ey + Ey * Ez + Ez * Ex);
			const float Cell = std::max(1e-5f, std::sqrt(Area / (float)Total) * 3.0f);
			auto CellKey = [](int X, int Y, int Z)
			{
				return ((uint64_t)(uint32_t)(X + 0x100000) << 42) | ((uint64_t)(uint32_t)(Y + 0x100000) << 21) | (uint64_t)(uint32_t)(Z + 0x100000);
			};
			auto CellOf = [&](const FVec3& P, int& X, int& Y, int& Z)
			{
				X = (int)std::floor((P.X - Level.BoundsMin.X) / Cell);
				Y = (int)std::floor((P.Y - Level.BoundsMin.Y) / Cell);
				Z = (int)std::floor((P.Z - Level.BoundsMin.Z) / Cell);
			};

			std::vector<std::pair<uint64_t, uint32_t>> Grid;
			Grid.reserve(Total);
			for (uint32_t i = 0; i < Total; i++)
			{
				int X, Y, Z;
				CellOf(Pts[i], X, Y, Z);
				Grid.emplace_back(CellKey(X, Y, Z), i);
			}
			std::sort(Grid.begin(), Grid.end());

			const uint32_t Samples = std::min<uint32_t>(Total, 60000);
			const uint32_t Stride = std::max<uint32_t>(1, Total / Samples);
			std::vector<float> Dists;
			Dists.reserve(Samples);
			for (uint32_t s = 0; s < Total; s += Stride)
			{
				const FVec3& P = Pts[s];
				int X, Y, Z;
				CellOf(P, X, Y, Z);
				float Best = FLT_MAX;
				for (int Dz = -1; Dz <= 1; Dz++) for (int Dy = -1; Dy <= 1; Dy++) for (int Dx = -1; Dx <= 1; Dx++)
				{
					const uint64_t K = CellKey(X + Dx, Y + Dy, Z + Dz);
					auto It = std::lower_bound(Grid.begin(), Grid.end(), std::make_pair(K, 0u));
					for (; It != Grid.end() && It->first == K; ++It)
					{
						if (It->second == s) continue;
						const FVec3 D = Sub(Pts[It->second], P);
						Best = std::min(Best, D.X * D.X + D.Y * D.Y + D.Z * D.Z);
					}
				}
				if (Best < FLT_MAX) Dists.push_back(std::sqrt(Best));
			}
			if (Dists.empty()) return 0.0f;
			std::nth_element(Dists.begin(), Dists.begin() + Dists.size() / 2, Dists.end());
			return Dists[Dists.size() / 2];
		}
	}

	bool ReadPackageInfo(const uint8_t* Data, uint64_t Size, FPackageInfo& Out, std::string& OutError)
	{
		Out = FPackageInfo();
		FFileHeader& H = Out.Header;
		const size_t HeaderBytes = (size_t)std::min<uint64_t>(Size, sizeof(FFileHeader));
		if (HeaderBytes < 8) { OutError = "The file is too small to be a .gstx package."; return false; }
		memcpy(&H, Data, HeaderBytes);

		if (H.Magic != kMagic && H.Magic != kMagicSFLW) { OutError = Format("Not a .gstx package: signature 0x%08llX, expected 0x%08llX (GSTX) or, for an older .sflw, 0x%08llX (SFLW).", H.Magic, kMagic, kMagicSFLW); return false; }
		if (H.Version == 0 || H.Version > kMaxSupportedVersion) { OutError = Format("Package format version %llu is not supported (this plugin reads 1 to %llu).", H.Version, kMaxSupportedVersion); return false; }
		if ((H.Magic == kMagic) != (H.Version >= 10)) { OutError = Format("The header is inconsistent: signature 0x%08llX with format version %llu (GSTX starts at version 10, SFLW ends at 9).", H.Magic, H.Version); return false; }
		if (HeaderBytes < HeaderBytesForVersion(H.Version)) { OutError = "The file is truncated inside its header."; return false; }
		if (H.Version < 4) { OutError = "Pre-v4 packages keep their manifest in a companion .json, which this plugin does not read. Re-bake the package in SplatLab."; return false; }

		// Zero whatever a shorter, older header did not contain (it read into the payload).
		memset(reinterpret_cast<uint8_t*>(&H) + HeaderBytesForVersion(H.Version), 0, sizeof(FFileHeader) - HeaderBytesForVersion(H.Version));

		if (H.NumChunks == 0 || H.NumChunks > 16u * 1024u * 1024u) { OutError = Format("Implausible chunk count %llu; the header is corrupt.", H.NumChunks); return false; }
		if (H.ManifestOffset == 0 || H.ManifestOffset >= Size) { OutError = "The chunk manifest offset lies outside the file; the package is corrupt or truncated."; return false; }

		Out.Format = (H.Version >= 8) ? (ERecordFormat)H.SurfelFormat : ERecordFormat::Surfel8;
		if (Out.Format != ERecordFormat::Surfel8 && Out.Format != ERecordFormat::Splat20) { OutError = Format("Unknown surfel format %llu.", H.SurfelFormat); return false; }
		Out.RecordBytes = (Out.Format == ERecordFormat::Splat20) ? 20u : 8u;
		if (H.Version >= 8 && H.SplatRecordBytes != Out.RecordBytes) { OutError = "The declared record size does not match the surfel format."; return false; }
		if (H.PayloadCodec > 1) { OutError = Format("Unknown payload codec %llu; the package was written by a newer SplatLab.", H.PayloadCodec); return false; }
		if (Out.Format == ERecordFormat::Splat20 && !(H.SplatScaleLog2Max > H.SplatScaleLog2Min)) { OutError = "The splat scale range is empty or inverted; the package is corrupt."; return false; }

		uint64_t Cursor = H.ManifestOffset;
		Out.Chunks.resize(H.NumChunks);
		for (uint32_t c = 0; c < H.NumChunks; c++)
		{
			FChunk& Chunk = Out.Chunks[c];
			if (Cursor + sizeof(FChunkManifestRecord) > Size) { OutError = "The chunk manifest is truncated."; return false; }
			memcpy(&Chunk.Record, Data + Cursor, sizeof(FChunkManifestRecord));
			Cursor += sizeof(FChunkManifestRecord);
			if (Chunk.Record.NumLODs == 0 || Chunk.Record.NumLODs > 32) { OutError = Format("Chunk %llu lists %llu LOD levels; the manifest is corrupt.", c, Chunk.Record.NumLODs); return false; }
			if (Cursor + sizeof(FChunkLODHeader) * Chunk.Record.NumLODs > Size) { OutError = "The chunk manifest is truncated."; return false; }
			Chunk.Lods.resize(Chunk.Record.NumLODs);
			memcpy(Chunk.Lods.data(), Data + Cursor, sizeof(FChunkLODHeader) * Chunk.Record.NumLODs);
			Cursor += sizeof(FChunkLODHeader) * Chunk.Record.NumLODs;

			for (size_t l = 0; l < Chunk.Lods.size(); l++)
			{
				const FChunkLODHeader& Lod = Chunk.Lods[l];
				if (Lod.FileOffset + (uint64_t)Lod.CompressedByteSize > Size) { OutError = Format("Chunk %llu LOD %llu extends past the end of the file; the package is truncated.", c, l); return false; }
				if ((uint64_t)Lod.UncompressedByteSize != (uint64_t)Lod.SurfelCount * Out.RecordBytes) { OutError = Format("Chunk %llu LOD %llu has a payload size that does not match its surfel count.", c, l); return false; }
				Out.MaxLevel = std::max(Out.MaxLevel, Lod.LodLevel);
			}
		}
		return true;
	}

	bool DecodeLevel(const uint8_t* Data, uint64_t Size, const FPackageInfo& Info, uint32_t RequestedLevel,
		FDecodedLevel& Out, std::string& OutError)
	{
		Out = FDecodedLevel();
		const FFileHeader& H = Info.Header;
		Out.Format = Info.Format;
		Out.RecordBytes = Info.RecordBytes;
		Out.ScaleLog2Min = H.SplatScaleLog2Min;
		Out.ScaleLog2Max = H.SplatScaleLog2Max;
		Out.QuantMin = H.BoundsMin;
		const FVec3 E = Sub(H.BoundsMax, H.BoundsMin);
		Out.QuantExtents = (Info.Format == ERecordFormat::Surfel8)
			? FVec3{ E.X + 1e-5f, E.Y + 1e-5f, E.Z + 1e-5f }                                  // Quantizer::QuantizeSurfels
			: FVec3{ std::max(1e-6f, E.X), std::max(1e-6f, E.Y), std::max(1e-6f, E.Z) };      // SplatCodec

		// Pick each chunk's LOD: the requested level, or its coarsest when it stops short of it.
		std::vector<const FChunkLODHeader*> Picked(Info.Chunks.size(), nullptr);
		uint64_t TotalSurfels = 0;
		for (size_t c = 0; c < Info.Chunks.size(); c++)
		{
			const FChunkLODHeader* Best = nullptr;
			for (const FChunkLODHeader& Lod : Info.Chunks[c].Lods)
			{
				if (Lod.LodLevel <= RequestedLevel && (!Best || Lod.LodLevel > Best->LodLevel)) Best = &Lod;
			}
			Picked[c] = Best;
			if (Best) TotalSurfels += Best->SurfelCount;
		}
		if (TotalSurfels == 0) { OutError = "The requested level holds no surfels."; return false; }
		if (TotalSurfels > 0xFFFFFFFFull / Info.RecordBytes) { OutError = "The requested level is too large to upload as one buffer."; return false; }

		Out.Records.resize((size_t)TotalSurfels * Info.RecordBytes);
		Out.SurfelCount = (uint32_t)TotalSurfels;
		std::vector<uint8_t> Shuffled;
		uint32_t Offset = 0;
		for (size_t c = 0; c < Info.Chunks.size(); c++)
		{
			const FChunkLODHeader* Lod = Picked[c];
			if (!Lod || Lod->SurfelCount == 0) continue;
			if (Lod->FileOffset + (uint64_t)Lod->CompressedByteSize > Size) { OutError = "A chunk payload extends past the end of the file."; return false; }
			Shuffled.resize(Lod->UncompressedByteSize);
			if (!Surfels::PayloadDecode::Decompress(Data + Lod->FileOffset, Lod->CompressedByteSize, Shuffled.data(), Lod->UncompressedByteSize, H.PayloadCodec))
			{
				OutError = Format("Chunk %llu holds data this codec build cannot decode; the package was baked with a different codec build or is corrupt.", c);
				return false;
			}
			uint8_t* Dst = Out.Records.data() + (size_t)Offset * Info.RecordBytes;
			Surfels::ByteShuffle::Unshuffle(Shuffled.data(), Dst, Lod->SurfelCount, Info.RecordBytes);

			// 64 consecutive surfels per meshlet, as SurfelsApp does (chunk payloads are spatially ordered).
			for (uint32_t Start = 0; Start < Lod->SurfelCount; Start += kSurfelsPerMeshlet)
			{
				const uint32_t Count = std::min(kSurfelsPerMeshlet, Lod->SurfelCount - Start);
				FVec3 Lo = { FLT_MAX, FLT_MAX, FLT_MAX }, Hi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				for (uint32_t i = 0; i < Count; i++)
				{
					const FVec3 P = DecodePosition(Out, Offset + Start + i);
					Lo = { std::min(Lo.X, P.X), std::min(Lo.Y, P.Y), std::min(Lo.Z, P.Z) };
					Hi = { std::max(Hi.X, P.X), std::max(Hi.Y, P.Y), std::max(Hi.Z, P.Z) };
				}
				FMeshlet M = {};
				M.AabbMin = Lo;
				M.AabbExtents = Sub(Hi, Lo);
				M.Center = { (Lo.X + Hi.X) * 0.5f, (Lo.Y + Hi.Y) * 0.5f, (Lo.Z + Hi.Z) * 0.5f };
				M.BoundingRadius = 0.5f * std::sqrt(M.AabbExtents.X * M.AabbExtents.X + M.AabbExtents.Y * M.AabbExtents.Y + M.AabbExtents.Z * M.AabbExtents.Z);
				M.SurfelOffset = Offset + Start;
				M.SurfelCount = Count;
				M.BlendWeight = 1.0f;
				M.LodLevel = Lod->LodLevel;
				M.ConeAxis = { 0.0f, 1.0f, 0.0f };
				M.ConeCutoff = -1.0f; // Normal-cone culling arrives with step 2
				Out.Meshlets.push_back(M);
				Out.BoundsMin = (Out.Meshlets.size() == 1) ? Lo : FVec3{ std::min(Out.BoundsMin.X, Lo.X), std::min(Out.BoundsMin.Y, Lo.Y), std::min(Out.BoundsMin.Z, Lo.Z) };
				Out.BoundsMax = (Out.Meshlets.size() == 1) ? Hi : FVec3{ std::max(Out.BoundsMax.X, Hi.X), std::max(Out.BoundsMax.Y, Hi.Y), std::max(Out.BoundsMax.Z, Hi.Z) };
			}
			Offset += Lod->SurfelCount;
		}

		Out.PointSpacing = ComputePointSpacing(Out);
		return true;
	}

	FVec3 DecodePosition(const FDecodedLevel& Level, uint32_t Index)
	{
		const uint8_t* R = Level.Records.data() + (size_t)Index * Level.RecordBytes;
		uint32_t W0, W1;
		memcpy(&W0, R, 4);
		float Tx, Ty, Tz;
		if (Level.Format == ERecordFormat::Surfel8)
		{
			Tx = (W0 & 0x3FF) / 1023.0f;
			Ty = ((W0 >> 10) & 0x3FF) / 1023.0f;
			Tz = ((W0 >> 20) & 0x3FF) / 1023.0f;
		}
		else
		{
			memcpy(&W1, R + 4, 4);
			Tx = (W0 & 0xFFFF) / 65535.0f;
			Ty = ((W0 >> 16) & 0xFFFF) / 65535.0f;
			Tz = (W1 & 0xFFFF) / 65535.0f;
		}
		return { Level.QuantMin.X + Tx * Level.QuantExtents.X, Level.QuantMin.Y + Ty * Level.QuantExtents.Y, Level.QuantMin.Z + Tz * Level.QuantExtents.Z };
	}

	FVec3 DecodeOct16(uint16_t Packed)
	{
		const float Ox = (int8_t)(Packed & 0xFF) / 127.0f;
		const float Oy = (int8_t)(Packed >> 8) / 127.0f;
		FVec3 V = { Ox, Oy, 1.0f - std::fabs(Ox) - std::fabs(Oy) };
		if (V.Z < 0.0f)
		{
			V.X = (1.0f - std::fabs(Oy)) * (Ox >= 0.0f ? 1.0f : -1.0f);
			V.Y = (1.0f - std::fabs(Ox)) * (Oy >= 0.0f ? 1.0f : -1.0f);
		}
		const float Len = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
		return (Len > 1e-6f) ? FVec3{ V.X / Len, V.Y / Len, V.Z / Len } : FVec3{ 0.0f, 0.0f, 1.0f };
	}
}

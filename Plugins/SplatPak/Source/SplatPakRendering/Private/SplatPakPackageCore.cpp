// SplatPakPackageCore.cpp
// See SplatPakPackageCore.h. Decoding mirrors StreamPackager::LoadPackage in the 3DGSStream repo: each chunk
// LOD payload goes through the codec library's PayloadDecode (run-length or Brotli, per the header), then is
// unshuffled back to interleaved records.

#include "SplatPakPackageCore.h"

#include "ByteShuffle.h"
#include "PayloadDecode.h"
#include "ColourTransform.h"
#include "ShadowMeshCodec.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace SplatCore
{
	namespace
	{
		std::string Format(const char* Fmt, unsigned long long A = 0, unsigned long long B = 0, unsigned long long C = 0)
		{
			char Buf[256];
			snprintf(Buf, sizeof(Buf), Fmt, A, B, C);
			return Buf;
		}

		FVec3 Sub(FVec3 A, FVec3 B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }

		// Median nearest-neighbour distance over a sample of the level (ComputeLodSpacing in SplatPak).
		float ComputePointSpacing(const FDecodedLevel& Level)
		{
			const uint32_t Total = Level.SplatCount;
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

		if (H.Magic != kMagic) { OutError = Format("Not a .gstx package: signature 0x%08llX, expected 0x%08llX (GSTX).", H.Magic, kMagic); return false; }
		if (H.Version != kFormatVersion)
		{
			OutError = H.Version > kFormatVersion
				? Format("Package format version %llu is newer than this plugin reads (%llu).", H.Version, kFormatVersion)
				: Format("Package format version %llu is no longer read (this plugin reads %llu). Bake it again in SplatPak.", H.Version, kFormatVersion);
			return false;
		}
		if (HeaderBytes < sizeof(FFileHeader)) { OutError = "The file is truncated inside its header."; return false; }

		if (H.NumChunks == 0 || H.NumChunks > 16u * 1024u * 1024u) { OutError = Format("Implausible chunk count %llu; the header is corrupt.", H.NumChunks); return false; }
		if (H.ManifestOffset == 0 || H.ManifestOffset >= Size) { OutError = "The chunk manifest offset lies outside the file; the package is corrupt or truncated."; return false; }

		Out.Format = (ERecordFormat)H.RecordFormat;
		if (Out.Format != ERecordFormat::Splat20) { OutError = Format("Record format %llu is not read; this plugin reads 3D Gaussian packages (format 1).", H.RecordFormat); return false; }
		Out.RecordBytes = 20u;
		if (H.SplatRecordBytes != Out.RecordBytes) { OutError = "The declared record size does not match the splat format."; return false; }
		if (H.PayloadCodec > 1) { OutError = Format("Unknown payload codec %llu; the package was written by a newer SplatPak.", H.PayloadCodec); return false; }
		if (H.ShadowMeshOffset != 0 && (H.ShadowMeshOffset < sizeof(FFileHeader) || H.ShadowMeshBytes == 0 || H.ShadowMeshOffset + H.ShadowMeshBytes > Size))
		{
			OutError = "The shadow mesh section lies outside the file; the package is truncated or corrupt.";
			return false;
		}
		// The occluder mesh: DecodeLevel reads it for occlusion culling
		if (H.OccluderMeshOffset != 0 && (H.OccluderMeshOffset < sizeof(FFileHeader) || H.OccluderMeshBytes == 0 || H.OccluderMeshOffset + H.OccluderMeshBytes > Size))
		{
			OutError = "The occluder mesh section lies outside the file; the package is truncated or corrupt.";
			return false;
		}
		if (H.PayloadTransforms & ~(uint32_t)Splat::PAYLOAD_TRANSFORMS_KNOWN) { OutError = Format("Unknown payload transforms 0x%llX; the package was written by a newer SplatPak.", H.PayloadTransforms); return false; }
		if (!(H.SplatScaleLog2Max > H.SplatScaleLog2Min)) { OutError = "The splat scale range is empty or inverted; the package is corrupt."; return false; }

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
				if ((uint64_t)Lod.UncompressedByteSize != (uint64_t)Lod.SplatCount * Out.RecordBytes) { OutError = Format("Chunk %llu LOD %llu has a payload size that does not match its splat count.", c, l); return false; }
				Out.MaxLevel = std::max(Out.MaxLevel, Lod.LodLevel);
			}
		}
		return true;
	}

	bool DecodeLevel(const uint8_t* Data, uint64_t Size, const FPackageInfo& Info, uint32_t RequestedLevel,
		FDecodedLevel& Out, std::string& OutError, bool bExactLevel)
	{
		Out = FDecodedLevel();
		const FFileHeader& H = Info.Header;
		Out.Format = Info.Format;
		Out.RecordBytes = Info.RecordBytes;
		Out.ScaleLog2Min = H.SplatScaleLog2Min;
		Out.ScaleLog2Max = H.SplatScaleLog2Max;
		Out.QuantMin = H.BoundsMin;
		const FVec3 E = Sub(H.BoundsMax, H.BoundsMin);
		Out.QuantExtents = (Info.Format == ERecordFormat::Point8)
			? FVec3{ E.X + 1e-5f, E.Y + 1e-5f, E.Z + 1e-5f }                                  // Quantizer::QuantizePoints
			: FVec3{ std::max(1e-6f, E.X), std::max(1e-6f, E.Y), std::max(1e-6f, E.Z) };      // SplatCodec

		// Pick each chunk's LOD: the requested level, or its coarsest when it stops short of it.
		std::vector<const FChunkLODHeader*> Picked(Info.Chunks.size(), nullptr);
		uint64_t TotalSplats = 0;
		for (size_t c = 0; c < Info.Chunks.size(); c++)
		{
			const FChunkLODHeader* Best = nullptr;
			for (const FChunkLODHeader& Lod : Info.Chunks[c].Lods)
			{
				if (bExactLevel ? Lod.LodLevel == RequestedLevel : (Lod.LodLevel <= RequestedLevel && (!Best || Lod.LodLevel > Best->LodLevel))) Best = &Lod;
			}
			Picked[c] = Best;
			if (Best) TotalSplats += Best->SplatCount;
		}
		if (TotalSplats == 0) { OutError = "The requested level holds no splats."; return false; }
		if (TotalSplats > 0xFFFFFFFFull / Info.RecordBytes) { OutError = "The requested level is too large to upload as one buffer."; return false; }

		Out.Records.resize((size_t)TotalSplats * Info.RecordBytes);
		Out.SplatCount = (uint32_t)TotalSplats;
		std::vector<uint8_t> Shuffled;
		uint32_t Offset = 0;
		for (size_t c = 0; c < Info.Chunks.size(); c++)
		{
			const FChunkLODHeader* Lod = Picked[c];
			if (!Lod || Lod->SplatCount == 0) continue;
			if (Lod->FileOffset + (uint64_t)Lod->CompressedByteSize > Size) { OutError = "A chunk payload extends past the end of the file."; return false; }
			Shuffled.resize(Lod->UncompressedByteSize);
			if (!Splat::PayloadDecode::Decompress(Data + Lod->FileOffset, Lod->CompressedByteSize, Shuffled.data(), Lod->UncompressedByteSize, H.PayloadCodec))
			{
				OutError = Format("Chunk %llu holds data this codec build cannot decode; the package was baked with a different codec build or is corrupt.", c);
				return false;
			}
			uint8_t* Dst = Out.Records.data() + (size_t)Offset * Info.RecordBytes;
			Splat::ByteShuffle::Unshuffle(Shuffled.data(), Dst, Lod->SplatCount, Info.RecordBytes);
			if (Info.Format == ERecordFormat::Splat20 && (Info.Header.PayloadTransforms & Splat::PAYLOAD_TRANSFORM_RECORD_YCOCG))
			{
				Splat::ColourTransform::UndoRecords(Dst, Lod->SplatCount, Info.RecordBytes); // Colour back to RGB (v11)
			}

			// 64 consecutive splats per meshlet, as SplatPak does (chunk payloads are spatially ordered).
			for (uint32_t Start = 0; Start < Lod->SplatCount; Start += kSplatsPerMeshlet)
			{
				const uint32_t Count = std::min(kSplatsPerMeshlet, Lod->SplatCount - Start);
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
				M.SplatOffset = Offset + Start;
				M.SplatCount = Count;
				M.BlendWeight = 1.0f;
				M.LodLevel = Lod->LodLevel;
				M.ConeAxis = { 0.0f, 1.0f, 0.0f };
				M.ConeCutoff = -1.0f; // Normal-cone culling arrives with step 2
				Out.Meshlets.push_back(M);
				Out.BoundsMin = (Out.Meshlets.size() == 1) ? Lo : FVec3{ std::min(Out.BoundsMin.X, Lo.X), std::min(Out.BoundsMin.Y, Lo.Y), std::min(Out.BoundsMin.Z, Lo.Z) };
				Out.BoundsMax = (Out.Meshlets.size() == 1) ? Hi : FVec3{ std::max(Out.BoundsMax.X, Hi.X), std::max(Out.BoundsMax.Y, Hi.Y), std::max(Out.BoundsMax.Z, Hi.Z) };
			}
			Offset += Lod->SplatCount;
		}

		// The occluder mesh, finest level (same encoding as the shadow mesh). A section that does not decode only
		// costs the occlusion culling, so it does not fail the level.
		if (HasOccluderMesh(Info) && !bExactLevel)
		{
			Splat::ShadowMesh Occluder;
			std::string OccluderError;
			if (Splat::ShadowMeshCodec::Decode(Data + H.OccluderMeshOffset, (size_t)H.OccluderMeshBytes, Occluder, OccluderError) && !Occluder.levels.empty())
			{
				const Splat::ShadowMeshLevel& L = Occluder.levels[0];
				Out.OccluderPositions.reserve(L.positions.size());
				for (const Splat::ShadowMeshVec3& P : L.positions) Out.OccluderPositions.push_back({ P.x, P.y, P.z });
				Out.OccluderColours = L.colours;
				Out.OccluderIndices = L.indices;
			}
		}
		// The shadow mesh, finest level, for the gap fill. Only costs the fill when it does not decode.
		if (HasShadowMesh(Info) && !bExactLevel && H.ShadowMeshOffset + H.ShadowMeshBytes <= Size)
		{
			Splat::ShadowMesh Shadow;
			std::string ShadowError;
			if (Splat::ShadowMeshCodec::Decode(Data + H.ShadowMeshOffset, (size_t)H.ShadowMeshBytes, Shadow, ShadowError) && !Shadow.levels.empty())
			{
				const Splat::ShadowMeshLevel& L = Shadow.levels[0];
				Out.ShadowPositions.reserve(L.positions.size());
				for (const Splat::ShadowMeshVec3& P : L.positions) Out.ShadowPositions.push_back({ P.x, P.y, P.z });
				Out.ShadowColours = L.colours;
				Out.ShadowIndices = L.indices;
			}
		}

		Out.PointSpacing = bExactLevel ? 0.0f : ComputePointSpacing(Out);
		return true;
	}

	FVec3 DecodePosition(const FDecodedLevel& Level, uint32_t Index)
	{
		const uint8_t* R = Level.Records.data() + (size_t)Index * Level.RecordBytes;
		uint32_t W0, W1;
		memcpy(&W0, R, 4);
		float Tx, Ty, Tz;
		if (Level.Format == ERecordFormat::Point8)
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

	bool ReadShadowMesh(const uint8_t* Data, uint64_t Size, const FPackageInfo& Info, std::vector<FShadowMeshLevel>& OutLevels, std::string& OutError)
	{
		OutLevels.clear();
		if (!HasShadowMesh(Info)) return false;
		const FFileHeader& H = Info.Header;
		if (H.ShadowMeshOffset + H.ShadowMeshBytes > Size) { OutError = "The shadow mesh section lies outside the file."; return false; }
		Splat::ShadowMesh Mesh;
		if (!Splat::ShadowMeshCodec::Decode(Data + H.ShadowMeshOffset, (size_t)H.ShadowMeshBytes, Mesh, OutError)) return false;
		for (const Splat::ShadowMeshLevel& L : Mesh.levels)
		{
			FShadowMeshLevel Level;
			Level.Positions.reserve(L.positions.size());
			for (const Splat::ShadowMeshVec3& P : L.positions) Level.Positions.push_back({ P.x, P.y, P.z });
			Level.Colours = L.colours;
			Level.Indices = L.indices;
			OutLevels.push_back(std::move(Level));
		}
		return !OutLevels.empty();
	}
}

// SplatPackageCore.h
// Engine-independent .gstx (and older .sflw) reading for the SplatPak plugin: the on-disk layout (mirrors
// WaveletTypes.h in the 3DGSStream repo), header/manifest validation, decoding one level into
// GPU-ready records, and splitting it into the 64-splat meshlets the draw shaders consume.
// Plain C++17 so the same code builds inside the UE module and in a native test harness.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Exported from the SplatPakRendering module; empty when built outside Unreal (native tests).
#ifndef SPLATPAKRENDERING_API
#define SPLATPAKRENDERING_API
#endif

namespace SplatCore
{
	static constexpr uint32_t kMagic = 0x58545347;     // "GSTX": format version 10 and later (.gstx)
	static constexpr uint32_t kMagicSFLW = 0x574C4653; // "SFLW": versions 1 to 9 (.sflw)
	static constexpr uint32_t kMaxSupportedVersion = 12;
	static constexpr uint32_t kMaxOcclusionMips = 4;
	static constexpr uint32_t kSplatsPerMeshlet = 64; // SPLATS_PER_GROUP in the shaders

	enum class ERecordFormat : uint32_t
	{
		Point8 = 0, // PackedPointGPU (legacy 8-byte record): 10:10:10:2 position, oct16 normal, RGB565
		Splat20 = 1, // PackedSplatGPU: 16-bit position, log2 scales, opacity, DC colour, rotation
	};

	struct FVec3 { float X, Y, Z; };

	// Byte-for-byte SFLWFileHeader (MSVC natural alignment). Fields past a file's version are zeroed on read.
	struct FFileHeader
	{
		uint32_t Magic;
		uint32_t Version;
		uint32_t NumChunks;
		uint32_t MaxLOD;
		uint64_t TotalSplatsLOD0;
		double   GlobalOrigin[3];
		FVec3    BoundsMin;
		FVec3    BoundsMax;
		float    SplatRadius;
		uint32_t OcclusionVoxelCount;
		uint64_t OcclusionVoxelOffset;
		uint64_t ManifestOffset;
		uint64_t SourceFileBytes;
		uint32_t OcclusionMipCount;
		uint32_t OcclusionMipBlockCount[kMaxOcclusionMips];
		float    OcclusionMipCellSize[kMaxOcclusionMips];
		uint32_t DetailGridDims[3];
		float    DetailGridCellSize;
		uint64_t DetailGridOffset;
		uint32_t RecordFormat;
		uint32_t SHDegree;
		uint32_t SplatRecordBytes;
		uint32_t SHRecordBytes;
		float    SplatScaleLog2Min;
		float    SplatScaleLog2Max;
		uint64_t SHManifestOffset;
		uint32_t PayloadCodec; // v9+: the entropy stage of every payload (0 = run-length, 1 = Brotli); older files are 0
		uint32_t PayloadTransforms; // v11+, in the former padding: bit 0 = record colour as YCoCg-R (ColourTransform.h); older files are 0
		uint64_t ShadowMeshOffset;  // v12+: absolute offset of the shadow mesh section (ShadowMeshCodec.h); 0 = none
		uint64_t ShadowMeshBytes;   // v12+: its size
	};
	static_assert(sizeof(FFileHeader) == 224, "FFileHeader must match SFLWFileHeader");
	static_assert(offsetof(FFileHeader, ShadowMeshOffset) == 208, "FFileHeader must match SFLWFileHeader");
	static_assert(offsetof(FFileHeader, PayloadCodec) == 200, "FFileHeader must match SFLWFileHeader");
	static_assert(offsetof(FFileHeader, PayloadTransforms) == 204, "FFileHeader must match SFLWFileHeader");
	static_assert(offsetof(FFileHeader, ManifestOffset) == 88, "FFileHeader must match SFLWFileHeader");
	static_assert(offsetof(FFileHeader, DetailGridOffset) == 160, "FFileHeader must match SFLWFileHeader");
	static_assert(offsetof(FFileHeader, RecordFormat) == 168, "FFileHeader must match SFLWFileHeader");

	struct FChunkLODHeader
	{
		uint32_t LodLevel;
		uint32_t SplatCount;
		uint32_t UncompressedByteSize;
		uint32_t CompressedByteSize;
		uint64_t FileOffset;
		float    GeometricError;
	};
	static_assert(sizeof(FChunkLODHeader) == 32, "FChunkLODHeader must match ChunkLODHeader");

	struct FChunkManifestRecord
	{
		uint32_t ChunkId;
		FVec3    AabbMin;
		FVec3    AabbMax;
		FVec3    Center;
		float    BoundingRadius;
		uint32_t NumLODs;
	};
	static_assert(sizeof(FChunkManifestRecord) == 48, "FChunkManifestRecord must match ChunkManifestRecord");

	struct FChunk
	{
		FChunkManifestRecord Record;
		std::vector<FChunkLODHeader> Lods;
	};

	// One draw meshlet, laid out as MeshletChunkGPU / FSplatMeshlet in the shaders (80 bytes).
	struct FMeshlet
	{
		FVec3    Center;
		float    BoundingRadius;
		FVec3    AabbMin;
		uint32_t SplatOffset;
		FVec3    AabbExtents;
		uint32_t SplatCount;
		float    BlendWeight;
		uint32_t LodLevel;
		float    DilationMorph;
		float    IsSilhouette;
		FVec3    ConeAxis;
		float    ConeCutoff;
	};
	static_assert(sizeof(FMeshlet) == 80, "FMeshlet must match MeshletChunkGPU");

	struct FPackageInfo
	{
		FFileHeader Header = {};
		std::vector<FChunk> Chunks;
		ERecordFormat Format = ERecordFormat::Point8;
		uint32_t RecordBytes = 8;
		uint32_t MaxLevel = 0; // Highest LOD level any chunk carries (the coarsest)
	};

	// One level of a package, decoded and ready to upload.
	struct FDecodedLevel
	{
		ERecordFormat Format = ERecordFormat::Point8;
		uint32_t RecordBytes = 8;
		std::vector<uint8_t> Records;   // RecordBytes per splat, meshlet order
		std::vector<FMeshlet> Meshlets; // Model-space bounds, kSplatsPerMeshlet splats at most
		uint32_t SplatCount = 0;
		FVec3 BoundsMin = {}, BoundsMax = {};
		FVec3 QuantMin = {}, QuantExtents = {}; // Frame the record positions are quantized in
		float ScaleLog2Min = 0.0f, ScaleLog2Max = 0.0f;
		float PointSpacing = 0.0f; // Median nearest-neighbour distance (model units); 0 = unknown
	};

	// Reads and validates the header and chunk manifest. Data is the whole package file.
	SPLATPAKRENDERING_API bool ReadPackageInfo(const uint8_t* Data, uint64_t Size, FPackageInfo& Out, std::string& OutError);

	// Decodes one level. Chunks that stop short of RequestedLevel contribute their coarsest level, so
	// RequestedLevel >= MaxLevel loads the coarsest representation of the whole model.
	SPLATPAKRENDERING_API bool DecodeLevel(const uint8_t* Data, uint64_t Size, const FPackageInfo& Info, uint32_t RequestedLevel,
		FDecodedLevel& Out, std::string& OutError);

	// Model-space position of record Index (same maths as the shaders).
	SPLATPAKRENDERING_API FVec3 DecodePosition(const FDecodedLevel& Level, uint32_t Index);

	// Octahedral normal as written by PackNormalOct16 (two signed bytes).
	SPLATPAKRENDERING_API FVec3 DecodeOct16(uint16_t Packed);

	// The package's shadow mesh (format v12, ShadowMeshCodec.h): levels finest first, each positions (model space),
	// per-vertex colours (0xAABBGGRR) and a counter-clockwise triangle list. False with no error for a package without
	// one (HasShadowMesh), false with an error for a section that does not decode.
	struct FShadowMeshLevel
	{
		std::vector<FVec3> Positions;
		std::vector<uint32_t> Colours;
		std::vector<uint32_t> Indices;
	};
	inline bool HasShadowMesh(const FPackageInfo& Info) { return Info.Header.Version >= 12 && Info.Header.ShadowMeshOffset != 0; }
	SPLATPAKRENDERING_API bool ReadShadowMesh(const uint8_t* Data, uint64_t Size, const FPackageInfo& Info,
		std::vector<FShadowMeshLevel>& OutLevels, std::string& OutError);
}

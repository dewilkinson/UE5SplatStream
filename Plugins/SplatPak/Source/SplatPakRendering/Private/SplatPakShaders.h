// SplatPakShaders.h
// Global shaders for the opaque disc pass (Shaders/Private/SplatPakDisc.usf). The amplification +
// mesh shader pair is the default path wherever the platform supports Mesh Shader Tier 1; the
// instanced vertex shader is the fallback. Both feed the same pixel shader.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"

// Shared by every stage. Field order keeps each FVector3f on a 16-byte boundary.
BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakDiscParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Records)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSplatPakMeshlet>, Meshlets)
	SHADER_PARAMETER(FMatrix44f, ModelToClip)
	SHADER_PARAMETER(FVector3f, CamRightModel)
	SHADER_PARAMETER(float, SplatScale)
	SHADER_PARAMETER(FVector3f, CamUpModel)
	SHADER_PARAMETER(float, GlowIntensity)
	SHADER_PARAMETER(FVector3f, EyeModel)
	SHADER_PARAMETER(float, ClipWToModel)
	SHADER_PARAMETER(FVector3f, QuantMin)
	SHADER_PARAMETER(uint32, RecordFormat)
	SHADER_PARAMETER(FVector3f, QuantExtents)
	SHADER_PARAMETER(uint32, MeshletCount)
	SHADER_PARAMETER(FVector3f, LightDirModel)
	SHADER_PARAMETER(uint32, OrientMode)
	SHADER_PARAMETER(float, ScaleLog2Min)
	SHADER_PARAMETER(float, ScaleLog2Max)
	SHADER_PARAMETER(float, PreExposure)
	SHADER_PARAMETER(uint32, WordsPerRecord)
	SHADER_PARAMETER(float, EmissiveIntensity)
	SHADER_PARAMETER(FVector3f, ColourGain)   // The colour grade (FSplatPakDrawSettings), linear
	SHADER_PARAMETER(FVector3f, ColourOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OccDepth) // Occlusion grid (SplatPakOcclusion.ush); one cleared element when culling is off
	SHADER_PARAMETER(uint32, OcclusionCull)
	SHADER_PARAMETER(uint32, OccWidth)
	SHADER_PARAMETER(uint32, OccHeight)
	SHADER_PARAMETER(float, OccNearW)
	SHADER_PARAMETER(float, OccMeshletPad)
	SHADER_PARAMETER(uint32, StreamFlags) // Bit 0: dithered cross-fades; bit 1: Refinement Visualizer
	SHADER_PARAMETER(float, GlowHue)
	SHADER_PARAMETER_ARRAY(FVector4f, LevelRadii, [2]) // Disc radius per level (8 levels)
	// Lit mode (MainGBufferPS): the discs go into Unreal's GBuffer so its lights, shadows and Lumen shade them
	SHADER_PARAMETER(FMatrix44f, ModelToWorldNormal) // Inverse transpose of model to world: model-space normals to world space
	SHADER_PARAMETER(uint32, LitMode)    // 1 = GBuffer pass: no SplatPak key light on point records
	SHADER_PARAMETER(float, LitBlend)    // 0 = the baked colour as emissive (the unlit look), 1 = the colour as albedo, fully lit
	SHADER_PARAMETER(float, LitRoughness)
	SHADER_PARAMETER(float, LitSpecular)
	SHADER_PARAMETER(float, AlbedoScale) // The baked colour times this is the albedo
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MeshNormalTexture) // The shadow mesh's world normals (xyz) and clip w (w; 0 = none)
	SHADER_PARAMETER(float, NormalSmoothing)     // 0 = the splat's own normal, 1 = the shadow mesh's where the disc lies on it
	SHADER_PARAMETER(float, MeshNormalTolerance) // How far (clip w, world units) a disc may lie from the mesh and still take its normal
	RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs) // Mesh-shader groups written on the GPU (r.SplatPak.IndirectDispatch); null otherwise
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// Sorted Gaussians (SplatPakGaussian.usf). The constants are listed in the order the shader file declares them, in both
// structs, so the compute and the draw stages see the same layout.
#define SPLATPAK_GAUSSIAN_CONSTANTS() \
	SHADER_PARAMETER(FMatrix44f, ModelToClip) \
	SHADER_PARAMETER(FVector3f, EyeModel) \
	SHADER_PARAMETER(uint32, MeshletCount) \
	SHADER_PARAMETER(FVector3f, ForwardModel) \
	SHADER_PARAMETER(uint32, SlotCount) \
	SHADER_PARAMETER(FVector3f, QuantMin) \
	SHADER_PARAMETER(uint32, PaddedSlotCount) \
	SHADER_PARAMETER(FVector3f, QuantExtents) \
	SHADER_PARAMETER(uint32, NumBlocks) \
	SHADER_PARAMETER(uint32, RadixPass) \
	SHADER_PARAMETER(uint32, GroupsX) \
	SHADER_PARAMETER(FMatrix44f, ModelToView) \
	SHADER_PARAMETER(FVector2f, Viewport) \
	SHADER_PARAMETER(float, Proj00) \
	SHADER_PARAMETER(float, ScaleLog2Min) \
	SHADER_PARAMETER(float, ScaleLog2Max) \
	SHADER_PARAMETER(float, PreExposure) \
	SHADER_PARAMETER(float, EmissiveIntensity) \
	SHADER_PARAMETER(FVector3f, ColourGain) \
	SHADER_PARAMETER(FVector3f, ColourOffset) \
	SHADER_PARAMETER(uint32, StreamFlags) \
	SHADER_PARAMETER(float, GlowIntensity) \
	SHADER_PARAMETER(float, GlowHue) \
	SHADER_PARAMETER_ARRAY(FVector4f, LevelRadii, [2])

BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakGaussianSortParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Records)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSplatPakMeshlet>, Meshlets)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MeshletSlotBase)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, SlotInfo)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, PairsIn)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, PairsOut)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Hist)
	SPLATPAK_GAUSSIAN_CONSTANTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakGaussianDrawParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Records)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSplatPakMeshlet>, Meshlets)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, SortedPairs)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, SortedSlotInfo)
	SPLATPAK_GAUSSIAN_CONSTANTS()
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FSplatPakGaussianCSBase : public FGlobalShader
{
public:
	using FParameters = FSplatPakGaussianSortParameters;
	FSplatPakGaussianCSBase() = default;
	FSplatPakGaussianCSBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSplatPakGaussianExpandCS : public FSplatPakGaussianCSBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakGaussianExpandCS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakGaussianExpandCS, FSplatPakGaussianCSBase);
};

class FSplatPakGaussianHistogramCS : public FSplatPakGaussianCSBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakGaussianHistogramCS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakGaussianHistogramCS, FSplatPakGaussianCSBase);
};

class FSplatPakGaussianScanCS : public FSplatPakGaussianCSBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakGaussianScanCS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakGaussianScanCS, FSplatPakGaussianCSBase);
};

class FSplatPakGaussianScatterCS : public FSplatPakGaussianCSBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakGaussianScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakGaussianScatterCS, FSplatPakGaussianCSBase);
};

class FSplatPakGaussianDrawBase : public FGlobalShader
{
public:
	using FParameters = FSplatPakGaussianDrawParameters;
	FSplatPakGaussianDrawBase() = default;
	FSplatPakGaussianDrawBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSplatPakGaussianVS : public FSplatPakGaussianDrawBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakGaussianVS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakGaussianVS, FSplatPakGaussianDrawBase);
};

class FSplatPakGaussianPS : public FSplatPakGaussianDrawBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakGaussianPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakGaussianPS, FSplatPakGaussianDrawBase);
};

// The occluder and shadow meshes behind the splats (SplatPakMesh.usf)
BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakMeshParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MeshPositions)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MeshColours)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MeshIndices)
	SHADER_PARAMETER(FMatrix44f, ModelToClip)
	SHADER_PARAMETER(FVector3f, LightDirModel)
	SHADER_PARAMETER(float, PreExposure)
	SHADER_PARAMETER(float, EmissiveIntensity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FSplatPakMeshShaderBase : public FGlobalShader
{
public:
	using FParameters = FSplatPakMeshParameters;
	FSplatPakMeshShaderBase() = default;
	FSplatPakMeshShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSplatPakMeshVS : public FSplatPakMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakMeshVS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakMeshVS, FSplatPakMeshShaderBase);
};

class FSplatPakMeshPS : public FSplatPakMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakMeshPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakMeshPS, FSplatPakMeshShaderBase);
};

// Lit mode's normal smoothing (SplatPakMesh.usf MainNormalVS/PS): the shadow mesh's smooth normals and view depth
BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakMeshNormalParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MeshPositions)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MeshNormals)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MeshIndices)
	SHADER_PARAMETER(FMatrix44f, ModelToClip)
	SHADER_PARAMETER(FMatrix44f, ModelToWorldNormal)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FSplatPakMeshNormalShaderBase : public FGlobalShader
{
public:
	using FParameters = FSplatPakMeshNormalParameters;
	FSplatPakMeshNormalShaderBase() = default;
	FSplatPakMeshNormalShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSplatPakMeshNormalVS : public FSplatPakMeshNormalShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakMeshNormalVS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakMeshNormalVS, FSplatPakMeshNormalShaderBase);
};

class FSplatPakMeshNormalPS : public FSplatPakMeshNormalShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakMeshNormalPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakMeshNormalPS, FSplatPakMeshNormalShaderBase);
};

// The occluder raster (SplatPakOcclusion.usf)
BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakOccRasterParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, OccluderPositions)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OccluderIndices)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWOccDepth)
	SHADER_PARAMETER(FMatrix44f, ModelToClip)
	SHADER_PARAMETER(uint32, OccWidth)
	SHADER_PARAMETER(uint32, OccHeight)
	SHADER_PARAMETER(uint32, OccTriCount)
	SHADER_PARAMETER(float, OccNearW)
END_SHADER_PARAMETER_STRUCT()

class FSplatPakOccRasterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSplatPakOccRasterCS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakOccRasterCS, FGlobalShader);
	using FParameters = FSplatPakOccRasterParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// The disc pass's indirect arguments (SplatPakIndirect.usf): the mesh-shader group counts, written on the GPU
BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakDiscArgsParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWArgs)
	SHADER_PARAMETER(uint32, MeshletCount)
	SHADER_PARAMETER(uint32, MeshletsPerGroup)
END_SHADER_PARAMETER_STRUCT()

class FSplatPakDiscArgsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSplatPakDiscArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDiscArgsCS, FGlobalShader);
	using FParameters = FSplatPakDiscArgsParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsMeshShadersTier1(Parameters.Platform);
	}
};

class FSplatPakDiscShaderBase : public FGlobalShader
{
public:
	using FParameters = FSplatPakDiscParameters;
	FSplatPakDiscShaderBase() = default;
	FSplatPakDiscShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSplatPakDiscMeshShaderBase : public FSplatPakDiscShaderBase
{
public:
	FSplatPakDiscMeshShaderBase() = default;
	FSplatPakDiscMeshShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSplatPakDiscShaderBase(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsMeshShadersTier1(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SPLATS_MESH_SHADER"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}
};

class FSplatPakDiscAS : public FSplatPakDiscMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakDiscAS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDiscAS, FSplatPakDiscMeshShaderBase);
};

class FSplatPakDiscMS : public FSplatPakDiscMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakDiscMS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDiscMS, FSplatPakDiscMeshShaderBase);
};

class FSplatPakDiscVS : public FSplatPakDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakDiscVS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDiscVS, FSplatPakDiscShaderBase);
};

class FSplatPakDiscPS : public FSplatPakDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakDiscPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDiscPS, FSplatPakDiscShaderBase);
};

// Lit mode: the same discs written into Unreal's GBuffer (scene colour + GBufferA-C) right after the base pass
class FSplatPakDiscGBufferPS : public FSplatPakDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatPakDiscGBufferPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDiscGBufferPS, FSplatPakDiscShaderBase);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FSplatPakDiscShaderBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SPLATS_GBUFFER"), 1);
	}
};

// Must match SPLATS_PER_GROUP / AS_GROUP_SIZE in SplatPakDisc.usf.
static constexpr uint32 GSplatsPerMeshlet = 64;
static constexpr uint32 GSplatMeshletsPerASGroup = 32;

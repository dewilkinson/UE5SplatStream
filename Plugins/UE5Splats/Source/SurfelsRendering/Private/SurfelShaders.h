// SurfelShaders.h
// Global shaders for the opaque disc pass (Shaders/Private/SurfelsDisc.usf). The amplification +
// mesh shader pair is the default path wherever the platform supports Mesh Shader Tier 1; the
// instanced vertex shader is the fallback. Both feed the same pixel shader.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"

// Shared by every stage. Field order keeps each FVector3f on a 16-byte boundary.
BEGIN_SHADER_PARAMETER_STRUCT(FSurfelDiscParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Records)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSurfelMeshlet>, Meshlets)
	SHADER_PARAMETER(FMatrix44f, ModelToClip)
	SHADER_PARAMETER(FVector3f, CamRightModel)
	SHADER_PARAMETER(float, SplatScale)
	SHADER_PARAMETER(FVector3f, CamUpModel)
	SHADER_PARAMETER(float, LevelRadius)
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
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FSurfelDiscShaderBase : public FGlobalShader
{
public:
	using FParameters = FSurfelDiscParameters;
	FSurfelDiscShaderBase() = default;
	FSurfelDiscShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSurfelDiscMeshShaderBase : public FSurfelDiscShaderBase
{
public:
	FSurfelDiscMeshShaderBase() = default;
	FSurfelDiscMeshShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSurfelDiscShaderBase(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsMeshShadersTier1(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SURFELS_MESH_SHADER"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}
};

class FSurfelDiscAS : public FSurfelDiscMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSurfelDiscAS);
	SHADER_USE_PARAMETER_STRUCT(FSurfelDiscAS, FSurfelDiscMeshShaderBase);
};

class FSurfelDiscMS : public FSurfelDiscMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSurfelDiscMS);
	SHADER_USE_PARAMETER_STRUCT(FSurfelDiscMS, FSurfelDiscMeshShaderBase);
};

class FSurfelDiscVS : public FSurfelDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSurfelDiscVS);
	SHADER_USE_PARAMETER_STRUCT(FSurfelDiscVS, FSurfelDiscShaderBase);
};

class FSurfelDiscPS : public FSurfelDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSurfelDiscPS);
	SHADER_USE_PARAMETER_STRUCT(FSurfelDiscPS, FSurfelDiscShaderBase);
};

// Must match SURFELS_PER_GROUP / AS_GROUP_SIZE in SurfelsDisc.usf.
static constexpr uint32 GSurfelsPerMeshlet = 64;
static constexpr uint32 GSurfelMeshletsPerASGroup = 32;

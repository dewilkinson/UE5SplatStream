// SplatShaders.h
// Global shaders for the opaque disc pass (Shaders/Private/SplatDisc.usf). The amplification +
// mesh shader pair is the default path wherever the platform supports Mesh Shader Tier 1; the
// instanced vertex shader is the fallback. Both feed the same pixel shader.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"

// Shared by every stage. Field order keeps each FVector3f on a 16-byte boundary.
BEGIN_SHADER_PARAMETER_STRUCT(FSplatDiscParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Records)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSplatMeshlet>, Meshlets)
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
	SHADER_PARAMETER(float, EmissiveIntensity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FSplatDiscShaderBase : public FGlobalShader
{
public:
	using FParameters = FSplatDiscParameters;
	FSplatDiscShaderBase() = default;
	FSplatDiscShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSplatDiscMeshShaderBase : public FSplatDiscShaderBase
{
public:
	FSplatDiscMeshShaderBase() = default;
	FSplatDiscMeshShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSplatDiscShaderBase(Initializer) {}

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

class FSplatDiscAS : public FSplatDiscMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatDiscAS);
	SHADER_USE_PARAMETER_STRUCT(FSplatDiscAS, FSplatDiscMeshShaderBase);
};

class FSplatDiscMS : public FSplatDiscMeshShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatDiscMS);
	SHADER_USE_PARAMETER_STRUCT(FSplatDiscMS, FSplatDiscMeshShaderBase);
};

class FSplatDiscVS : public FSplatDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatDiscVS);
	SHADER_USE_PARAMETER_STRUCT(FSplatDiscVS, FSplatDiscShaderBase);
};

class FSplatDiscPS : public FSplatDiscShaderBase
{
	DECLARE_GLOBAL_SHADER(FSplatDiscPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatDiscPS, FSplatDiscShaderBase);
};

// Must match SPLATS_PER_GROUP / AS_GROUP_SIZE in SplatDisc.usf.
static constexpr uint32 GSplatsPerMeshlet = 64;
static constexpr uint32 GSplatMeshletsPerASGroup = 32;

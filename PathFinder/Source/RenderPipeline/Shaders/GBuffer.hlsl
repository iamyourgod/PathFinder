﻿#ifndef _GBuffer__
#define _GBuffer__

#include "Packing.hlsl"
#include "Camera.hlsl"

static const uint GBufferTypeStandard = 0;
static const uint GBufferTypeEmissive = 1;

struct GBufferTextureIndices
{
    uint AlbedoMetalnessTexIdx;
    uint NormalRoughnessTexIdx;
    uint MotionTexIdx;
    uint TypeAndMaterialTexIdx;
    // 16 byte boundary
    uint ViewDepthTexIdx;
    uint DepthStencilTexIdx;
    uint __Pad0;
    uint __Pad1;
    // 16 byte boundary
};

struct GBufferTexturePack
{
    Texture2D AlbedoMetalness;
    Texture2D NormalRoughness;
    Texture2D<uint4> Motion;
    Texture2D<uint4> TypeAndMaterialIndex;
    Texture2D ViewDepth;
    Texture2D DepthStencil;
};

struct GBufferPixelOut
{
    float4 AlbedoMetalness : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    uint2 Motion : SV_Target2;
    uint TypeAndMaterialIndex : SV_Target3;
    float ViewDepth : SV_Target4;
};

struct GBufferStandard
{
    float3 Albedo;
    float3 Normal;
    float3 Motion;
    float Roughness;
    float Metalness;
    uint MaterialIndex;
};

struct GBufferEmissive
{
    uint LightIndex;
    float3 Motion;
};

GBufferStandard ZeroGBufferStandard()
{
    GBufferStandard gb;
    gb.Albedo = 0;
    gb.Normal = 0;
    gb.Motion = 0;
    gb.Roughness = 0;
    gb.Metalness = 0;
    gb.MaterialIndex = 0;
    return gb;
}

GBufferEmissive ZeroGBufferEmissive()
{
    GBufferEmissive gb;
    gb.LightIndex = 0;
    gb.Motion = 0;
    return gb;
}

GBufferPixelOut GetStandardGBufferPixelOutput(float3 albedo, float metalness, float roughness, float3 normal, float3 motion, uint materialIndex, float viewDepth)
{
    GBufferPixelOut output;
    output.AlbedoMetalness = float4(albedo, metalness);
    output.NormalRoughness = float4(normal * 0.5 + 0.5, roughness);
    output.Motion = uint2(PackSnorm2x16(motion.x, motion.y, 1.0), asuint(motion.z)); // 3D motion vector;
    output.TypeAndMaterialIndex = (GBufferTypeStandard << 4) | (materialIndex & 0x0000000F);
    output.ViewDepth = viewDepth;
    return output;
}

GBufferPixelOut GetEmissiveGBufferPixelOutput(uint lightIndex, float3 motion, float viewDepth)
{
    GBufferPixelOut output;
    output.AlbedoMetalness = 0.0;
    output.NormalRoughness = 0.0;
    output.Motion = uint2(PackSnorm2x16(motion.x, motion.y, 1.0), asuint(motion.z)); // 3D motion vector
    output.TypeAndMaterialIndex = (GBufferTypeEmissive << 4) | (lightIndex & 0x0000000F);
    output.ViewDepth = viewDepth;
    return output;
}

float3 ExpandGBufferNormal(float3 normal)
{
    return normal * 2.0 - 1.0;
}

uint LoadGBufferType(GBufferTexturePack textures, uint2 texelIndex)
{
    uint type = textures.TypeAndMaterialIndex.Load(uint3(texelIndex, 0)).x;
    return type >> 4;
}

float3 LoadGBufferMotion(Texture2D<uint4> motion, uint2 texelIndex)
{
    // 3D motion
    uint2 motionEncoded = motion[texelIndex].rg;
    return float3(UnpackSnorm2x16(motionEncoded.x, 1.0), asfloat(motionEncoded.y));
}

void LoadGBufferNormalAndRoughness(Texture2D normalRoughness, uint2 texelIndex, out float3 normal, out float roughness)
{
    float4 nr = normalRoughness.Load(uint3(texelIndex, 0));
    normal = ExpandGBufferNormal(nr.xyz);
    roughness = nr.w;
}

void LoadStandardGBufferNoMotion(inout GBufferStandard gBuffer, GBufferTexturePack textures, uint2 texelIndex)
{
    uint3 loadIndex = uint3(texelIndex, 0);

    float4 albedoMetalness = textures.AlbedoMetalness.Load(loadIndex).xyzw;
    float4 normalRoughness = textures.NormalRoughness.Load(loadIndex).xyzw;
    uint typeAndMaterialIndex = textures.TypeAndMaterialIndex.Load(loadIndex).x;
    uint materialIndex = typeAndMaterialIndex & 0x0000000F;

    gBuffer.Albedo = albedoMetalness.rgb;
    gBuffer.Metalness = albedoMetalness.a;
    gBuffer.Roughness = normalRoughness.w;
    gBuffer.Normal = ExpandGBufferNormal(normalRoughness.xyz);
    gBuffer.Motion = 0;
    gBuffer.MaterialIndex = materialIndex;
}

void LoadStandardGBuffer(inout GBufferStandard gBuffer, GBufferTexturePack textures, uint2 texelIndex)
{
    LoadStandardGBufferNoMotion(gBuffer, textures, texelIndex);
    gBuffer.Motion = LoadGBufferMotion(textures.Motion, texelIndex);;
}

void LoadEmissiveGBufferNoMotion(inout GBufferEmissive gBuffer, GBufferTexturePack textures, uint2 texelIndex)
{
    uint3 loadIndex = uint3(texelIndex, 0);

    uint typeAndMaterialIndex = textures.TypeAndMaterialIndex.Load(loadIndex).x;
    uint lightIndex = typeAndMaterialIndex & 0x0000000F;

    gBuffer.LightIndex = lightIndex;
    gBuffer.Motion = 0;
}

void LoadEmissiveGBuffer(inout GBufferEmissive gBuffer, GBufferTexturePack textures, uint2 texelIndex)
{
    LoadEmissiveGBufferNoMotion(gBuffer, textures, texelIndex);
    gBuffer.Motion = LoadGBufferMotion(textures.Motion, texelIndex);;
}

#endif
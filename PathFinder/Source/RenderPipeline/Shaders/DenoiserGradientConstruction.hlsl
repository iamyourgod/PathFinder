﻿#ifndef _DenoiserGradientConstruction__
#define _DenoiserGradientConstruction__

struct PassCBData
{
    uint GradientInputsTexIdx;
    uint SamplePositionsTexIdx;
    uint ShadowedShadingTexIdx;
    uint GradientTexIdx;
};

#define PassDataType PassCBData

#include "MandatoryEntryPointInclude.hlsl"
#include "DenoiserCommon.hlsl"
#include "ColorConversion.hlsl"
#include "Constants.hlsl"

static const uint GroupDimensionSize = 8;

[numthreads(GroupDimensionSize, GroupDimensionSize, 1)]
void CSMain(int3 dispatchThreadID : SV_DispatchThreadID, int3 groupThreadID : SV_GroupThreadID)
{
    uint2 downscaledPixelIdx = dispatchThreadID.xy;
    uint2 fullPixelIndex = downscaledPixelIdx * GradientUpscaleCoefficient;

    Texture2D gradientInputsTexture = Textures2D[PassDataCB.GradientInputsTexIdx];
    Texture2D shadowedShadingTexture = Textures2D[PassDataCB.ShadowedShadingTexIdx];
    Texture2D<uint4> gradientSamplePositionsTexture = UInt4_Textures2D[PassDataCB.SamplePositionsTexIdx];

    RWTexture2D<float4> gradientOutputTexture = RW_Float4_Textures2D[PassDataCB.GradientTexIdx];

    uint packedStratumPosition = gradientSamplePositionsTexture[downscaledPixelIdx].x;
    SetDataInspectorWriteCondition(all(FrameDataCB.MousePosition / 3 == downscaledPixelIdx));
    if (packedStratumPosition == InvalidStrataPositionIndicator)
    {
        gradientOutputTexture[downscaledPixelIdx].r = 0.0;
        return;
    }

    uint2 stratumPosition = UnpackStratumPosition(packedStratumPosition);
    uint2 gradientSamplePixelIdx = fullPixelIndex + stratumPosition;

    float currentFrameLuminance = CIELuminance(shadowedShadingTexture[gradientSamplePixelIdx].rgb);
    float previousFrameLuminance = gradientInputsTexture[downscaledPixelIdx].r;

    float gradient = GetHFGradient(currentFrameLuminance, previousFrameLuminance);

    gradientOutputTexture[downscaledPixelIdx].r = gradient;

    
    OutputDataInspectorValue(float2(currentFrameLuminance, previousFrameLuminance));
    OutputDataInspectorValue(currentFrameLuminance - previousFrameLuminance);
    OutputDataInspectorValue(fullPixelIndex);
    OutputDataInspectorValue(gradientSamplePixelIdx);
    /*  OutputDataInspectorValue(downscaledPixelIdx);
      OutputDataInspectorValue(stratumPosition);*/
}

#endif
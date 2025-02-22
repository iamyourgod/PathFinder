#include "DeferredShadowsRenderPass.hpp"

#include <Foundation/Halton.hpp>

namespace PathFinder
{

    DeferredShadowsRenderPass::DeferredShadowsRenderPass()
        : RenderPass("DeferredShadows") {} 

    void DeferredShadowsRenderPass::SetupPipelineStates(PipelineStateCreator* stateCreator)
    {
        stateCreator->CreateRayTracingState(PSONames::DeferredShadows, [this](RayTracingStateProxy& state)
        {
            state.RayGenerationShaderFileName = "DeferredShadows.hlsl";
            state.AddMissShader({ "DeferredShadows.hlsl", "ShadowRayMiss" });
            state.AddMissShader({ "DeferredShadows.hlsl", "AORayMiss" });
            state.ShaderConfig = HAL::RayTracingShaderConfig{ sizeof(float), sizeof(float) * 2 };
            state.GlobalRootSignatureName = RootSignatureNames::ShadingCommon;
            state.PipelineConfig = HAL::RayTracingPipelineConfig{ 1 };
        });
    }
     
    void DeferredShadowsRenderPass::ScheduleResources(ResourceScheduler<RenderPassContentMediator>* scheduler)
    { 
        auto currentFrameIndex = scheduler->GetFrameNumber() % 2;
        auto previousFrameIndex = (scheduler->GetFrameNumber() - 1) % 2;

        NewTextureProperties outputProperties{ HAL::ColorFormat::RGBA16_Float };
        outputProperties.MipCount = 5;

        scheduler->NewTexture(ResourceNames::StochasticShadowedShadingOutput[currentFrameIndex], MipSet::FirstMip(), outputProperties);
        scheduler->NewTexture(ResourceNames::StochasticShadowedShadingOutput[previousFrameIndex], MipSet::Empty(), outputProperties);
        scheduler->NewTexture(ResourceNames::StochasticUnshadowedShadingOutput, MipSet::FirstMip(), outputProperties);

        scheduler->ReadTexture(ResourceNames::DenoisedReprojectedTexelIndices);
        scheduler->ReadTexture(ResourceNames::DenoiserGradientSamplePositions[currentFrameIndex]);
        scheduler->ReadTexture(ResourceNames::GBufferAlbedoMetalnessPatched);
        scheduler->ReadTexture(ResourceNames::GBufferNormalRoughnessPatched);
        scheduler->ReadTexture(ResourceNames::GBufferMotionVector);
        scheduler->ReadTexture(ResourceNames::GBufferTypeAndMaterialIndex);
        scheduler->ReadTexture(ResourceNames::GBufferViewDepthPatched);
        scheduler->ReadTexture(ResourceNames::DeferredLightingRayPDFs);
        scheduler->ReadTexture(ResourceNames::DeferredLightingRayLightIntersectionPoints);

        scheduler->UseRayTracing();
    } 

    void DeferredShadowsRenderPass::Render(RenderContext<RenderPassContentMediator>* context)
    {
        context->GetCommandRecorder()->ApplyPipelineState(PSONames::DeferredShadows);

        const Scene* scene = context->GetContent()->GetScene();
        const SceneGPUStorage* sceneStorage = context->GetContent()->GetSceneGPUStorage();
        const Memory::Texture* blueNoiseTexture = scene->GetBlueNoiseTexture();

        auto currentFrameIndex = context->GetFrameNumber() % 2;
        auto resourceProvider = context->GetResourceProvider();

        DeferredShadowsCBContent cbContent{};

        cbContent.ReprojectedTexelIndicesTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::DenoisedReprojectedTexelIndices);
        cbContent.DenoiserGradientSamplePositionsTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::DenoiserGradientSamplePositions[currentFrameIndex]);
        cbContent.GBufferIndices.AlbedoMetalnessTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::GBufferAlbedoMetalnessPatched);
        cbContent.GBufferIndices.NormalRoughnessTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::GBufferNormalRoughnessPatched);
        cbContent.GBufferIndices.MotionTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::GBufferMotionVector);
        cbContent.GBufferIndices.TypeAndMaterialTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::GBufferTypeAndMaterialIndex);
        cbContent.GBufferIndices.ViewDepthTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::GBufferViewDepthPatched);
        cbContent.ShadowRayPDFsTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::DeferredLightingRayPDFs);
        cbContent.ShadowRayIntersectionPointsTexIdx = resourceProvider->GetSRTextureIndex(ResourceNames::DeferredLightingRayLightIntersectionPoints);
        cbContent.StochasticShadowedOutputTexIdx = resourceProvider->GetUATextureIndex(ResourceNames::StochasticShadowedShadingOutput[currentFrameIndex]);
        cbContent.StochasticUnshadowedOutputTexIdx = resourceProvider->GetUATextureIndex(ResourceNames::StochasticUnshadowedShadingOutput);
        cbContent.BlueNoiseTexIdx = blueNoiseTexture->GetSRDescriptor()->IndexInHeapRange();
        cbContent.BlueNoiseTexSize = blueNoiseTexture->Properties().Dimensions.Width;
        cbContent.BlueNoiseTexDepth = blueNoiseTexture->Properties().Dimensions.Depth;
        cbContent.FrameNumber = context->GetFrameNumber();

        context->GetConstantsUpdater()->UpdateRootConstantBuffer(cbContent);
        context->GetCommandRecorder()->SetRootConstants(sceneStorage->GetCompressedLightPartitionInfo(), 0, 0);

        const Memory::Buffer* bvh = sceneStorage->TopAccelerationStructure().AccelerationStructureBuffer();
        const Memory::Buffer* lights = sceneStorage->LightTable();
        const Memory::Buffer* materials = sceneStorage->MaterialTable();

        if (bvh) context->GetCommandRecorder()->BindExternalBuffer(*bvh, 0, 0, HAL::ShaderRegister::ShaderResource);
        if (lights) context->GetCommandRecorder()->BindExternalBuffer(*lights, 1, 0, HAL::ShaderRegister::ShaderResource);
        if (materials) context->GetCommandRecorder()->BindExternalBuffer(*materials, 2, 0, HAL::ShaderRegister::ShaderResource);

        context->GetCommandRecorder()->DispatchRays(context->GetDefaultRenderSurfaceDesc().Dimensions());
    }

}

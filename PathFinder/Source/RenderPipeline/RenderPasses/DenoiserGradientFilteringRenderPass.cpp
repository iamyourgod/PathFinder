#include "DenoiserGradientFilteringRenderPass.hpp"
#include "UAVClearHelper.hpp"

#include <Foundation/Gaussian.hpp>

namespace PathFinder
{

    DenoiserGradientFilteringRenderPass::DenoiserGradientFilteringRenderPass()
        : RenderPass("DenoiserGradientFiltering") {}

    void DenoiserGradientFilteringRenderPass::SetupPipelineStates(PipelineStateCreator* stateCreator)
    {
        stateCreator->CreateComputeState(PSONames::DenoiserGradientFiltering, [](ComputeStateProxy& state)
        {
            state.ComputeShaderFileName = "DenoiserGradientAtrousWaveletFilter.hlsl";
        });
    }

    void DenoiserGradientFilteringRenderPass::ScheduleResources(ResourceScheduler<RenderPassContentMediator>* scheduler)
    {
        if (!scheduler->GetContent()->GetSettings()->IsDenoiserEnabled)
            return;

        scheduler->AliasAndWriteTexture(ResourceNames::DenoiserGradient, ResourceNames::DenoiserGradientFilteredIntermediate);
        scheduler->NewTexture(ResourceNames::DenoiserGradientFiltered, NewTextureProperties{ ResourceNames::DenoiserGradientSamples });
    }
     
    void DenoiserGradientFilteringRenderPass::Render(RenderContext<RenderPassContentMediator>* context)
    {
        context->GetCommandRecorder()->ApplyPipelineState(PSONames::DenoiserGradientFiltering);

        auto resourceProvider = context->GetResourceProvider();

        const Geometry::Dimensions& outputDimensions = resourceProvider->GetTextureProperties(ResourceNames::DenoiserGradientFiltered).Dimensions;
        auto inputTexIdx = resourceProvider->GetUATextureIndex(ResourceNames::DenoiserGradientFilteredIntermediate);
        auto outputTexIdx = resourceProvider->GetUATextureIndex(ResourceNames::DenoiserGradientFiltered);

        DenoiserGradientFilteringCBContent cbContent{};
        cbContent.ImageSize = { outputDimensions.Width, outputDimensions.Height };

        for (auto i = 0u; i < 3u; ++i)
        {
            cbContent.CurrentIteration = i;
            cbContent.InputTexIdx = inputTexIdx;
            cbContent.OutputTexIdx = outputTexIdx;

            context->GetConstantsUpdater()->UpdateRootConstantBuffer(cbContent);
            context->GetCommandRecorder()->Dispatch(outputDimensions, { 8, 8 });

            std::swap(inputTexIdx, outputTexIdx);
        }
    }

}

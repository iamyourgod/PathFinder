#pragma once

#include "../RenderPass.hpp"
#include "../RenderPassContentMediator.hpp"

#include "PipelineNames.hpp"
#include "DownsamplingRenderSubPass.hpp"
#include "GBufferTextureIndices.hpp"

namespace PathFinder
{

    struct DenoiserPostBlurCBContent
    {
        GPUIlluminanceField ProbeField;
        GBufferTextureIndices GBufferIndices;
        glm::uvec2 DispatchGroupCount;
        uint32_t AccumulatedFramesCountTexIdx;
        uint32_t AnalyticShadingTexIdx;
        uint32_t SecondaryGradientTexIdx;
        uint32_t ShadowedShadingTexIdx;
        uint32_t UnshadowedShadingTexIdx;
        uint32_t ShadowedShadingBlurredOutputTexIdx;
        uint32_t UnshadowedShadingBlurredOutputTexIdx;
        uint32_t CombinedShadingTexIdx;
        uint32_t CombinedShadingOversaturatedTexIdx;
    };

    class DenoiserPostBlurRenderPass : public RenderPass<RenderPassContentMediator>
    { 
    public: 
        DenoiserPostBlurRenderPass();
        ~DenoiserPostBlurRenderPass() = default;

        virtual void SetupPipelineStates(PipelineStateCreator* stateCreator) override;
        virtual void ScheduleResources(ResourceScheduler<RenderPassContentMediator>* scheduler) override;
        virtual void Render(RenderContext<RenderPassContentMediator>* context) override;
    };

}

#pragma once

#include "../RenderPass.hpp"
#include "../RenderPassContentMediator.hpp"

#include "PipelineNames.hpp"

namespace PathFinder
{

     struct DenoiserGradientConstructionCBContent
     {
         uint32_t GradientInputsTexIdx;
         uint32_t SamplePositionsTexIdx;
         uint32_t ShadowedShadingTexIdx;
         uint32_t GradientTexIdx;
     };

    class DenoiserGradientConstructionRenderPass : public RenderPass<RenderPassContentMediator>
    { 
    public: 
        DenoiserGradientConstructionRenderPass();
        ~DenoiserGradientConstructionRenderPass() = default;

        virtual void SetupPipelineStates(PipelineStateCreator* stateCreator) override;
        virtual void ScheduleResources(ResourceScheduler<RenderPassContentMediator>* scheduler) override;
        virtual void Render(RenderContext<RenderPassContentMediator>* context) override;
    };

}

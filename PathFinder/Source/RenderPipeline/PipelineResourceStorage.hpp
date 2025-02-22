#pragma once

#include "RenderSurfaceDescription.hpp"
#include "GlobalRootConstants.hpp"
#include "PerFrameRootConstants.hpp"
#include "PipelineResourceSchedulingInfo.hpp"
#include "PipelineResourceMemoryAliaser.hpp"
#include "PipelineResourceStoragePass.hpp"
#include "PipelineResourceStorageResource.hpp"

#include <HardwareAbstractionLayer/DescriptorHeap.hpp>
#include <HardwareAbstractionLayer/SwapChain.hpp>
#include <Foundation/MemoryUtils.hpp>
#include <Memory/GPUResourceProducer.hpp>
#include <Memory/PoolDescriptorAllocator.hpp>
#include <Memory/ResourceStateTracker.hpp>

#include <vector>
#include <functional>
#include <tuple>
#include <memory>
#include <optional>

#include <robinhood/robin_hood.h>
#include <dtl/dtl.hpp>

namespace PathFinder
{

    class RenderPassGraph;

    class PipelineResourceStorage
    {
    public:
        using ResourceName = Foundation::Name;
        using PassName = Foundation::Name;

        PipelineResourceStorage(
            HAL::Device* device,
            Memory::GPUResourceProducer* resourceProducer,
            Memory::PoolDescriptorAllocator* descriptorAllocator,
            Memory::ResourceStateTracker* stateTracker,
            const RenderSurfaceDescription& defaultRenderSurface,
            const RenderPassGraph* passExecutionGraph
        );

        using DebugBufferIteratorFunc = std::function<void(PassName passName, const float* debugData)>;
        using SchedulingInfoConfigurator = std::function<void(PipelineResourceSchedulingInfo&)>;

        const HAL::RTDescriptor* GetRenderTargetDescriptor(Foundation::Name resourceName, Foundation::Name passName, uint64_t mipIndex = 0) const;
        const HAL::DSDescriptor* GetDepthStencilDescriptor(Foundation::Name resourceName, Foundation::Name passName) const;
        const HAL::SamplerDescriptor* GetSamplerDescriptor(Foundation::Name resourceName) const;

        void BeginFrame();
        void EndFrame();

        bool HasMemoryLayoutChange() const;
        
        PipelineResourceStoragePass& CreatePerPassData(PassName name);

        void StartResourceScheduling();
        void EndResourceScheduling();
        void OptimizeScheduledResourceStates(const RenderPassGraph& passGraph);
        void AllocateScheduledResources();
        
        template <class Constants> 
        void UpdateGlobalRootConstants(const Constants& constants);

        template <class Constants>
        void UpdateFrameRootConstants(const Constants& constants);

        template <class Constants>
        void UpdatePassRootConstants(const Constants& constants, const RenderPassGraph::Node& passNode);

        const Memory::Buffer* GlobalRootConstantsBuffer() const;
        const Memory::Buffer* PerFrameRootConstantsBuffer() const;

        PipelineResourceStoragePass* GetPerPassData(PassName name);
        PipelineResourceStorageResource* GetPerResourceData(ResourceName name);
        const PipelineResourceStoragePass* GetPerPassData(PassName name) const;
        const PipelineResourceStorageResource* GetPerResourceData(ResourceName name) const;

        void IterateDebugBuffers(const DebugBufferIteratorFunc& func) const;

        template <class Func>
        void ForEachResource(const Func& func) const;

        void QueueResourceAllocationIfNeeded(
            PassName passName,
            ResourceName resourceName, 
            const HAL::ResourcePropertiesVariant& properties, 
            std::optional<Foundation::Name> propertyCopySourceName,
            const SchedulingInfoConfigurator& siConfigurator);

        void QueueResourceUsage(PassName passName, ResourceName resourceName, std::optional<ResourceName> aliasName, const SchedulingInfoConfigurator& siConfigurator);
        void QueueResourceReadback(PassName passName, ResourceName resourceName, const SchedulingInfoConfigurator& siConfigurator);
        void AddSampler(Foundation::Name samplerName, const HAL::Sampler& sampler);

    private:
        using SamplerDescriptorPair = std::pair<HAL::Sampler, Memory::PoolDescriptorAllocator::SamplerDescriptorPtr>;
        using ResourceMap = robin_hood::unordered_flat_map<ResourceName, uint64_t>;
        using SamplerMap = robin_hood::unordered_flat_map<ResourceName, SamplerDescriptorPair>;
        using ResourceAliasMap = robin_hood::unordered_flat_map<ResourceName, ResourceName>;
        using ResourceList = std::vector<PipelineResourceStorageResource>;
        using DiffEntryList = std::vector<PipelineResourceStorageResource::DiffEntry>;

        struct ResourceCreationRequest
        {
            HAL::ResourcePropertiesVariant ResourceProperties;
            Foundation::Name ResourceName;
            Foundation::Name ResourceNameToCopyPropertiesFrom;
            Foundation::Name PassName;
        };

        struct SchedulingRequest
        {
            SchedulingInfoConfigurator Configurator;
            Foundation::Name ResourceName;
            Foundation::Name PassName;
        };

        PipelineResourceStorageResource& CreatePerResourceData(ResourceName name, const HAL::ResourceFormat& resourceFormat);
        HAL::Heap* GetHeapForAliasingGroup(HAL::HeapAliasingGroup group);

        bool TransferPreviousFrameResources();

        HAL::Device* mDevice;
        Memory::GPUResourceProducer* mResourceProducer;
        Memory::PoolDescriptorAllocator* mDescriptorAllocator;
        Memory::ResourceStateTracker* mResourceStateTracker;
        const RenderPassGraph* mPassExecutionGraph;

        std::unique_ptr<HAL::Heap> mRTDSHeap;
        std::unique_ptr<HAL::Heap> mNonRTDSHeap;
        std::unique_ptr<HAL::Heap> mBufferHeap;
        std::unique_ptr<HAL::Heap> mUniversalHeap;

        RenderSurfaceDescription mDefaultRenderSurface;

        PipelineResourceMemoryAliaser mRTDSMemoryAliaser;
        PipelineResourceMemoryAliaser mNonRTDSMemoryAliaser;
        PipelineResourceMemoryAliaser mBufferMemoryAliaser;
        PipelineResourceMemoryAliaser mUniversalMemoryAliaser;

        // Constant buffer for global data that changes rarely
        Memory::GPUResourceProducer::BufferPtr mGlobalRootConstantsBuffer;

        // Constant buffer for data that changes every frame
        Memory::GPUResourceProducer::BufferPtr mPerFrameRootConstantsBuffer;

        robin_hood::unordered_node_map<PassName, PipelineResourceStoragePass> mPerPassData;

        std::vector<SchedulingRequest> mSchedulingCreationRequests;
        std::vector<SchedulingRequest> mSchedulingUsageRequests;
        std::vector<SchedulingRequest> mSchedulingReadbackRequests;
        std::vector<ResourceCreationRequest> mPrimaryResourceCreationRequests;
        std::vector<ResourceCreationRequest> mSecondaryResourceCreationRequests;

        // Two sets of resources: current and previous frame
        std::pair<ResourceList, ResourceList> mResourceLists;
        std::pair<ResourceMap, ResourceMap> mResourceMaps;
        ResourceList* mPreviousFrameResources = &mResourceLists.first;
        ResourceList* mCurrentFrameResources = &mResourceLists.second;
        ResourceMap* mPreviousFrameResourceMap = &mResourceMaps.first;
        ResourceMap* mCurrentFrameResourceMap = &mResourceMaps.second;
        ResourceAliasMap mAliasMap;
        SamplerMap mSamplers;

        // Resource diff entries to determine resource allocation needs
        std::pair<DiffEntryList, DiffEntryList> mDiffEntries;
        DiffEntryList* mPreviousFrameDiffEntries = &mDiffEntries.first;
        DiffEntryList* mCurrentFrameDiffEntries = &mDiffEntries.second;

        // Transitions for resources scheduled for readback
        HAL::ResourceBarrierCollection mReadbackBarriers;

        bool mMemoryLayoutChanged = false;
    };

}

#include "PipelineResourceStorage.inl"
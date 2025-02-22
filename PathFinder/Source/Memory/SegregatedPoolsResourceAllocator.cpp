#include "SegregatedPoolsResourceAllocator.hpp"

namespace Memory
{

    SegregatedPoolsResourceAllocator::SegregatedPoolsResourceAllocator(const HAL::Device* device, uint8_t simultaneousFramesInFlight)
        : mDevice{ device }, 
        mRingFrameTracker{ simultaneousFramesInFlight },
        mSimultaneousFramesInFlight{ simultaneousFramesInFlight },
        mUploadPools{ mMinimumSlotSize, mOnGrowSlotCount },
        mReadbackPools{ mMinimumSlotSize, mOnGrowSlotCount },
        mDefaultUniversalOrBufferPools{ mMinimumSlotSize, mOnGrowSlotCount },
        mDefaultRTDSPools{ mMinimumSlotSize, mOnGrowSlotCount },
        mDefaultNonRTDSPools{ mMinimumSlotSize, mOnGrowSlotCount }
    {
        mMinimumSlotSize = device->MinimumHeapSize() / mOnGrowSlotCount;
        mPendingDeallocations.resize(simultaneousFramesInFlight);

        mRingFrameTracker.SetDeallocationCallback([this](const Ring::FrameTailAttributes& frameAttributes)
        {
            auto frameIndex = frameAttributes.Tail - frameAttributes.Size;
            ExecutePendingDeallocations(frameIndex);
        });
    }

    SegregatedPoolsResourceAllocator::BufferPtr SegregatedPoolsResourceAllocator::AllocateBuffer(const HAL::BufferProperties& properties, std::optional<HAL::CPUAccessibleHeapType> heapType)
    {
        HAL::ResourceFormat format{ mDevice, properties };
        Allocation allocation = FindOrAllocateMostFittingFreeSlot(format.ResourceSizeInBytes(), format, heapType);
        PoolsAllocation& poolAllocation = allocation.PoolAllocation;

        auto offsetInHeap = AdjustMemoryOffsetToPointInsideHeap(allocation);

        // If CPU accessible buffer is requested
        if (heapType)
        {
            // We can search for existing one
            if (!poolAllocation.Slot.UserData.Buffer)
            {
                // We need CPU accessible buffers to match slot size so that they can be reused properly
                HAL::BufferProperties cpuAccessibleBufferProperties{ allocation.PoolsPtr->SlotSizeInBucket(poolAllocation.BucketIndex) };

                // Allocate buffer with slot size, not requested size, to make it more generic and suitable for later reuse in other allocations
                poolAllocation.Slot.UserData.Buffer = new HAL::Buffer{ *mDevice, cpuAccessibleBufferProperties, *allocation.HeapPtr, offsetInHeap };
            }

            auto deallocationCallback = [this, poolAllocation, poolsThatProducedAllocation = allocation.PoolsPtr](HAL::Buffer* buffer)
            {
                // Do not pass cpu accessible resource for deallocation. We can reuse it later.
                mPendingDeallocations[mCurrentFrameIndex].emplace_back(Deallocation{ buffer, poolAllocation, poolsThatProducedAllocation, true });
            };

            // Create unique_ptr with already existing buffer ptr that's being reused
            return BufferPtr{ poolAllocation.Slot.UserData.Buffer, deallocationCallback };
        }
        else
        {
            auto deallocationCallback = [this, poolAllocation, poolsThatProducedAllocation = allocation.PoolsPtr](HAL::Buffer* buffer)
            {
                mPendingDeallocations[mCurrentFrameIndex].emplace_back(Deallocation{ buffer, poolAllocation, poolsThatProducedAllocation, false });
            };

            HAL::Buffer* buffer = new HAL::Buffer{ *mDevice, properties, *allocation.HeapPtr, offsetInHeap };

            // The design decision is to recreate buffers in default memory due to different state requirements unlike upload/readback 
            return BufferPtr{ buffer, deallocationCallback };
        }
    }

    SegregatedPoolsResourceAllocator::TexturePtr SegregatedPoolsResourceAllocator::AllocateTexture(const HAL::TextureProperties& properties)
    {
        HAL::ResourceFormat format{ mDevice, properties };
        Allocation allocation = FindOrAllocateMostFittingFreeSlot(format.ResourceSizeInBytes(), format, std::nullopt);
        PoolsAllocation& poolAllocation = allocation.PoolAllocation;

        auto offsetInHeap = AdjustMemoryOffsetToPointInsideHeap(allocation);

        auto deallocationCallback = [this, poolAllocation, poolsThatProducedAllocation = allocation.PoolsPtr](HAL::Texture* texture)
        {
            mPendingDeallocations[mCurrentFrameIndex].emplace_back(Deallocation{ texture, poolAllocation, poolsThatProducedAllocation, false });
        };

        HAL::Texture* texture = new HAL::Texture{ *mDevice, *allocation.HeapPtr, offsetInHeap, properties };

        return TexturePtr{ texture, deallocationCallback };
    }

    void SegregatedPoolsResourceAllocator::BeginFrame(uint64_t frameNumber)
    {
        mCurrentFrameIndex = mRingFrameTracker.Allocate(1);
        mRingFrameTracker.FinishCurrentFrame(frameNumber);
    }

    void SegregatedPoolsResourceAllocator::EndFrame(uint64_t frameNumber)
    {
        mRingFrameTracker.ReleaseCompletedFrames(frameNumber);
    }

    SegregatedPoolsResourceAllocator::Allocation SegregatedPoolsResourceAllocator::FindOrAllocateMostFittingFreeSlot(
        uint64_t allocationSizeInBytes, const HAL::ResourceFormat& resourceFormat, std::optional<HAL::CPUAccessibleHeapType> cpuHeapType)
    {
        assert_format(allocationSizeInBytes > 0, "0 bytes allocations are forbidden");
        assert_format(allocationSizeInBytes < std::numeric_limits<uint32_t>::max(), "Ridiculous allocation size");

        Pools* pools = nullptr;
        std::vector<HeapList>* heapLists = nullptr;

        if (cpuHeapType)
        {
            switch (*cpuHeapType)
            {
            case HAL::CPUAccessibleHeapType::Upload:
                pools = &mUploadPools;
                heapLists = &mUploadHeapLists;
                break;

            case HAL::CPUAccessibleHeapType::Readback:
                pools = &mReadbackPools;
                heapLists = &mReadbackHeapLists;
                break;
            }
        }
        else
        {
            switch (resourceFormat.ResourceAliasingGroup())
            {
            case HAL::HeapAliasingGroup::Universal:
            case HAL::HeapAliasingGroup::Buffers:
                pools = &mDefaultUniversalOrBufferPools;
                heapLists = &mDefaultUniversalOrBufferHeapLists;
                break;

            case HAL::HeapAliasingGroup::RTDSTextures:
                pools = &mDefaultRTDSPools;
                heapLists = &mDefaultRTDSHeapLists;
                break;

            case HAL::HeapAliasingGroup::NonRTDSTextures:
                pools = &mDefaultNonRTDSPools;
                heapLists = &mDefaultNonRTDSHeapLists;
                break;
            }
        }

        PoolsAllocation allocation = pools->Allocate(allocationSizeInBytes);
        Pools::Bucket& bucket = pools->GetBucket(allocation.BucketIndex);

        std::optional<uint64_t> heapListIndex = bucket.UserData.HeapListIndex;

        // No heap list is associated with a bucket yet. Create and associate one.
        if (!heapListIndex)
        {
            heapLists->emplace_back();
            heapListIndex = heapLists->size() - 1;
            bucket.UserData.HeapListIndex = heapListIndex;
        }
        
        HeapList& heapsList = heapLists->at(*heapListIndex);
        std::optional<uint64_t> heapIndex = allocation.Slot.UserData.HeapIndex;

        auto totalHeapsSize = heapsList.size() * mOnGrowSlotCount * bucket.SlotSize();

        bool outOfAllocatedMemory = allocation.Slot.MemoryOffset >= totalHeapsSize;
        bool existingHeapIndexPresent = heapIndex != std::nullopt;

        assert_format(!(outOfAllocatedMemory && existingHeapIndexPresent),
            "Implementation error. Heap index is present but memory offset indicates that a new heap is required.");

        // Out of memory means we need to add another heap
        if (outOfAllocatedMemory)
        {
            auto newHeapSize = mOnGrowSlotCount * bucket.SlotSize();
            heapsList.emplace_back(*mDevice, newHeapSize, resourceFormat.ResourceAliasingGroup(), cpuHeapType);
        }

        // Heap definitely exists at this point but its index is not recorded in the slot,
        // which means this slot has not ever been requested yet.
        if (!existingHeapIndexPresent)
        {
            allocation.Slot.UserData.HeapIndex = heapsList.size() - 1;
        }

        return { allocation, pools, &heapsList[*allocation.Slot.UserData.HeapIndex] };
    }

    uint64_t SegregatedPoolsResourceAllocator::AdjustMemoryOffsetToPointInsideHeap(const SegregatedPoolsResourceAllocator::Allocation& allocation)
    {
        // One heap is created per OnGrowSlotCount slots in a bucket.
        // So we can calculate an offset local to a particular heap.
        uint64_t bytesPerHeap = mOnGrowSlotCount * allocation.PoolsPtr->SlotSizeInBucket(allocation.PoolAllocation.BucketIndex);
        uint64_t allocationHeapIndex = allocation.PoolAllocation.Slot.MemoryOffset / bytesPerHeap;
        uint64_t localOffset = allocation.PoolAllocation.Slot.MemoryOffset - allocationHeapIndex * bytesPerHeap;

        return localOffset;
    }

    void SegregatedPoolsResourceAllocator::ExecutePendingDeallocations(uint64_t frameIndex)
    {
        for (Deallocation& deallocation : mPendingDeallocations[frameIndex])
        {
            if (!deallocation.ResourceWillBeReused)
            {
                delete deallocation.Resource;
            }
            else
            {
                deallocation.Resource->SetDebugName("Resource Allocator Free Memory");
            }

            deallocation.PoolsThatProducedAllocation->Deallocate(deallocation.Allocation);
        }
        mPendingDeallocations[frameIndex].clear();
    }

}

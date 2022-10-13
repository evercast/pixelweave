﻿#include "VulkanVideoConverter.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "DebugUtils.h"

namespace PixelWeave
{
VulkanVideoConverter::VulkanVideoConverter(VulkanDevice* device) : mDevice(nullptr)
{
    device->AddRef();
    mDevice = device;
}

void VulkanVideoConverter::InitResources(const ProtoVideoFrame& src, ProtoVideoFrame& dst)
{
    // Create source buffer and copy CPU memory into it
    const vk::DeviceSize srcBufferSize = src.GetBufferSize();
    mSrcLocalBuffer = mDevice->CreateBuffer(
        srcBufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    mSrcDeviceBuffer = mDevice->CreateBuffer(
        srcBufferSize,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Create CPU readable dest buffer to do conversions in
    const vk::DeviceSize dstBufferSize = dst.GetBufferSize();
    mDstLocalBuffer = mDevice->CreateBuffer(
        dstBufferSize,
        vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    mDstDeviceBuffer = mDevice->CreateBuffer(
        dstBufferSize,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Create compute pipeline and bindings
    mPipelineResources = mDevice->CreateComputePipeline(mSrcDeviceBuffer, mDstDeviceBuffer);
    mCommand = mDevice->CreateCommandBuffer();

    // Record command buffer
    {
        const vk::CommandBufferBeginInfo commandBeginInfo = vk::CommandBufferBeginInfo();
        PW_ASSERT_VK(mCommand.begin(commandBeginInfo));

        // Copy local memory into VRAM and add barrier for next stage
        {
            mCommand.copyBuffer(
                mSrcLocalBuffer.bufferHandle,
                mSrcDeviceBuffer.bufferHandle,
                vk::BufferCopy().setSize(mSrcLocalBuffer.size).setDstOffset(0).setSrcOffset(0));
            const vk::BufferMemoryBarrier bufferBarrier = vk::BufferMemoryBarrier()
                                                              .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                                                              .setBuffer(mSrcDeviceBuffer.bufferHandle)
                                                              .setOffset(0)
                                                              .setSize(mSrcDeviceBuffer.size);
            mCommand.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                {},
                bufferBarrier,
                {});
        }

        // Bind compute shader resources
        mCommand.bindPipeline(vk::PipelineBindPoint::eCompute, mPipelineResources.pipeline);
        mCommand.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            mPipelineResources.pipelineLayout,
            0,
            mPipelineResources.descriptorSet,
            {});

        // Hardcoded to 422 for now
        const uint32_t srcChromaStride = (src.width + 1) / 2;
        const uint32_t srcChromaHeight = src.height;
        const uint32_t srcChromaWidth = (src.width + 1) / 2;

        const uint32_t dstChromaStride = (dst.width + 1) / 2;
        const uint32_t dstChromaHeight = dst.height;
        const uint32_t dstChromaWidth = (dst.width + 1) / 2;

        const InOutPictureInfo pictureInfo{
            {src.width, src.height, src.stride, srcChromaWidth, srcChromaHeight, srcChromaStride},
            {dst.width, dst.height, dst.stride, dstChromaWidth, dstChromaHeight, dstChromaStride}};
        mCommand
            .pushConstants(mPipelineResources.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(InOutPictureInfo), &pictureInfo);

        const uint32_t groupCountX = dstChromaWidth / 16;
        const uint32_t groupCountY = dstChromaHeight / 8;
        mCommand.dispatch(groupCountX, groupCountY, 1);

        // Wait for compute stage and copy results back to local memory
        {
            const vk::BufferMemoryBarrier bufferBarrier = vk::BufferMemoryBarrier()
                                                              .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                                                              .setBuffer(mDstDeviceBuffer.bufferHandle)
                                                              .setOffset(0)
                                                              .setSize(mDstDeviceBuffer.size);
            mCommand.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlags{},
                {},
                bufferBarrier,
                {});

            mCommand.copyBuffer(
                mDstDeviceBuffer.bufferHandle,
                mDstLocalBuffer.bufferHandle,
                vk::BufferCopy().setSize(mDstDeviceBuffer.size).setDstOffset(0).setSrcOffset(0));
        }

        PW_ASSERT_VK(mCommand.end());
    }
}

void VulkanVideoConverter::Cleanup()
{
    mDevice->DestroyCommand(mCommand);
    mDevice->DestroyComputePipeline(mPipelineResources);
    mDevice->DestroyBuffer(mSrcLocalBuffer);
    mDevice->DestroyBuffer(mSrcDeviceBuffer);
    mDevice->DestroyBuffer(mDstLocalBuffer);
    mDevice->DestroyBuffer(mDstDeviceBuffer);
}

struct Timer {
    void Start() { beginTime = std::chrono::steady_clock::now(); }
    template <typename M>
    uint64_t Elapsed()
    {
        return std::chrono::duration_cast<M>(std::chrono::steady_clock::now() - beginTime).count();
    }
    uint64_t ElapsedMillis() { return Elapsed<std::chrono::milliseconds>(); }
    uint64_t ElapsedMicros() { return Elapsed<std::chrono::microseconds>(); }

    std::chrono::steady_clock::time_point beginTime;
};

bool AreFramePropertiesEqual(const ProtoVideoFrame& frameA, const ProtoVideoFrame& frameB)
{
    return frameA.stride == frameB.stride && frameA.width == frameB.width && frameA.height == frameB.height &&
           frameA.pixelFormat == frameB.pixelFormat;
}

void VulkanVideoConverter::Convert(const ProtoVideoFrame& src, ProtoVideoFrame& dst)
{
    Timer timer;
    timer.Start();

    const bool wasInitialized = mPrevSourceFrame.has_value() && mPrevDstFrame.has_value();
    if (!wasInitialized || !AreFramePropertiesEqual(mPrevSourceFrame.value(), src) ||
        !AreFramePropertiesEqual(mPrevDstFrame.value(), dst)) {
        if (wasInitialized) {
            Cleanup();
        }
        InitResources(src, dst);
        mPrevSourceFrame = src;
        mPrevDstFrame = dst;
    }

    // Copy src buffer into GPU readable buffer
    Timer stageTimer;
    stageTimer.Start();
    const vk::DeviceSize srcBufferSize = src.GetBufferSize();
    uint8_t* mappedSrcBuffer = mDevice->MapBuffer(mSrcLocalBuffer);
    std::copy_n(src.buffer, srcBufferSize, mappedSrcBuffer);
    mDevice->UnmapBuffer(mSrcLocalBuffer);
    PW_LOG("Copying src buffer took " << stageTimer.ElapsedMicros() << " us");

    // Dispatch command in compute queue
    stageTimer.Start();
    vk::Fence computeFence = mDevice->CreateFence();
    mDevice->SubmitCommand(mCommand, computeFence);
    mDevice->WaitForFence(computeFence);
    mDevice->DestroyFence(computeFence);
    PW_LOG("Compute took " << stageTimer.ElapsedMicros() << " us");

    // Copy contents into CPU buffer
    stageTimer.Start();
    const vk::DeviceSize dstBufferSize = dst.GetBufferSize();
    uint8_t* mappedDstBuffer = mDevice->MapBuffer(mDstLocalBuffer);
    std::copy_n(mappedDstBuffer, dstBufferSize, dst.buffer);
    mDevice->UnmapBuffer(mDstLocalBuffer);
    PW_LOG("Copy back took " << stageTimer.ElapsedMicros() << " us");

    PW_LOG("Total frame processing time: " << timer.ElapsedMillis() << " ms" << std::endl);
}

VulkanVideoConverter::~VulkanVideoConverter()
{
    Cleanup();
    mDevice->Release();
}

}  // namespace PixelWeave

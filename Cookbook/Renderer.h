#pragma once

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>

#define VMA_IMPLEMENTATION
#include "VulkanMemoryAllocator/vk_mem_alloc.h"

// Utils
#define vkCheck(x)														\
		{ VkResult err = x;												\
		if (err)														\
		{																\
			std::cout <<"Detected Vulkan error: " << err << std::endl;	\
			__debugbreak();													\
		}}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cout << "\033[1;33mWarning: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		// NOTE: temporary fix because it shows a initial error because of a json in medal tv, idk why
		if (!strstr(pCallbackData->pMessage, "medal"))
			std::cerr << "\033[1;31mError: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}
	else
	{
		std::cerr << "\033[1;36mInfo: " << pCallbackData->pMessageIdName << " : \033[0m" << pCallbackData->pMessage << std::endl;
	}

	return VK_FALSE;
}

namespace Renderer
{
	// Structs
	struct WaitSemaphoreInfo
	{
		VkSemaphore semaphore;
		VkPipelineStageFlags waitingStage;
	};

	struct GPUBuffer
	{
		VkBuffer buffer;
		VmaAllocation allocation;
	};

	struct BufferTransition 
	{
		VkBuffer        Buffer;
		VkAccessFlags   CurrentAccess;
		VkAccessFlags   NewAccess;
		uint32_t        CurrentQueueFamily;
		uint32_t        NewQueueFamily;
	};

	VmaAllocator allocator;

	// Declarations
	void CreateCommandPool(VkDevice device,
						   VkCommandPoolCreateFlags flags,
						   uint32_t queueFamily,
						   VkCommandPool& commandPool);
	void AllocateCommandBuffers(VkDevice device,
								VkCommandPool commandPool,
								uint32_t count,
								std::vector<VkCommandBuffer>& commandBuffers,
								VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	void BeginCommandBufferRecording(VkCommandBuffer commandBuffer,
									 VkCommandBufferUsageFlags usage,
									 VkCommandBufferInheritanceInfo* secondaryCommandBufferInfo);
	void EndCommandBufferRecording(VkCommandBuffer commandBuffer);
	void ResetCommandBuffer(VkCommandBuffer commandBuffer, bool releaseResources);
	void ResetCommandPool(VkDevice device, VkCommandPool commandPool, bool releaseResources);
	void CreateSemaphore(VkDevice device, VkSemaphore& semaphore);
	void CreateFence(VkDevice device, VkFence& fence, bool signaled = true);
	void WaitForFences(VkDevice device,
					   std::vector<VkFence>& fences,
					   bool waitForAll = true,
					   uint64_t timeout = UINT64_MAX);
	void ResetFences(VkDevice device, std::vector<VkFence>& fences);
	void SubmitCommandBuffersToQueue(VkQueue queue,
									 std::vector<WaitSemaphoreInfo> waitSemaphoreInfos,
									 std::vector<VkCommandBuffer> commandBuffers,
									 std::vector<VkSemaphore> signalSemaphores,
									 VkFence fence);
	void QueueWaitIdle(VkQueue queue);
	void DeviceWaitIdle(VkDevice device);
	void CreateBuffer(VkDevice device, 
					  VkBufferUsageFlags usage,
					  VkDeviceSize size, 
					  GPUBuffer& buffer, 
					  VkBufferCreateFlags flags = 0);

	// Implementations
	void CreateCommandPool(VkDevice device, VkCommandPoolCreateFlags flags, uint32_t queueFamily, VkCommandPool& commandPool)
	{
		VkCommandPoolCreateInfo commandPoolCreateInfo = {};
		commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		commandPoolCreateInfo.pNext = nullptr;
		commandPoolCreateInfo.flags = flags;
		commandPoolCreateInfo.queueFamilyIndex = queueFamily;
		vkCheck(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool));
	}

	void AllocateCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t count, std::vector<VkCommandBuffer>& commandBuffers, VkCommandBufferLevel level /*= VK_COMMAND_BUFFER_LEVEL_PRIMARY*/)
	{
		commandBuffers.resize(count);
		VkCommandBufferAllocateInfo commandBufferAllocInfo = {};
		commandBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferAllocInfo.pNext = nullptr;
		commandBufferAllocInfo.commandPool = commandPool;
		commandBufferAllocInfo.commandBufferCount = count;
		commandBufferAllocInfo.level = level;
		vkCheck(vkAllocateCommandBuffers(device, &commandBufferAllocInfo, commandBuffers.data()));
	}

	void BeginCommandBufferRecording(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags usage, VkCommandBufferInheritanceInfo* secondaryCommandBufferInfo)
	{
		VkCommandBufferBeginInfo commandBufferBeginInfo = {};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBufferBeginInfo.pNext = nullptr;
		commandBufferBeginInfo.pInheritanceInfo = secondaryCommandBufferInfo;
		commandBufferBeginInfo.flags = usage;

		vkCheck(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
	}

	void EndCommandBufferRecording(VkCommandBuffer commandBuffer)
	{
		vkCheck(vkEndCommandBuffer(commandBuffer));
	}

	void ResetCommandBuffer(VkCommandBuffer commandBuffer, bool releaseResources)
	{
		vkCheck(vkResetCommandBuffer(commandBuffer, releaseResources ? VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT : 0));
	}

	void ResetCommandPool(VkDevice device, VkCommandPool commandPool, bool releaseResources)
	{
		vkCheck(vkResetCommandPool(device, commandPool, releaseResources ? VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT : 0));
	}

	void CreateSemaphore(VkDevice device, VkSemaphore& semaphore)
	{
		VkSemaphoreCreateInfo semaphoreCreateInfo = {};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreCreateInfo.pNext = nullptr;
		semaphoreCreateInfo.flags = 0;
		vkCheck(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore));
	}

	void CreateFence(VkDevice device, VkFence& fence, bool signaled /*= true*/)
	{
		VkFenceCreateInfo fenceCreateInfo = {};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.pNext = nullptr;
		fenceCreateInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
		vkCheck(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
	}

	void WaitForFences(VkDevice device, std::vector<VkFence>& fences, bool waitForAll /*= true*/, uint64_t timeout /*= UINT64_MAX*/)
	{
		vkCheck(vkWaitForFences(device, fences.size(), fences.data(), waitForAll, timeout));
	}

	void ResetFences(VkDevice device, std::vector<VkFence>& fences)
	{
		vkCheck(vkResetFences(device, fences.size(), fences.data()));
	}

	void SubmitCommandBuffersToQueue(VkQueue queue, std::vector<WaitSemaphoreInfo> waitSemaphoreInfos, std::vector<VkCommandBuffer> commandBuffers, std::vector<VkSemaphore> signalSemaphores, VkFence fence)
	{
		std::vector<VkSemaphore> waitSemaphores;
		std::vector<VkPipelineStageFlags> waitSemaphoreStages;

		for (auto& waitSemaphoreInfo : waitSemaphoreInfos)
		{
			waitSemaphores.emplace_back(waitSemaphoreInfo.semaphore);
			waitSemaphoreStages.emplace_back(waitSemaphoreInfo.waitingStage);
		}

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pNext = nullptr;
		submitInfo.waitSemaphoreCount = waitSemaphores.size();
		submitInfo.pWaitSemaphores = waitSemaphores.data();
		submitInfo.pWaitDstStageMask = waitSemaphoreStages.data();
		submitInfo.commandBufferCount = commandBuffers.size();
		submitInfo.pCommandBuffers = commandBuffers.data();
		submitInfo.signalSemaphoreCount = signalSemaphores.size();
		submitInfo.pSignalSemaphores = signalSemaphores.data();

		vkCheck(vkQueueSubmit(queue, 1, &submitInfo, fence));
	}

	void SynchronizeTwoCommandBuffers(VkQueue                         first_queue,
									  std::vector<WaitSemaphoreInfo>  first_wait_semaphore_infos,
									  std::vector<VkCommandBuffer>    first_command_buffers,
									  std::vector<WaitSemaphoreInfo>  synchronizing_semaphores,
									  VkQueue                         second_queue,
									  std::vector<VkCommandBuffer>    second_command_buffers,
									  std::vector<VkSemaphore>        second_signal_semaphores,
									  VkFence                         second_fence) {
		std::vector<VkSemaphore> first_signal_semaphores;
		for (auto& semaphore_info : synchronizing_semaphores) {
			first_signal_semaphores.emplace_back(semaphore_info.semaphore);
		}
		SubmitCommandBuffersToQueue(first_queue, first_wait_semaphore_infos, first_command_buffers, first_signal_semaphores, VK_NULL_HANDLE);
		SubmitCommandBuffersToQueue(second_queue, synchronizing_semaphores, second_command_buffers, second_signal_semaphores, second_fence);
	}

	void QueueWaitIdle(VkQueue queue)
	{
		vkCheck(vkQueueWaitIdle(queue));
	}

	void DeviceWaitIdle(VkDevice device)
	{
		vkCheck(vkDeviceWaitIdle(device));
	}

	void CreateBuffer(VkDevice device, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VkDeviceSize size, GPUBuffer& buffer, VkBufferCreateFlags flags /*= 0*/)
	{
		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.pNext = nullptr;
		bufferCreateInfo.flags = flags;
		bufferCreateInfo.size = size;
		bufferCreateInfo.usage = bufferUsage;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.queueFamilyIndexCount = 0;
		bufferCreateInfo.pQueueFamilyIndices = nullptr;

		VmaAllocationCreateInfo allocationInfo = {};
		allocationInfo.usage = memoryUsage;

		vmaCreateBuffer(allocator, &bufferCreateInfo, &allocationInfo, &buffer.buffer, &buffer.allocation, nullptr);
	}
}

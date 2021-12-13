#if 0

#include <stdio.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <D3Dcompiler.h>

#include <vector>

using namespace Microsoft::WRL;

// Globals
float width = 1280.0f;
float height = 720.0f;
float aspectRatio = width / height;

constexpr int maxFrames = 2;
int frameIndex;

#define check(call) \
		{ HRESULT result_ = call; \
		assert(result_ == S_OK); } 


ID3D12Device* device;
ID3D12CommandQueue* commandQueue;
IDXGISwapChain3* swapChain;
ID3D12DescriptorHeap* rtvDescriptorHeap;
uint32_t rtvDescriptorHeapSize;
ID3D12Resource* renderTarget[2];
ID3D12CommandAllocator* commandAllocator;
ID3D12GraphicsCommandList* commandList;
ID3D12Fence* fence;
HANDLE fenceEvent;
uint64_t fenceValue;
ID3D12PipelineState* pipelineState;

void Init(GLFWwindow* window)
{
	uint32_t dxgiFactoryFlags = 0;
#ifdef _DEBUG
	{
		ID3D12Debug* debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	// Create factory
	IDXGIFactory4* factory;
	IDXGIFactory6* factory6;
	check(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
	check(factory->QueryInterface(IID_PPV_ARGS(&factory6)));

	// Get adapter
	IDXGIAdapter1* adapter;
	for (uint32_t adapterIndex = 0;
		 SUCCEEDED(factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
		 adapterIndex++)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			break;
	}

	// Create device
	check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

	// Create Command queue
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	check(device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue)));

	// Create swap chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = maxFrames;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	IDXGISwapChain1* tempSwapChain;
	check(factory6->CreateSwapChainForHwnd(commandQueue, glfwGetWin32Window(window), &swapChainDesc, nullptr, nullptr, &tempSwapChain));
	tempSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain));

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDescriptorHeapDesc.NumDescriptors = maxFrames;

		check(device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));

		rtvDescriptorHeapSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		for (uint32_t i = 0; i < maxFrames; i++)
		{
			check(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTarget[i])));
			device->CreateRenderTargetView(renderTarget[i], nullptr, rtvHandle);
			rtvHandle.Offset(1, rtvDescriptorHeapSize);
		}
	}

	// Create command allocator
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	// Create command list
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
	commandList->Close();

	// Create synchronization objects
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	fenceValue = 1;

	fenceEvent = CreateEvent(nullptr, false, false, nullptr);
}

void Render(GLFWwindow* window)
{
	check(commandAllocator->Reset());
	check(commandList->Reset(commandAllocator, pipelineState));

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
									 renderTarget[frameIndex],
									 D3D12_RESOURCE_STATE_PRESENT,
									 D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorHeapSize);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
									 renderTarget[frameIndex],
									 D3D12_RESOURCE_STATE_RENDER_TARGET,
									 D3D12_RESOURCE_STATE_PRESENT));

	commandList->Close();

	ID3D12CommandList* ppCommandList[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

	swapChain->Present(1, 0);

	// Signal and increment the fence value.
	const UINT64 currentFenceValue = fenceValue;
	check(commandQueue->Signal(fence, currentFenceValue));
	fenceValue++;

	// Wait until the previous frame is finished.
	if (fence->GetCompletedValue() < currentFenceValue)
	{
		check(fence->SetEventOnCompletion(currentFenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	frameIndex = swapChain->GetCurrentBackBufferIndex();
}

int main()
{
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* window = glfwCreateWindow(width, height, "DirectX 12", 0, 0);
	assert(window);

	Init(window);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		Render(window);
	}

	glfwDestroyWindow(window);

	return 0;
}
#endif
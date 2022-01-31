#if 1

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

#include <iostream>
#include <vector>
#include <chrono>

using namespace Microsoft::WRL;

// Globals
float width = 1600.0f;
float height = 900.0f;
float aspectRatio = width / height;

static const int backbufferCount = 2;
int currentBuffer;

#define check(call) \
		{ HRESULT result_ = call; \
		assert(result_ == S_OK); } 


struct Vertex
{
	float position[3];
	float color[3];
};

struct
{
	glm::mat4 projectionMatrix;
	glm::mat4 modelMatrix;
	glm::mat4 viewMatrix;
} cbVS;


ID3D12Device* device;

IDXGISwapChain3* swapchain;
D3D12_VIEWPORT viewport;
D3D12_RECT surfaceSize;

ID3D12DescriptorHeap* rtvDescriptorHeap;
uint32_t rtvDescriptorHeapSize;
ID3D12Resource* renderTargets[backbufferCount];


ID3D12Fence* fence;
HANDLE fenceEvent;
uint64_t fenceValue;
uint32_t frameIndex;

ID3D12RootSignature* rootSignature;

ID3D12Resource* vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

ID3D12Resource* indexBuffer;
D3D12_INDEX_BUFFER_VIEW indexBufferView;

ID3D12Resource* constantBuffer;
ID3D12DescriptorHeap* constantBufferHeap;
uint8_t* mappedConstantBuffer;

ID3D12CommandQueue* commandQueue;
ID3D12CommandAllocator* commandAllocator;
ID3D12GraphicsCommandList* commandList;
ID3D12PipelineState* initialPipelineState;

std::chrono::time_point<std::chrono::steady_clock> tStart, tEnd;
float elapsedTime = 0.0f;

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

		if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
			break;
	}

	// Create device
	check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

	// Create Command queue
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	check(device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue)));

	// Create command allocator
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	// Create synchronization objects
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	fenceValue = 1;

	fenceEvent = CreateEvent(nullptr, false, false, nullptr);

	// Create Barrier
	/**
	 * A Barrier lets the driver know how a resource should be used in upcoming commands. 
	 * This can be useful if say, you're writing to a texture, 
	 * and you want to copy that texture to another texture (such as the swapchain's render attachment).
	*/
	/**
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texResource;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList->ResourceBarrier(1, &barrier);
	*/

	// Create viewport and scissor
	viewport.Width = width;
	viewport.Height = height;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	surfaceSize.left = 0;
	surfaceSize.top = 0;
	surfaceSize.right = width;
	surfaceSize.bottom = height;

	// Create swap chain
	if (swapchain != nullptr)
	{
		// Create Render Target Attachments from swapchain
		swapchain->ResizeBuffers(backbufferCount, width, height,
								 DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	}
	else
	{

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = width;
		swapChainDesc.Height = height;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = backbufferCount;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		IDXGISwapChain1* tempSwapChain;
		check(factory6->CreateSwapChainForHwnd(commandQueue, glfwGetWin32Window(window), &swapChainDesc, nullptr, nullptr, &tempSwapChain));
		tempSwapChain->QueryInterface(IID_PPV_ARGS(&swapchain));
	}
	frameIndex = swapchain->GetCurrentBackBufferIndex();

	// Descriptor heaps are objects that handle memory allocation required for storing the descriptions of objects that shaders reference.
	// Describe and create a render target view (RTV) descriptor heap.
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDescriptorHeapDesc.NumDescriptors = backbufferCount;

		check(device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));

		rtvDescriptorHeapSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a render target view(RTV) for each frame
		for (uint32_t i = 0; i < backbufferCount; i++)
		{
			check(swapchain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])));
			device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
			rtvHandle.Offset(1, rtvDescriptorHeapSize);
		}
	}

	// Create Root signature
	// Determine if we can get Root Signature Version 1.1
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	   featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	// Individual GPU Resources
	D3D12_DESCRIPTOR_RANGE1 ranges[1];
	ranges[0].BaseShaderRegister = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[0].NumDescriptors = 1;
	ranges[0].RegisterSpace = 0;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;
	ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

	// Group of GPU Resources
	D3D12_ROOT_PARAMETER1 rootParameters[1];
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[0].DescriptorTable.pDescriptorRanges = ranges;

	// Overall Layout
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	rootSignatureDesc.Desc_1_1.NumParameters = 1;
	rootSignatureDesc.Desc_1_1.pParameters = rootParameters;

	rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
	rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;

	ID3DBlob* signature;
	ID3DBlob* error;

	try
	{
		// Create the root signature
		check(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
		check(device->CreateRootSignature(0, 
										  signature->GetBufferPointer(),
										  signature->GetBufferSize(), 
										  IID_PPV_ARGS(&rootSignature)));

		rootSignature->SetName(L"Hello Triangle Root Signature");
	}
	catch (std::exception e)
	{
		const char* errStr = (const char*)error->GetBufferPointer();
		std::cout << errStr << std::endl;
		error->Release();
		error = nullptr;
	}

	if (signature)
	{
		signature->Release();
		signature = nullptr;
	}

	/**
	 * About Root Signature:
	 *		While these work well enough, 
	 *		using bindless resources is significantly more easy, 
	 *		Matt Pettineo (@MyNameIsMJP) wrote a blog post and chapter in Ray Tracing Gems 2 about this.
	 */

	// Heaps
	/** 
	 * Heaps are objects that encompass GPU memory.
	 * They can be used to upload resources like vertex buffers or textures to GPU exclusive memory.
	 */

	// Create Vertex Buffer
	Vertex vertexBufferData[3] = { {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
							  {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
							  {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}} };

	const auto vertexBufferSize = sizeof(vertexBufferData);

	// Use D3D12MA(https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
	// Like VMA
	D3D12_HEAP_PROPERTIES heapProps;
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC vertexBufferResourceDesc;
	vertexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexBufferResourceDesc.Alignment = 0;
	vertexBufferResourceDesc.Width = vertexBufferSize;
	vertexBufferResourceDesc.Height = 1;
	vertexBufferResourceDesc.DepthOrArraySize = 1;
	vertexBufferResourceDesc.MipLevels = 1;
	vertexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	vertexBufferResourceDesc.SampleDesc = {1, 0};
	vertexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	vertexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	check(device->CreateCommittedResource(&heapProps, 
										  D3D12_HEAP_FLAG_NONE, 
										  &vertexBufferResourceDesc, 
										  D3D12_RESOURCE_STATE_GENERIC_READ, 
										  nullptr, 
										  IID_PPV_ARGS(&vertexBuffer)));

	// Copy the triangle data to the vertex buffer.
	uint8_t* pVertexDataBegin;

	// We do not intend to read from this resource on the CPU.
	D3D12_RANGE readRange{ 0, 0 };

	vertexBuffer->Map(0, &readRange, (void**)&pVertexDataBegin);
	memcpy(pVertexDataBegin, vertexBufferData, sizeof(vertexBufferData));
	vertexBuffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view.
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof(Vertex);
	vertexBufferView.SizeInBytes = vertexBufferSize;

	// Create index buffer
	uint32_t indexBufferData[3] = { 0, 1, 2 };

	const auto indexBufferSize = sizeof(indexBufferData);

	check(device->CreateCommittedResource(&heapProps, 
										  D3D12_HEAP_FLAG_NONE, 
										  &vertexBufferResourceDesc, 
										  D3D12_RESOURCE_STATE_GENERIC_READ, 
										  nullptr, 
										  IID_PPV_ARGS(&indexBuffer)));

	indexBuffer->Map(0, &readRange, (void**)&pVertexDataBegin);
	memcpy(pVertexDataBegin, indexBufferData, sizeof(indexBufferData));
	indexBuffer->Unmap(0, nullptr);

	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = indexBufferSize;

	// Create constant Buffer
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&constantBufferHeap)));

	D3D12_RESOURCE_DESC cbResourceDesc;
	cbResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbResourceDesc.Alignment = 0;
	cbResourceDesc.Width = (sizeof(cbVS) * 255) & ~255; // CB size is required to be 256-byte aligned.
	cbResourceDesc.Height = 1;
	cbResourceDesc.DepthOrArraySize = 1;
	cbResourceDesc.MipLevels = 1;
	cbResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	cbResourceDesc.SampleDesc = { 1, 0 };
	cbResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	cbResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &cbResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer)));
	constantBufferHeap->SetName(L"Constant Buffer Upload Resource Heap");

	// Create our Constant buffer view
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (sizeof(cbVS) * 255) & ~255; // CB size is required to be 256-byte aligned.

	D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle(constantBufferHeap->GetCPUDescriptorHandleForHeapStart());
	cbvHandle.ptr = cbvHandle.ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 0;
	device->CreateConstantBufferView(&cbvDesc, cbvHandle);

	constantBuffer->Map(0, &readRange, (void**)&mappedConstantBuffer);
	memcpy(mappedConstantBuffer, &cbVS, sizeof(cbVS));
	constantBuffer->Unmap(0, &readRange);

	// Shaders
	// NOTE: Use dxc in the future
	// https://github.com/microsoft/DirectXShaderCompiler
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif
	std::wstring shaderPath = L"src/shaders/triangle_dx12.hlsl";

	ID3DBlob* vertexShaderBlob;
	auto result = D3DCompileFromFile(shaderPath.c_str(), nullptr, nullptr, "vs_main", "vs_5_0", compileFlags, NULL, &vertexShaderBlob, &error);
	if (FAILED(result))
	{
		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
		{
			MessageBoxA(nullptr, "Could not find the vertex shader file", "Error", MB_OK);
			__debugbreak();
		}
		else
		{
			const char* errorString = (const char*)error->GetBufferPointer();
			MessageBoxA(nullptr, errorString, "Error", MB_OK);
			__debugbreak();
		}
	}

	D3D12_SHADER_BYTECODE vsBytecode;
	vsBytecode.pShaderBytecode = vertexShaderBlob->GetBufferPointer();
	vsBytecode.BytecodeLength = vertexShaderBlob->GetBufferSize();

	ID3DBlob* pixelShaderBlob;
	result = D3DCompileFromFile(shaderPath.c_str(), nullptr, nullptr, "ps_main", "ps_5_0", compileFlags, NULL, &pixelShaderBlob, &error);
	if (FAILED(result))
	{
		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
		{
			MessageBoxA(nullptr, "Could not find the vertex shader file", "Error", MB_OK);
			__debugbreak();
		}
		else
		{
			const char* errorString = (const char*)error->GetBufferPointer();
			MessageBoxA(nullptr, errorString, "Error", MB_OK);
			__debugbreak();
		}
	}

	D3D12_SHADER_BYTECODE psBytecode;
	psBytecode.pShaderBytecode = pixelShaderBlob->GetBufferPointer();
	psBytecode.BytecodeLength = pixelShaderBlob->GetBufferSize();

	// Graphics Pipeline
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

	// Input Assembly
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
		 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
		 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0} };

	// Rasterization
	D3D12_RASTERIZER_DESC rasterizerDesc;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FrontCounterClockwise = false;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = true;
    rasterizerDesc.MultisampleEnable = false;
    rasterizerDesc.AntialiasedLineEnable = false;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	// Color/Blend
	D3D12_BLEND_DESC blendDesc;
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc;
	defaultRenderTargetBlendDesc.BlendEnable = false;
	defaultRenderTargetBlendDesc.LogicOpEnable = false;
	defaultRenderTargetBlendDesc.SrcBlend = D3D12_BLEND_ONE;
	defaultRenderTargetBlendDesc.DestBlend = D3D12_BLEND_ZERO;
	defaultRenderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	defaultRenderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	defaultRenderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	defaultRenderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	defaultRenderTargetBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	defaultRenderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	for (uint32_t i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

	psoDesc.pRootSignature = rootSignature;
	psoDesc.VS = vsBytecode;
	psoDesc.PS = psBytecode;
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.RasterizerState = rasterizerDesc;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.BlendState = blendDesc;
	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.StencilEnable = false;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	//Create the raster pipeline state
	try
	{
		check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&initialPipelineState)));
	}
	catch (std::exception e)
	{
		std::cout << "Failed to create Graphics Pipeline!";
	}

	// Create command list
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, initialPipelineState, IID_PPV_ARGS(&commandList));
}

void SetupCommands()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	check(commandAllocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	check(commandList->Reset(commandAllocator, initialPipelineState));

	// Indicate that the back buffer will be used as a render target
	D3D12_RESOURCE_BARRIER renderTargetBarrier;
	renderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	renderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	renderTargetBarrier.Transition.pResource = renderTargets[frameIndex];
	renderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	renderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	renderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &renderTargetBarrier);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHandle.ptr = rtvHandle.ptr + (frameIndex * rtvDescriptorHeapSize);
	commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	// Record raster commands
	const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &surfaceSize);
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);

	commandList->DrawIndexedInstanced(3, 1, 0, 0, 0);

	// Indicate that the back buffer will now be used to present
	D3D12_RESOURCE_BARRIER presentBarrier;
	presentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	presentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	presentBarrier.Transition.pResource = renderTargets[frameIndex];
	presentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	presentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	presentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &presentBarrier);

	check(commandList->Close());
}

void Render(GLFWwindow* window)
{
	// Frame limit set to 60 fps
	tEnd = std::chrono::high_resolution_clock::now();
	float time =
		std::chrono::duration<float, std::milli>(tEnd - tStart).count();
	if (time < (1000.0f / 60.0f))
	{
		return;
	}
	tStart = std::chrono::high_resolution_clock::now();

	// Update Uniforms
	elapsedTime += 0.001f * time;
	elapsedTime = fmodf(elapsedTime, 6.283185307179586f);
	cbVS.modelMatrix = glm::rotate(glm::mat4(1.0f), elapsedTime, glm::vec3(0.0f, 1.0f, 0.0f));

	D3D12_RANGE readRange;
	readRange.Begin = 0;
	readRange.End = 0;

	check(constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedConstantBuffer)));
	memcpy(mappedConstantBuffer, &cbVS, sizeof(cbVS));
	constantBuffer->Unmap(0, &readRange);

	SetupCommands();

	ID3D12CommandList* ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	swapchain->Present(1, 0);

	const UINT64 currentFenceValue = fenceValue;
	check(commandQueue->Signal(fence, currentFenceValue));
	fenceValue++;

	if (fence->GetCompletedValue() < currentFenceValue)
	{
		check(fence->SetEventOnCompletion(currentFenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	frameIndex = swapchain->GetCurrentBackBufferIndex();
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
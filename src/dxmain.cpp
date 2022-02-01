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

#include <vector>

using namespace Microsoft::WRL;

// Globals
float width = 1280.0f;
float height = 720.0f;
float aspectRatio = width / height;

constexpr int backBufferCount = 2;
int frameIndex;

#define check(call) \
		{ HRESULT result_ = call; \
		assert(result_ == S_OK); } 


struct Vertex
{
	glm::vec3 position;
	glm::vec4 color;
};

// NOTE: Use ComPtr
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
ID3D12RootSignature* rootSignature;

ID3D12Resource* vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

CD3DX12_VIEWPORT viewport(0.0f, 0.0f, width, height);
CD3DX12_RECT scissor(0, 0, width, height);

void WaitForPreviousFrame()
{
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

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
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
	swapChainDesc.BufferCount = backBufferCount;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	IDXGISwapChain1* tempSwapChain;
	check(factory6->CreateSwapChainForHwnd(commandQueue, glfwGetWin32Window(window), &swapChainDesc, nullptr, nullptr, &tempSwapChain));
	tempSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain));

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDescriptorHeapDesc.NumDescriptors = backBufferCount;

		check(device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));

		rtvDescriptorHeapSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		for (uint32_t i = 0; i < backBufferCount; i++)
		{
			check(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTarget[i])));
			device->CreateRenderTargetView(renderTarget[i], nullptr, rtvHandle);
			rtvHandle.Offset(1, rtvDescriptorHeapSize);
		}
	}

	// Create command allocator
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	//Load assets
	// Create empty root signature
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;

		check(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		check(device->CreateRootSignature(0, 
										  signature->GetBufferPointer(),
										  signature->GetBufferSize(),
										  IID_PPV_ARGS(&rootSignature)));
	}

	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		std::wstring shaderPath = L"src/shaders/triangle_dx12.hlsl";

		// NOTE: Use dxc
		check(D3DCompileFromFile(shaderPath.c_str(), 
								 nullptr, nullptr, 
								 "VSMain", "vs_5_0",
								 compileFlags, 
								 0,
								 &vertexShader, 
								 nullptr));

		check(D3DCompileFromFile(shaderPath.c_str(), 
								 nullptr, nullptr, 
								 "PSMain", "ps_5_0",
								 compileFlags, 
								 0, 
								 &pixelShader, 
								 nullptr));

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO)
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = rootSignature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = false;
		psoDesc.DepthStencilState.StencilEnable = false;
		psoDesc.InputLayout.NumElements = _countof(inputElementDescs);
		psoDesc.InputLayout.pInputElementDescs = inputElementDescs;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc = {1, 0};

		check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
	}

	// Create command list
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, pipelineState, IID_PPV_ARGS(&commandList));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	check(commandList->Close());

	// Create vertex buffer
	{
		Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ { 0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		// NOTE: check on d3d12ma
		device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
										D3D12_HEAP_FLAG_NONE,
										&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
										D3D12_RESOURCE_STATE_GENERIC_READ,
										nullptr,
										IID_PPV_ARGS(&vertexBuffer));

		// Copy the triangle data to the vertex buffer
		uint8_t* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
		check(vertexBuffer->Map(0, &readRange, (void**)&pVertexDataBegin));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view
		vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.SizeInBytes = vertexBufferSize;
		vertexBufferView.StrideInBytes = sizeof(Vertex);
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU
	{
		device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		fenceValue = 1;

		// Create an event handle to use for frame synchronization
		fenceEvent = CreateEvent(nullptr, false, false, nullptr);
		if (fenceEvent == nullptr)
			check(HRESULT_FROM_WIN32(GetLastError()));

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame();
	}
}

void Render(GLFWwindow* window)
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	check(commandAllocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	check(commandList->Reset(commandAllocator, pipelineState));

	// Set necessary state
	commandList->SetGraphicsRootSignature(rootSignature);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		renderTarget[frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorHeapSize);
	commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->DrawInstanced(3, 1, 0, 0);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		renderTarget[frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));

	commandList->Close();

	ID3D12CommandList* ppCommandList[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

	swapChain->Present(1, 0);

	WaitForPreviousFrame();
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
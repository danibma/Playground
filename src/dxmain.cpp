#if 1

#include <stdio.h>

#include <windows.h>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include <vector>


// Globals
float width = 1280.0f;
float height = 720.0f;
float aspectRatio = width / height;

int frameIndex;

int frameCount = 2;

// D3D12
IDXGIFactory4* factory;
ID3D12Device* device;
ID3D12CommandQueue* commandQueue;
IDXGISwapChain3* swapChain;
ID3D12DescriptorHeap* rtvHeap;
unsigned int rtvDescriptorSize;
std::vector<ID3D12Resource*> renderTargets(2);
std::vector<ID3D12CommandAllocator*> commandAllocators(2);
ID3D12RootSignature* rootSignature;
ID3D12PipelineState* pipelineState;
/*
	Submitting work to command lists doesn’t start any work on the GPU
	Calls to ExecuteCommadList() finally do start work on the GPU
*/
ID3D12GraphicsCommandList* commandList;
ID3D12Resource* vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
ID3D12Fence* fence;
int fenceValues[2];
HANDLE fenceEvent;

D3D12_VIEWPORT viewport = { 0, 0, width, height };
D3D12_RECT scissor = { 0, 0, (LONG)width, (LONG)height };

#define FAILED(hr)      (((HRESULT)(hr)) < 0)

typedef DirectX::XMFLOAT3 float3;
typedef DirectX::XMFLOAT4 float4;

struct Vertex
{
	float3 position;
	float4 color;
};

inline void Check(HRESULT hr)
{
	assert(!FAILED(hr));
}

void WaitForGPU()
{
	// Schedule a Signal in the queue
	Check(commandQueue->Signal(fence, fenceValues[frameIndex]));

	// Wait until the fence has been processed
	Check(fence->SetEventOnCompletion(fenceValues[frameIndex], fenceEvent));
	WaitForSingleObjectEx(fenceEvent, INFINITE, false);

	// Increment the fence value for the current frame
	fenceValues[frameIndex]++;
}

void Init(HWND hwnd)
{

	// Load the rendering pipeline
	unsigned int factoryFlags = 0;
#if defined(_DEBUG)
	// Enable debug layer
	// NOTE: Enabling the debug layer after device creation will invalidate the active device
	{
		ID3D12Debug* debugController;
		if (!FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Aditional debug layers
			factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));
	// Warp device is a 3d software emulator

	IDXGIAdapter1* adapter;
	for (int adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); adapterIndex++)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Dont want the basic render driver adapter
			continue;
		}

		// Check to see if the adapter supports DX12, but don't create the device
		if (!FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}

		/* Could probably do this like this
		* DXGI_ADAPTER_DESC1 desc;
		* adapter->GetDesc1(&desc);
		  if(desc.Flags & DXGI_ADAPTER_FLAG_NONE)
		  {
			    if (!FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
				{
					break;
				}
		  }

		  because its checking if the adapter is not special
		**/
	}

	// Now create the actual device
	Check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

	// Create the command queue
	// Could do this like this "D3D12_COMMAND_QUEUE_DESC queueDesc = {};"
	// because the enums are both 0
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	// Create the swap chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = (unsigned int)width; 
	swapChainDesc.Height = (unsigned int)height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1; // Multisampling
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = frameCount; // Double buffer
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	// NOTE: Probably need to destroy this so it doesnt count to the ref count
	IDXGISwapChain1* swapChain1 = nullptr;
	Check(factory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1));

	// Disable Fullscreen by alt-enter
	Check(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

	// Here is the explanation why we need to query it to a IDXGISwapChain3
	/* you can get a IDXGISwapChain1, but then, you may or may not have a IDXGISwapChain3 available...
	 * that depends on what version the OS is, the driver, etc.
	 * so you query it to see if it can "cast" the IDXGISwapChain1 to IDXGISwapChain3, and that's the result it gives you
	 * but you need to release it later, because it does add a ref count to that object.
	*/

	swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain));

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = frameCount; // 2 descriptors for Double buffer
		Check(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));

		rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV and a command allocator for each frame/buffer
		for (int i = 0; i < frameCount; i++)
		{
			Check(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])));
			device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
			rtvHandle.Offset(1, rtvDescriptorSize);

			Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		}
	}


	// Load Assets(Shaders, etc.)

	// Create an empty root signature
	// A root signature is used to share data between the shaders and the cpu
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ID3DBlob* signature;
		ID3DBlob* error;

		Check(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
	}

	// Create the pipeline state, which includes compiling and loading shaders
	{
		ID3DBlob* VS;
		ID3DBlob* PS;
#if defined(_DEBUG)
		unsigned int compilerFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		unsigned int compilerFlags = 0;
#endif

		compilerFlags |= D3DCOMPILE_ALL_RESOURCES_BOUND;

		ID3DBlob* errorMsgs;
		Check(D3DCompileFromFile(L"src/shaders/triangle.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compilerFlags, 0, &VS, &errorMsgs));
		if (errorMsgs)
		{
			OutputDebugStringA((char*)errorMsgs->GetBufferPointer());
			ZeroMemory(errorMsgs, sizeof(errorMsgs));
		}

		Check(D3DCompileFromFile(L"src/shaders/triangle.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compilerFlags, 0, &PS, &errorMsgs));
		if (errorMsgs)
		{
			OutputDebugStringA((char*)errorMsgs->GetBufferPointer());
			ZeroMemory(errorMsgs, sizeof(errorMsgs));
		}

		// Define the vertex Input Layout
		/*
			typedef struct D3D12_INPUT_ELEMENT_DESC {
			  LPCSTR                     SemanticName;
			  UINT                       SemanticIndex;
			  DXGI_FORMAT                Format;
			  UINT                       InputSlot;
			  UINT                       AlignedByteOffset;
			  D3D12_INPUT_CLASSIFICATION InputSlotClass;
			  UINT                       InstanceDataStepRate;
			} D3D12_INPUT_ELEMENT_DESC;
		*/
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = {};
		pipelineDesc.pRootSignature = rootSignature;
		pipelineDesc.VS = CD3DX12_SHADER_BYTECODE(VS);
		pipelineDesc.PS = CD3DX12_SHADER_BYTECODE(PS);
		pipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pipelineDesc.SampleMask = UINT_MAX;
		pipelineDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pipelineDesc.DepthStencilState.DepthEnable = false;
		pipelineDesc.DepthStencilState.StencilEnable = false;
		pipelineDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineDesc.NumRenderTargets = 1;
		pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipelineDesc.SampleDesc.Count = 1;
		Check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState)));
	}

	Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[frameIndex], pipelineState, IID_PPV_ARGS(&commandList)));

	// Commands lists are created in the record state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	commandList->Close();

	// Create a vertex buffer
	{
		Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ { 0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
		};

		const unsigned int vertexBufferSize = sizeof(triangleVertices);

		// Note: using upload heaps to transfer static data like vert buffers is not
		// recommend. Every time the GPU needs it, the upload heap will be marshalled
		// over. Please read up on Default heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		// TODO: This ^

		Check(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBuffer)));

		// Copy the triangle data to the vertex buffer
		unsigned char* vertexDataBegin;
		CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
		Check(vertexBuffer->Map(0, &readRange, (void**)&vertexDataBegin));
		memcpy(vertexDataBegin, triangleVertices, sizeof(triangleVertices));
		vertexBuffer->Unmap(0, nullptr);

		// Initialize Vertex Buffer View
		vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.StrideInBytes = sizeof(Vertex);
		vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Create Syncronization objects and wait until assets have been uploaded to the GPU
	{
		Check(device->CreateFence(fenceValues[frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		fenceValues[frameIndex]++;

		// Create an event handle to use for frame syncronization
		fenceEvent = CreateEvent(nullptr, false, false, nullptr);
		if (fenceEvent)
		{
			Check(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute, we are resuing the same
		// command list in our main loop, but for now, we just want to wait for setup to
		// complete before continuing
		WaitForGPU();
	}
}

void PopulateCommandList()
{
	// Command list allocators can only be reseted when the associated
	// commands lists have finished execution on the GPU, apps sound use
	// fences to determine GPU execution progress.

	/*
		Call Allocator::Reset before reusing it in another frame
			Otherwise the allocator will keep on growing until you’ll run out of memory
	*/
	Check(commandAllocators[frameIndex]->Reset());

	// However, when ExecuteCommandsList() is called on a particular command
	// list, that command list can then be reseted at any time and must be before
	// re-recording
	Check(commandList->Reset(commandAllocators[frameIndex], pipelineState));

	// Set necessary state.
	commandList->SetGraphicsRootSignature(rootSignature);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	// Indicate that the back buffer will be used as a render target
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
										renderTargets[frameIndex], 
										D3D12_RESOURCE_STATE_PRESENT, 
										D3D12_RESOURCE_STATE_RENDER_TARGET));


	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
	commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	// Record Commands
	float clearColor[] = { 0.0f, 0.5f, 0.8f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->DrawInstanced(3, 1, 0, 0);

	// Indicate that the back buffer will now be used to present
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
											renderTargets[frameIndex],
											D3D12_RESOURCE_STATE_RENDER_TARGET, 
											D3D12_RESOURCE_STATE_PRESENT));

	Check(commandList->Close());
}

void MoveToNextFrame()
{
	// Schedule a Signal command in the queue
	const unsigned long long currentFenceValue = fenceValues[frameIndex];
	Check(commandQueue->Signal(fence, currentFenceValue));

	// Update the frame index
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready
	if (fence->GetCompletedValue() < fenceValues[frameIndex])
	{
		Check(fence->SetEventOnCompletion(fenceValues[frameIndex], fenceEvent));
		WaitForSingleObjectEx(fenceEvent, INFINITE, false);
	}

	// Set the fence value for the next frame
	fenceValues[frameIndex] = (int)currentFenceValue + 1;
}

void Render() 
{
	// Record all the commands we need to render the scene into the command list
	PopulateCommandList();

	// Execute the command list
	ID3D12CommandList* commandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	// Present the frame
	swapChain->Present(1, 0); // I think this has something to do with v-sync too

	MoveToNextFrame();
}

void Destroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGPU();

	CloseHandle(fenceEvent);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		Render();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, (LONG)width, (LONG)height };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	HWND hwnd = CreateWindow(
		windowClass.lpszClassName,
		L"Skel Engine",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,        // We have no parent window.
		nullptr,        // We aren't using menus.
		hInstance,
		0);

	// Initialize the sample. OnInit is defined in each child-implementation of DXSample.
	Init(hwnd);

	ShowWindow(hwnd, nCmdShow);

	// Main sample loop.
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	Destroy();

	// Return this part of the WM_QUIT message to Windows.
	return static_cast<char>(msg.wParam);
}
#endif
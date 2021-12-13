#if 0

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")


using namespace DirectX;

static bool g_isRunning = true;

int WIDTH = 1600;
int HEIGHT = 900;

ID3D11Device* device;
ID3D11DeviceContext* deviceContext;
IDXGISwapChain1* swapChain;
ID3D11RenderTargetView* renderTarget;
ID3D11Texture2D* backBuffer;

XMMATRIX perspectiveMatrix;
XMMATRIX viewMat;
XMMATRIX modelMat;

HRESULT hr;

#define check(x) \
		if(FAILED(x)){ \
		std::stringstream ss; \
		ss << #x << "Failed! " << __FILE__ << " : " << __LINE__; \
		MessageBoxA(nullptr, ss.str().c_str(), "Error!", MB_OK); \
		}

#define SAFE_RELEASE(a) if((a)!=nullptr){(a)->Release();(a)=nullptr;}

// Input
enum GameAction {
	GameActionMoveCamFwd,
	GameActionMoveCamBack,
	GameActionMoveCamLeft,
	GameActionMoveCamRight,
	GameActionTurnCamLeft,
	GameActionTurnCamRight,
	GameActionLookUp,
	GameActionLookDown,
	GameActionRaiseCam,
	GameActionLowerCam,
	GameActionCount
};
static bool g_keyIsDown[GameActionCount] = {};

void Resize(int width, int height)
{
	//If it doesnt have swap chain its because its the window initialization
	if (swapChain && width > 0 && height > 0)
	{
		WIDTH = width;
		HEIGHT = height;

		SAFE_RELEASE(backBuffer);
		SAFE_RELEASE(renderTarget);
		hr = swapChain->ResizeBuffers(2, WIDTH, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
		check(hr);

		hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		check(hr);

		hr = device->CreateRenderTargetView(backBuffer, 0, &renderTarget);
		check(hr);

		perspectiveMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(84), WIDTH / HEIGHT, 0.1f, 1000.f);
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (wparam == SC_KEYMENU && (lparam >> 16) <= 0) return 0;
	switch (msg)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
	{
		bool isDown = (msg == WM_KEYDOWN);
		if (wparam == VK_ESCAPE)
			PostQuitMessage(0);
		if (wparam == 'W')
			g_keyIsDown[GameActionMoveCamFwd] = isDown;
		if (wparam == 'A')
			g_keyIsDown[GameActionMoveCamLeft] = isDown;
		if (wparam == 'S')
			g_keyIsDown[GameActionMoveCamBack] = isDown;
		if (wparam == 'D')
			g_keyIsDown[GameActionMoveCamRight] = isDown;
		if (wparam == VK_UP)
			g_keyIsDown[GameActionLookUp] = isDown;
		if (wparam == VK_LEFT)
			g_keyIsDown[GameActionTurnCamLeft] = isDown;
		if (wparam == VK_DOWN)
			g_keyIsDown[GameActionLookDown] = isDown;
		if (wparam == VK_RIGHT)
			g_keyIsDown[GameActionTurnCamRight] = isDown;
		if(wparam == VK_SPACE)
			g_keyIsDown[GameActionRaiseCam] = isDown;
		if (wparam == VK_SHIFT)
			g_keyIsDown[GameActionLowerCam] = isDown;
		break;
	}
	case WM_SIZE:
		Resize((int)LOWORD(lparam), (int)HIWORD(lparam));
		break;
	case WM_DESTROY:
	{
		g_isRunning = false;
		PostQuitMessage(0);
		break;
	}
		break;
	}

	return DefWindowProcW(hWnd, msg, wparam, lparam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
					_In_opt_ HINSTANCE hPrevInstance,
					_In_ LPWSTR    lpCmdLine,
					_In_ int       nShowCmd)
{
	//Create Window Class
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEXW);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	//wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_WICKEDENGINEGAME);
	wcex.lpszMenuName = L"";
	wcex.lpszClassName = L"winclass";
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

	if (!RegisterClassExW(&wcex))
	{
		MessageBoxA(nullptr, "Failed to register class", "Error!", MB_OK);
		return GetLastError();
	}

	//Create the window itself
	HWND hwnd = CreateWindow(L"winclass", L"DirectX 11",
							WS_OVERLAPPEDWINDOW,
							CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, nullptr, nullptr, hInstance,
							nullptr);

	if (!hwnd)
	{
		MessageBoxA(nullptr, "Window Creation failed", "Error!", MB_OK);
		return GetLastError();
	}

	ShowWindow(hwnd, nShowCmd);
	UpdateWindow(hwnd);

	//Create D3D11 Device and Context
	D3D_FEATURE_LEVEL featureLevels[] = 
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_11_1
	};

	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG) || defined(DEBUG)
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	D3D_FEATURE_LEVEL featureLevel;

	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		creationFlags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
		&device, &featureLevel, &deviceContext);
	check(hr);

	//Get Factory
	IDXGIDevice2* dxgiDevice;
	hr = device->QueryInterface(__uuidof(IDXGIDevice2), (void**)&dxgiDevice);
	check(hr);

	IDXGIAdapter* dxgiAdapter;
	hr = dxgiDevice->GetAdapter(&dxgiAdapter);
	check(hr);

	IDXGIFactory2* factory;
	hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);
	check(hr);

	//Create SwapChain
	DXGI_SWAP_CHAIN_DESC1 sd = { 0 };
	sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.Width = WIDTH;
	sd.Height = HEIGHT;
	sd.Stereo = false;
	sd.BufferCount = 2; //use double buffer to minimize latency
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.Flags = 0;
	sd.SampleDesc.Count = 1; //No multisampling
	sd.SampleDesc.Quality = 0;
	sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	sd.Scaling = DXGI_SCALING_STRETCH;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fd;
	fd.RefreshRate.Denominator = 60; //144 hz
	fd.RefreshRate.Numerator = 1;
	fd.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
	fd.Windowed = true;

	hr = factory->CreateSwapChainForHwnd(device, hwnd, &sd, &fd, nullptr, &swapChain);
	check(hr);

	// Ensure that DXGI does not queue more than one frame at a time. This both reduces latency
	// and ensure that the application will only render after each vsync, minimizing power consumpion
	hr = dxgiDevice->SetMaximumFrameLatency(1);

	SAFE_RELEASE(dxgiAdapter);
	SAFE_RELEASE(dxgiDevice);
	SAFE_RELEASE(factory);

	//Create framebuffer render target
	//We only use one backbuffer thats why use put 0
	hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer); 
	check(hr);

	hr = device->CreateRenderTargetView(backBuffer, 0, &renderTarget);
	check(hr);

	//Create Depth/Stencil state
	//NOTE: Dont really know the use of this buffer, maybe I'm just dumb
	ID3D11Texture2D* depthStencilBuffer;
	D3D11_TEXTURE2D_DESC ds;
	ds.Width = WIDTH;
	ds.Height = HEIGHT;
	ds.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	ds.MipLevels = 1;
	ds.ArraySize = 1;
	ds.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	ds.MiscFlags = 0;
	ds.CPUAccessFlags = 0;
	ds.SampleDesc.Count = 1;
	ds.SampleDesc.Quality = 0;
	ds.Usage = D3D11_USAGE_DEFAULT;
	device->CreateTexture2D(&ds, nullptr, &depthStencilBuffer);

	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV = {};
	descDSV.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Texture2D.MipSlice = 0;

	// Create the depth stencil view
	ID3D11DepthStencilView* depthStencilView;
	hr = device->CreateDepthStencilView(depthStencilBuffer, // Depth stencil texture
		&descDSV, // Depth stencil desc
		&depthStencilView);  // [out] Depth stencil view
	check(hr);

	//Create Vertex Shader
	ID3DBlob* shaderErrors;
	ID3DBlob* vsBlob;
	ID3D11VertexShader* vertexShader;

	hr = D3DCompileFromFile(L"src/shaders/shader_dx11.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsBlob, &shaderErrors);
	if (FAILED(hr))
	{
		const char* errorString = {};
		if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
		{
			errorString = "Could not find shader file";
		}
		else if (shaderErrors)
		{
			errorString = (const char*)shaderErrors->GetBufferPointer();
			SAFE_RELEASE(shaderErrors);
		}
		MessageBoxA(0, errorString, "Error!", MB_OK);
		return 0;
	}

	hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
	check(hr);


	//Create Pixel Shader
	ID3DBlob* psBlob;
	ID3D11PixelShader* pixelShader;

	hr = D3DCompileFromFile(L"src/shaders/shader_dx11.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlob, &shaderErrors);
	if (FAILED(hr))
	{
		const char* errorString = {};
		if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			errorString = "Could not find shader file";
		}
		else if (shaderErrors)
		{
			errorString = (const char*)shaderErrors->GetBufferPointer();
			shaderErrors->Release();
		}
		MessageBoxA(0, errorString, "Error!", MB_OK);
		return 0;
	}

	hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
	check(hr);

	SAFE_RELEASE(psBlob);
	SAFE_RELEASE(shaderErrors);

	//Create Input Layout
	ID3D11InputLayout* inputLayout;
	D3D11_INPUT_ELEMENT_DESC inputElements[] = 
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		//Use D3D11_APPEND_ALIGNED_ELEMENT when the current element is defined after the previous one
		// like { 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f } being the first two the position and the last 4 the color
		{"TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	hr = device->CreateInputLayout(inputElements, ARRAYSIZE(inputElements), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
	check(hr);
	SAFE_RELEASE(vsBlob);

	//Create Vertex Buffer
	ID3D11Buffer* vertexBuffer;
	UINT numVerts;
	UINT stride;
	UINT offset;

	/*float vert[] =
	{
		// x y z | r g b a
		-1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		 1.0f,  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
		-1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
		 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		 1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
	};*/

	float vert[] = {
		// x, y, z | u, v
		-1.0f,  1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		 1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f,  1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
		
		-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
		 1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		 1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

		-1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		-1.0f,  1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
		
		 1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
		 1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,

		-1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

		-1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
		 1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
	};

	stride = 9 * sizeof(float);
	numVerts = sizeof(vert) / stride;
	offset = 0;

	D3D11_BUFFER_DESC bd = { };
	bd.ByteWidth = sizeof(vert);
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vbData = { vert };

	hr = device->CreateBuffer(&bd, &vbData, &vertexBuffer);
	check(hr);

	//Create Sampler State
	D3D11_SAMPLER_DESC sp = {};
	sp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sp.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	sp.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	sp.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	sp.BorderColor[0] = 1.0f;
	sp.BorderColor[1] = 1.0f;
	sp.BorderColor[2] = 1.0f;
	sp.BorderColor[3] = 1.0f;
	sp.ComparisonFunc = D3D11_COMPARISON_NEVER;

	ID3D11SamplerState* samplerState;
	device->CreateSamplerState(&sp, &samplerState);

	//Load texture
	int twidth, theight, tchannels;
	unsigned char* textureBytes = stbi_load("assets/wall.jpg", &twidth, &theight, &tchannels, 4);
	if (!textureBytes)
	{
		MessageBoxA(nullptr, "Failed to load texture", "Error!", MB_OK);
		return 0;
	}

	int texBytesPerRow = 4 * twidth;

	//Load cube texture
	int tcwidth, tcheight, tcchannels;
	unsigned char* ctextureBytes = stbi_load("assets/texture.png", &tcwidth, &tcheight, &tcchannels, 4);
	if (!ctextureBytes)
	{
		MessageBoxA(nullptr, "Failed to load texture", "Error!", MB_OK);
		return 0;
	}

	int ctexBytesPerRow = 4 * tcwidth;

	//Create texture
	D3D11_TEXTURE2D_DESC td = {};
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	td.Width = twidth;
	td.Height = theight;
	td.ArraySize = 1;
	td.MipLevels = 1;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	td.SampleDesc.Count = 1;
	td.SampleDesc.Quality = 0;
	td.Usage = D3D11_USAGE_IMMUTABLE;

	D3D11_SUBRESOURCE_DATA textureData = {};
	textureData.pSysMem = textureBytes;
	textureData.SysMemPitch = texBytesPerRow;

	ID3D11Texture2D* texture;
	device->CreateTexture2D(&td, &textureData, &texture);

	ID3D11ShaderResourceView* textureView;
	device->CreateShaderResourceView(texture, nullptr, &textureView);

	free(textureBytes);

	//Create cube texture
	D3D11_TEXTURE2D_DESC ctd = {};
	ctd.ArraySize = 1;
	ctd.MipLevels = 1;
	ctd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	ctd.Width = tcwidth;
	ctd.Height = tcheight;
	ctd.Usage = D3D11_USAGE_IMMUTABLE;
	ctd.SampleDesc.Count = 1;
	ctd.SampleDesc.Quality = 0;
	ctd.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA tcd = {};
	tcd.pSysMem = ctextureBytes;
	tcd.SysMemPitch = ctexBytesPerRow;

	ID3D11Texture2D* cubeTexture;
	device->CreateTexture2D(&ctd, &tcd, &cubeTexture);

	ID3D11ShaderResourceView* cubeTextureView;
	device->CreateShaderResourceView(cubeTexture, nullptr, &cubeTextureView);

	//Create constant buffer
	struct Constants
	{
		XMMATRIX modelViewProj;
	};

	ID3D11Buffer* constantBuffer;
	D3D11_BUFFER_DESC cb = { };
	cb.ByteWidth = sizeof(Constants);
	cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cb.Usage = D3D11_USAGE_DYNAMIC;
	cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = device->CreateBuffer(&cb, nullptr, &constantBuffer);
	check(hr);

	//Create rasterizer state
	ID3D11RasterizerState* rasterizerState;
	D3D11_RASTERIZER_DESC rd = { };
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_FRONT;
	rd.FrontCounterClockwise = true;

	hr = device->CreateRasterizerState(&rd, &rasterizerState);
	check(hr);

	// Camera
	XMVECTOR cameraPos = { -5, 0, -5 };
	XMVECTOR cameraFront = { 0, 0, -1 };
	XMVECTOR cameraTarget = { 0, 0, 0 };
	XMVECTOR cameraUp = {0, 1, 0};

	perspectiveMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(84), (float)WIDTH / (float)HEIGHT, 0.1f, 500.0f);

	float dt = 0.0f;
	float lastFrame = 0.0f;

	LARGE_INTEGER li;
	if (!QueryPerformanceFrequency(&li))
	{
		MessageBoxA(nullptr, "QueryPerformanceFrequency failed!", "Error!", 0);
		return 0;
	}
		
	double lastTime = 0.0f;

	double PCFreq = double(li.QuadPart) / 1000.0;

	QueryPerformanceCounter(&li);
	long long CounterStart = li.QuadPart;

	UINT indices[] =
	{
		3,1,0,
		2,1,3,

		6,4,5,
		7,4,6,

		11,9,8,
		10,9,11,

		14,12,13,
		15,12,14,

		19,17,16,
		18,17,19,

		22,20,21,
		23,20,22
	};

	//Create Index Buffer
	D3D11_BUFFER_DESC ibd = { };
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.ByteWidth = sizeof(UINT) * 36;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;

	D3D11_SUBRESOURCE_DATA ibData;
	ibData.pSysMem = indices;

	ID3D11Buffer* ib;
	hr = device->CreateBuffer(&ibd, &ibData, &ib);
	check(hr);

	float yaw = 0.0f;
	float pitch = 0.0f;

	XMMATRIX model2;

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			LARGE_INTEGER li;
			QueryPerformanceCounter(&li);
			double totalTime = double(li.QuadPart - CounterStart) / PCFreq;
			double t;

			dt = float((totalTime - lastTime) / 1000.0f);
			lastTime = totalTime;

			t = totalTime / 1000.0;
			
			/// Update camera
			const float cameraSpeed = 5.0f * dt;
			const float cameraTurn = 70.0f * dt;
			if (g_keyIsDown[GameActionMoveCamFwd])
				cameraPos += cameraSpeed * cameraFront;
			if (g_keyIsDown[GameActionMoveCamBack])
				cameraPos -= cameraSpeed * cameraFront;
			if (g_keyIsDown[GameActionMoveCamLeft])
				cameraPos += XMVector3Normalize(XMVector3Cross(cameraFront, cameraUp)) * cameraSpeed;
			if (g_keyIsDown[GameActionMoveCamRight])
				cameraPos -= XMVector3Normalize(XMVector3Cross(cameraFront, cameraUp)) * cameraSpeed;

			if (g_keyIsDown[GameActionTurnCamLeft])
				yaw -= cameraTurn;
			if (g_keyIsDown[GameActionTurnCamRight])
				yaw += cameraTurn;
			if (g_keyIsDown[GameActionLookUp])
				pitch += cameraTurn;
			if (g_keyIsDown[GameActionLookDown])
				pitch -= cameraTurn;

			if (g_keyIsDown[GameActionRaiseCam])
			{
				XMFLOAT3 p;
				XMStoreFloat3(&p, cameraPos);
				p.y += cameraSpeed;
				cameraPos = XMLoadFloat3(&p);
			}

			if (g_keyIsDown[GameActionLowerCam])
			{
				XMFLOAT3 p;
				XMStoreFloat3(&p, cameraPos);
				p.y -= cameraSpeed;
				cameraPos = XMLoadFloat3(&p);
			}

			std::stringstream ss;

			if (pitch > 89.0f)
				pitch = 89.0f;
			else if (pitch < -89.0f)
				pitch = -89.0f;

			XMVECTOR front = XMVectorSet(sin(XMConvertToRadians(yaw)) * cos(XMConvertToRadians(pitch)),
				sin(XMConvertToRadians(pitch)),
				cos(XMConvertToRadians(yaw)) * cos(XMConvertToRadians(pitch)), 0.0f);
			cameraFront = XMVector3Normalize(front);

			// Calculate view matrix from camera data
			viewMat = XMMatrixLookAtLH(cameraPos, cameraPos + cameraFront, cameraUp);

			// Spin the quad
			modelMat = XMMatrixRotationY(t);

			// Copy model-view-projection matrix to uniform buffer
			XMMATRIX modelViewProj = XMMatrixTranspose(perspectiveMatrix) * XMMatrixTranspose(viewMat) * XMMatrixTranspose(modelMat);

			//Set and Update Constant Buffer
			D3D11_MAPPED_SUBRESOURCE mappedSubresource;
			deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);

			Constants* constants = (Constants*)mappedSubresource.pData;
			constants->modelViewProj = XMMatrixTranspose(modelViewProj);
			deviceContext->Unmap(constantBuffer, 0);

			//Render

			//Clear
			float bgColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
			deviceContext->ClearRenderTargetView(renderTarget, bgColor);
			deviceContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0.0f);

			//Set Viewport
			D3D11_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = WIDTH;
			viewport.Height = HEIGHT;
			viewport.MaxDepth = 1.0f;
			viewport.MinDepth = 0.0f;

			deviceContext->RSSetViewports(1, &viewport);

			//Set Rasterizer State
			deviceContext->RSSetState(rasterizerState);

			//Set Render Target
			deviceContext->OMSetRenderTargets(1, &renderTarget, depthStencilView);

			//Set Input Layout
			deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			deviceContext->IASetInputLayout(inputLayout);

			//Set Shaders
			deviceContext->VSSetShader(vertexShader, nullptr, 0);
			deviceContext->PSSetShader(pixelShader, nullptr, 0);

			//Set Shader Resource, "bind texture to shader"
			deviceContext->PSSetShaderResources(0, 1, &cubeTextureView);
			deviceContext->PSSetSamplers(0, 1, &samplerState);

			//Set Constant Buffer
			deviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);

			//Set Vertex Buffer
			deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

			//Set Index Buffer
			deviceContext->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);

			//Draw!
			deviceContext->DrawIndexed(36, 0, 0);

			//---------------------------Floor----------------------------------------

			modelMat = XMMatrixRotationX(XMConvertToRadians(90.0f)) * XMMatrixTranslation(0.0f, -30.0f, 0.0f) * XMMatrixScaling(15.0f, 0.05f, 15.0f);

			// Copy model-view-projection matrix to uniform buffer
			modelViewProj = XMMatrixTranspose(perspectiveMatrix) * XMMatrixTranspose(viewMat) * XMMatrixTranspose(modelMat);

			//Set and Update Constant Buffer
			deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);

			constants = (Constants*)mappedSubresource.pData;
			constants->modelViewProj = XMMatrixTranspose(modelViewProj);
			deviceContext->Unmap(constantBuffer, 0);

			//Set Shader Resource, "bind texture to shader"
			deviceContext->PSSetShaderResources(0, 1, &textureView);
			deviceContext->PSSetSamplers(0, 1, &samplerState);

			//Set Constant Buffer
			deviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);

			//Draw!
			deviceContext->DrawIndexed(36, 0, 0);

			swapChain->Present(1, 0);
		}

	}

	SAFE_RELEASE(texture);
	SAFE_RELEASE(textureView);
	SAFE_RELEASE(samplerState);
	SAFE_RELEASE(vertexBuffer);
	SAFE_RELEASE(inputLayout);
	SAFE_RELEASE(vertexShader);
	SAFE_RELEASE(pixelShader);
	SAFE_RELEASE(depthStencilBuffer);
	SAFE_RELEASE(swapChain);
	SAFE_RELEASE(renderTarget);
	SAFE_RELEASE(deviceContext);
	SAFE_RELEASE(device);

	return 0;
}
#endif
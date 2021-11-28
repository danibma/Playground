#if 1

#include <d3d11_1.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <sstream>
#include <iostream>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#define check(x) \
		if(FAILED(x)){ \
		std::stringstream ss; \
		ss << #x << "Failed! " << __FILE__ << " : " << __LINE__; \
		MessageBoxA(nullptr, ss.str().c_str(), "Error!", MB_OK); \
		}

#define release(a) if((a)!=nullptr){(a)->Release();(a)=nullptr;}

float WIDTH = 1600;
float HEIGHT = 900;

IDXGIFactory2* factory;
IDXGIAdapter* adapter;
ID3D11Device* device;
ID3D11DeviceContext* deviceContext;
ID3D11Debug* debugController;
IDXGISwapChain1* swapChain;
ID3D11RenderTargetView* renderTargetView;
ID3D11DepthStencilView* depthStencilView;
ID3D11Texture2D* backBuffer;
ID3D11Texture2D* depthStencilBuffer;
ID3D11Buffer* vertexBuffer;
ID3D11Buffer* indexBuffer;
ID3D11Buffer* constantBuffer;
ID3D11VertexShader* vertexShader;
ID3D11PixelShader* pixelShader;
ID3D11InputLayout* inputLayout;
ID3D11RasterizerState* rasterizerState;
ID3D11SamplerState* samplerState;
ID3D11Texture2D* texture;
ID3D11ShaderResourceView* textureView;

D3D11_MAPPED_SUBRESOURCE mappedResource;

uint32_t stride;
uint32_t offset;


struct constantBufferVS
{
	glm::mat4 MVPMatrix;
};
constantBufferVS cbVS;

glm::vec3 cameraPos = { 0, 0, -5 };
glm::vec3 cameraFront = { 0, 0, 1 };
glm::vec3 cameraTarget = { 0, 0, 0 };
glm::vec3 cameraUp = { 0, 1, 0 };

glm::mat4 modelMatrix(1.0f);
glm::mat4 viewMatrix(1.0f);
glm::mat4 projectionMatrix(1.0f);

float fov = 84.0f;
float deltaTime = 1.0f;
float lastFrame = 0.0f;

float lastX = WIDTH / 2;
float lastY = HEIGHT / 2;
float yaw = 0.0f;
float pitch = 0.0f;
float sensitivity = 0.2f;
float timer = 0;
double x = 0, y = 0;

bool firstTimeMouse = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (firstTimeMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstTimeMouse = false;
	}

	glfwGetCursorPos(window, &x, &y);
	float xoffset = static_cast<float>(x) - lastX;
	float yoffset = lastY - static_cast<float>(y);
	lastX = static_cast<float>(x);
	lastY = static_cast<float>(y);
	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	if (pitch > 89.f)
		pitch = 89.f;
	if (pitch < -89.f)
		pitch = -89.f;

	glm::vec3 front;
	front.x = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	cameraFront = glm::normalize(front);
}

void InitializeD3D11(HWND hwnd)
{
	IDXGIFactory* tempFactory;

	check(CreateDXGIFactory(IID_PPV_ARGS(&tempFactory)));

	check(tempFactory->EnumAdapters(0, &adapter));

	uint32_t creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_11_1
	};

	check(D3D11CreateDevice(
		adapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
		creationFlags,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION, 
		&device,
		nullptr,
		&deviceContext));

	// Enable debug controller
#if defined(_DEBUG)
	check(device->QueryInterface(IID_PPV_ARGS(&debugController)));
#endif

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.Width = WIDTH;
	swapChainDesc.Height = HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

	check(adapter->GetParent(IID_PPV_ARGS(&factory)));

	factory->CreateSwapChainForHwnd(device, hwnd, &swapChainDesc, 0, 0, &swapChain);

	check(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))); // Get Back Buffer

	check(device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView));

	D3D11_TEXTURE2D_DESC depthStencilBufferDesc = {};
	depthStencilBufferDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	depthStencilBufferDesc.Width = WIDTH;
	depthStencilBufferDesc.Height = HEIGHT;
	depthStencilBufferDesc.MipLevels = 1;
	depthStencilBufferDesc.SampleDesc.Count = 1;
	depthStencilBufferDesc.SampleDesc.Quality = 0;
	depthStencilBufferDesc.ArraySize = 1;
	depthStencilBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	device->CreateTexture2D(&depthStencilBufferDesc, nullptr, &depthStencilBuffer);

	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	depthStencilDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Texture2D.MipSlice = 0;

	check(device->CreateDepthStencilView(depthStencilBuffer, &depthStencilDesc, &depthStencilView));

	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 color;
	};

	float aspectRatio = WIDTH / HEIGHT;

	/*Vertex vertexData[3] = {
							{ { 0.0f, 0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f } },
							{ { 0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f } },
							{ { -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f } } };*/

	float vertexData[] = {
		// x, y, z | u, v | r, g, b
		-1.0f,  1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		 1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		 1.0f,  1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		-1.0f,  1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		 1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		 1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		-1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		-1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		-1.0f,  1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		-1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		 1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		 1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		 1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		 1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		-1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		 1.0f,  1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		-1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		-1.0f, -1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		 1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
		 1.0f,  1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
		-1.0f,  1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f,
	};

	stride = 8 * sizeof(float);
	offset = 0;

	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.ByteWidth = sizeof(vertexData);
	vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vertexBufferData = {};
	vertexBufferData.pSysMem = vertexData;

	check(device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &vertexBuffer));

	//uint32_t indexData[3] = { 0, 1, 2 };
	uint32_t indexData[36] = {
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

	D3D11_BUFFER_DESC indexBufferDesc = {};
	indexBufferDesc.ByteWidth = sizeof(uint32_t) * 36;
	indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA indexBufferData = {};
	indexBufferData.pSysMem = indexData;

	check(device->CreateBuffer(&indexBufferDesc, &indexBufferData, &indexBuffer));

	//Load cube texture
	int twidth, theight, tchannels;
	unsigned char* textureBytes = stbi_load("src/assets/wall.jpg", &twidth, &theight, &tchannels, 4);
	if (!textureBytes)
	{
		MessageBoxA(nullptr, "Failed to load texture", "Error!", MB_OK);
		return;
	}

	int texBytesPerRow = 4 * twidth;

	D3D11_SAMPLER_DESC samplerStateDesc = {};
	samplerStateDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerStateDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerStateDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerStateDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerStateDesc.BorderColor[0] = 1.0f;
	samplerStateDesc.BorderColor[1] = 1.0f;
	samplerStateDesc.BorderColor[2] = 1.0f;
	samplerStateDesc.BorderColor[3] = 1.0f;
	samplerStateDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	device->CreateSamplerState(&samplerStateDesc, &samplerState);

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

	device->CreateTexture2D(&td, &textureData, &texture);

	device->CreateShaderResourceView(texture, nullptr, &textureView);

	D3D11_BUFFER_DESC constantBufferDesc = {};
	constantBufferDesc.ByteWidth = sizeof(constantBufferVS);
	constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	check(device->CreateBuffer(&constantBufferDesc, nullptr, &constantBuffer));


#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif
	ID3DBlob* error;

	std::wstring shaderPath = L"src/shaders/simple_scene.hlsl";

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

	device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &vertexShader);

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

	device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &pixelShader);

	D3D11_INPUT_ELEMENT_DESC inputElements[3];
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].SemanticIndex = 0;
	inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElements[0].AlignedByteOffset = 0;
	inputElements[0].InputSlot = 0;
	inputElements[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	inputElements[0].InstanceDataStepRate = 0;

	inputElements[1].SemanticName = "TEX";
	inputElements[1].SemanticIndex = 0;
	inputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElements[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	inputElements[1].InputSlot = 0;
	inputElements[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	inputElements[1].InstanceDataStepRate = 0;

	inputElements[2].SemanticName = "COLOR";
	inputElements[2].SemanticIndex = 0;
	inputElements[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElements[2].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	inputElements[2].InputSlot = 0;
	inputElements[2].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	inputElements[2].InstanceDataStepRate = 0;

	device->CreateInputLayout(inputElements, ARRAYSIZE(inputElements), vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &inputLayout);

	viewMatrix = glm::lookAtLH(cameraPos, cameraPos + cameraFront, cameraUp);
	projectionMatrix = glm::perspectiveFovLH_ZO(glm::radians(fov), WIDTH, HEIGHT, 0.1f, 10000.0f);;
	cbVS.MVPMatrix = projectionMatrix * viewMatrix * modelMatrix;
	cbVS.MVPMatrix = glm::transpose(cbVS.MVPMatrix);

	deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	memcpy(mappedResource.pData, &cbVS, sizeof(cbVS));
	deviceContext->Unmap(constantBuffer, 0);

	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = true;
	device->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
}

void Render(GLFWwindow* window)
{
	/// Update camera
	float cameraSpeed = 10.0f * deltaTime;
	float currentFrame = static_cast<float>(glfwGetTime());
	deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;

	std::cout << deltaTime * 1000.0f << std::endl;

	if (glfwGetKey(window, GLFW_KEY_W))
		cameraPos += cameraSpeed * cameraFront;
	if (glfwGetKey(window, GLFW_KEY_S))
		cameraPos -= cameraSpeed * cameraFront;
	if (glfwGetKey(window, GLFW_KEY_D))
		cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_A))
		cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_SPACE))
		cameraPos.y += cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT))
		cameraPos.y -= cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_ESCAPE))
		glfwSetWindowShouldClose(window, true);

	glfwSetCursorPosCallback(window, mouse_callback);

	float color[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
	deviceContext->ClearRenderTargetView(renderTargetView, color);

	deviceContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

	D3D11_VIEWPORT viewport = {};
	viewport.Width = WIDTH;
	viewport.Height = HEIGHT;
	viewport.MaxDepth = 1;
	viewport.MinDepth = 0;

	deviceContext->RSSetViewports(1, &viewport);

	deviceContext->RSSetState(rasterizerState);

	deviceContext->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

	deviceContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	deviceContext->IASetInputLayout(inputLayout);

	deviceContext->VSSetShader(vertexShader, nullptr, 0);
	deviceContext->PSSetShader(pixelShader, nullptr, 0);

	viewMatrix = glm::lookAtLH(cameraPos, cameraPos + cameraFront, cameraUp);
	projectionMatrix = glm::perspectiveFovLH_ZO(glm::radians(fov), WIDTH, HEIGHT, 0.1f, 10000.0f);
	cbVS.MVPMatrix = projectionMatrix * viewMatrix * modelMatrix;
	cbVS.MVPMatrix = glm::transpose(cbVS.MVPMatrix);

	deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	memcpy(mappedResource.pData, &cbVS, sizeof(cbVS));
	deviceContext->Unmap(constantBuffer, 0);

	deviceContext->PSSetSamplers(0, 1, &samplerState);
	deviceContext->PSSetShaderResources(0, 1, &textureView);

	deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

	deviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);

	deviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	deviceContext->DrawIndexed(36, 0, 0);

	swapChain->Present(1, 0);
}

int main() {
	int rc = glfwInit();
	assert(rc);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Directx 11", 0, 0);
	assert(window);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	InitializeD3D11(glfwGetWin32Window(window));

	while (!glfwWindowShouldClose(window)) 
	{
		glfwPollEvents();

		Render(window);
	}

	glfwDestroyWindow(window);
	return 0;
}

#endif

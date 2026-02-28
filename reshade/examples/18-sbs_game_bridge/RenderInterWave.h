#pragma once
#include <d3d11.h>
#include <wincodec.h> // For WIC
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <filesystem>
#include <iostream>
#include <dxgi1_2.h>  
#include <wrl/client.h>
#include "runtime_ETDX_Interface.h"
#define SWAPCHAIN_FORMAT   DXGI_FORMAT_R8G8B8A8_UNORM
//#define SWAPCHAIN_FORMAT   DXGI_FORMAT_B8G8R8A8_UNORM
//#define SWAPCHAIN_FORMAT   DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
//#define SWAPCHAIN_FORMAT   DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
using Microsoft::WRL::ComPtr;
using namespace DirectX;
class RenderInterWave {
public:
    RenderInterWave();
    ~RenderInterWave();

    bool Initialize(HWND hwnd);

    void Render();
    int outheight;
    int outwidth;
    int inheight;
    int inwidth;
    int m_3dscreenindex;
    int m_3dleft;
    int m_3dright;
    int m_3dtop;
    int m_3dbottom;
    int m_predictDelay;
private:
    bool LoadImageFromFile(IWICImagingFactory* pFactory, const wchar_t* filePath, IWICBitmapDecoder** ppDecoder, IWICBitmapFrameDecode** ppFrame);
    bool LoadTexture(const wchar_t* filePath);
    bool InitD3D(HWND hwnd);
    bool ConvertPixelFormat(IWICImagingFactory* pFactory, IWICBitmapFrameDecode* pFrame, IWICFormatConverter** ppConverter);
    bool CreateTextureFromImageData(UINT width, UINT height, BYTE* imageData);
    bool InitializeShadersAndInputLayout();
    bool CreateVertexBuffer();
    ID3D11Texture2D* backBuffer = nullptr;
    ComPtr<ID3D11Texture2D> readableTexture;

    ComPtr < ID3D11Device> m_device = nullptr;
    ComPtr < ID3D11DeviceContext> m_deviceContext = nullptr;
    ComPtr < IDXGISwapChain1> swapChain = nullptr;
    ComPtr < ID3D11RenderTargetView> renderTargetView = nullptr;
    ID3D11ShaderResourceView* textureSRV = nullptr;
    ComPtr < ID3D11ShaderResourceView> texturebackSRV = nullptr;
    ComPtr < ID3D11SamplerState> samplerState = nullptr;
    ComPtr < ID3D11InputLayout> inputLayout = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11ComputeShader* computeShader = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
};
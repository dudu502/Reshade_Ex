#include "RenderInterWave.h"
#include <comdef.h>
#include <string>
#include "Runtime_ETDX_Interface.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "dxgi.lib")
using namespace std;
RenderInterWave::RenderInterWave() {
    m_predictDelay = 65;
}
struct Vertex {
    float x, y, z;     // Position
    float u, v;        // Texture coordinates
};
RenderInterWave::~RenderInterWave() {
}
bool RenderInterWave::InitD3D(HWND hwnd) {

    D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDevice(nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &m_device, nullptr, &m_deviceContext);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to D3D11CreateDevice", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }
    IDXGIFactory2* factory = nullptr;
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
    if (SUCCEEDED(hr)) {
        ComPtr<IDXGIAdapter> adapter;
        UINT support;
        m_device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,&support);


        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = outwidth;  // 设置为窗口的实际宽度
        swapChainDesc.Height = outheight; // 设置为窗口的实际高度
        //swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        //swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.Format = SWAPCHAIN_FORMAT;
        //swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.Flags = 0;
        hr = factory->CreateSwapChainForHwnd(
            m_device.Get(),                  // pDevice: Direct3D 设备
            hwnd,                    // hWnd: 目标窗口句柄
            &swapChainDesc,          // pDesc: 描述结构体
            nullptr,                 // pFullscreenDesc: 全屏描述（可选）
            nullptr,                 // pRestrictToOutput: 输出限制（可选）
            &swapChain               // ppSwapChain: 返回交换链接口指针
        );
        if (FAILED(hr)) {
            MessageBox(NULL, L"Failed to CreateSwapChainForHwnd", L"Error", MB_OK | MB_ICONERROR);
            factory->Release();
            return false;
        }
        factory->Release();
    }
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to get buffer from swap chain", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC rtvdesc;
    //这里如果跟swapchain的srgb不一致，会变暗
    rtvdesc.Format = SWAPCHAIN_FORMAT;
    rtvdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; // 指定视图维度类型
    rtvdesc.Texture2D.MipSlice = 0; // 对于2D纹理，需要指定Mip片级别
    hr = m_device->CreateRenderTargetView(backBuffer, &rtvdesc, &renderTargetView);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create render target view", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }
    D3D11_TEXTURE2D_DESC texDesc;
    backBuffer->GetDesc(&texDesc);

    // 修改描述以适应新的纹理
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.Format = SWAPCHAIN_FORMAT;
    //texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    // 创建新纹理
    hr = m_device->CreateTexture2D(&texDesc, nullptr, &readableTexture);
    if (FAILED(hr)) {
        return false;
    }
    // 复制后台缓冲区到新纹理
    m_deviceContext->CopyResource(readableTexture.Get(), backBuffer);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = SWAPCHAIN_FORMAT; // 或者 _UNORM_SRGB 根据你的需求
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(readableTexture.Get(), &srvDesc, &texturebackSRV);
    if (FAILED(hr)) {
        return false;
    }
    m_deviceContext->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);
    return true;
}

bool RenderInterWave::Initialize(HWND hwnd) {

    if (!InitD3D(hwnd))
        return false;
    if (!LoadTexture(L"./DI/lrhalf.png"))
        return false;
    int res = InitializeShadersAndInputLayout();
    if (!CreateVertexBuffer())
        return false;
     res = BOE_ETDX_initSDK("./",  2);
    BOE_ETDX_initDx11(m_device.Get(), 
        0,0,outwidth,outheight, SWAPCHAIN_FORMAT,65);
    BOE_ETDX_setPupillaryDistance(70);
    int index, l, r, t, b;
    res = BOE_ETDX_get3DMonitorInfo(&index, &l, &r, &t, &b);

    return true;
}




void RenderInterWave::Render() {

    //无3D屏，显示2D
    //if (m_3dscreenindex < 0) {
    //    D3D11_VIEWPORT viewport = {};
    //    viewport.Width = static_cast<float>(outwidth);
    //    viewport.Height = static_cast<float>(outheight);
    //    viewport.MinDepth = 0.0f;
    //    viewport.MaxDepth = 1.0f;
    //    viewport.TopLeftX = static_cast<float>(0);
    //    viewport.TopLeftY = static_cast<float>(0);
    //    m_deviceContext->RSSetViewports(1, &viewport);
    //    // 使用计算结果作为像素着色器的输入
    //    m_deviceContext->PSSetShaderResources(0, 1, &textureSRV);
    //    UINT stride = sizeof(Vertex);
    //    UINT offset = 0;
    //    m_deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    //    m_deviceContext->IASetInputLayout(inputLayout.Get());
    //    m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //    m_deviceContext->VSSetShader(vertexShader, nullptr, 0);
    //    m_deviceContext->PSSetShader(pixelShader, nullptr, 0);
    //    m_deviceContext->PSSetSamplers(0, 1, &samplerState);
    //    m_deviceContext->Draw(6, 0);
    //}
    //有3D屏，显示3D
    //else {
        BOE_ETDX_doInterlaceDx11((void*)m_deviceContext.Get(), (void*)renderTargetView.Get(),
          (void**)&textureSRV, inwidth,
            //1080, DXGI_FORMAT_B8G8R8A8_UNORM, (DIInterMode)0);
        inheight, SWAPCHAIN_FORMAT, (DIInterMode)0,0);
    //}

    HRESULT hr;
    hr = swapChain->Present(1, 0);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to present the swap chain", L"Error", MB_OK | MB_ICONERROR);
    }
}

// 加载纹理
bool RenderInterWave::LoadTexture(const wchar_t* filePath) {
    IWICImagingFactory* pFactory = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICFormatConverter* pConverter = nullptr;
    BYTE* imageData = nullptr;
    // 初始化WIC工厂
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (LPVOID*)&pFactory);
    if (FAILED(hr)) {
        return false;
    }
    // 从文件加载图像
    if (!LoadImageFromFile(pFactory, filePath, &pDecoder, &pFrame)) {
        pFactory->Release();
        return false;
    }
    // 转换像素格式
    if (!ConvertPixelFormat(pFactory, pFrame, &pConverter)) {
        pDecoder->Release();
        pFactory->Release();
        return false;
    }
    // 获取图像尺寸
    UINT width, height;
    pConverter->GetSize(&width, &height);
    // 分配内存并复制图像数据
    imageData = (BYTE*)malloc(width * height * 4);
    if (!imageData) {
        pConverter->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }
    inheight = height;
    inwidth = width;
    WICRect rc = { 0, 0, (int)width, (int)height };
    pConverter->CopyPixels(&rc, width * 4, width * height * 4, imageData);
    // 创建纹理
    if (!CreateTextureFromImageData(width, height, imageData)) {
        free(imageData);
        pConverter->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }
    free(imageData);
    pConverter->Release();
    pDecoder->Release();
    pFactory->Release();
    return true;
}

// 从文件加载图像
bool RenderInterWave::LoadImageFromFile(IWICImagingFactory* pFactory, const wchar_t* filePath, IWICBitmapDecoder** ppDecoder, IWICBitmapFrameDecode** ppFrame) {
    HRESULT hr = pFactory->CreateDecoderFromFilename(filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, ppDecoder);
    if (FAILED(hr)) {
        return false;
    }

    hr = (*ppDecoder)->GetFrame(0, ppFrame);
    if (FAILED(hr)) {
        (*ppDecoder)->Release();
        return false;
    }

    return true;
}

// 转换像素格式
bool RenderInterWave::ConvertPixelFormat(IWICImagingFactory* pFactory, IWICBitmapFrameDecode* pFrame, IWICFormatConverter** ppConverter) {
    HRESULT hr = pFactory->CreateFormatConverter(ppConverter);
    if (FAILED(hr)) {
        return false;
    }
    hr = (*ppConverter)->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        (*ppConverter)->Release();
        return false;
    }
    return true;
}

// 从图像数据创建纹理
bool RenderInterWave::CreateTextureFromImageData(UINT width, UINT height, BYTE* imageData) {
    // 创建输入纹理（用于存储图像数据）
    D3D11_TEXTURE2D_DESC inputTexDesc = {};
    inputTexDesc.Width = width;
    inputTexDesc.Height = height;
    inputTexDesc.MipLevels = 1;
    inputTexDesc.ArraySize = 1;
    inputTexDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 输入图像格式
    inputTexDesc.SampleDesc.Count = 1;
    inputTexDesc.Usage = D3D11_USAGE_DEFAULT;
    inputTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA inputData = {};
    inputData.pSysMem = imageData;
    inputData.SysMemPitch = width * 4; // 每行的字节数

    ID3D11Texture2D* inputTexture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&inputTexDesc, &inputData, &inputTexture);
    if (FAILED(hr)) {
        return false;
    }
    // 创建 Shader Resource View (SRV) for input texture
    hr = m_device->CreateShaderResourceView(inputTexture, nullptr, &textureSRV);
    if (FAILED(hr)) {
        inputTexture->Release();
        return false;
    }

    return true;
}
bool RenderInterWave::InitializeShadersAndInputLayout()
{
    const char* vsSource =
        "struct VS_INPUT { float4 pos : POSITION; float2 tex : TEXCOORD0; }; \n"
        "struct VS_OUTPUT { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; }; \n"
        "VS_OUTPUT main(VS_INPUT input) {  return input; }";
    const char* psSource =
        "Texture2D shaderTexture : register(t0); \n"
        "SamplerState samplerState : register(s0); \n"
        "struct PS_INPUT { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; }; \n"
        "float4 main(PS_INPUT input) : SV_TARGET { return shaderTexture.Sample(samplerState, input.tex); }";
    // 编译顶点着色器
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(vsSource, strlen(vsSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }

    // 创建顶点着色器
    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
    if (FAILED(hr)) {
        vsBlob->Release();
        return false;
    }

    // 编译像素着色器
    ID3DBlob* psBlob = nullptr;
    hr = D3DCompile(psSource, strlen(psSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        vsBlob->Release();
        return false;
    }

    // 创建像素着色器
    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
    if (FAILED(hr)) {
        psBlob->Release();
        vsBlob->Release();
        return false;
    }

    if (FAILED(hr)) {
        return false;
    }


    // 创建输入布局
    D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    if (FAILED(hr)) {
        psBlob->Release();
        vsBlob->Release();
        return false;
    }

    // 释放编译后的着色器代码
    vsBlob->Release();
    psBlob->Release();

    // 创建采样器状态
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&sampDesc, &samplerState);
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

bool RenderInterWave::CreateVertexBuffer() {
    // 定义一个简单的矩形（两个三角形）
    Vertex vertices[] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f}
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    HRESULT hr = m_device->CreateBuffer(&bd, &initData, &vertexBuffer);
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

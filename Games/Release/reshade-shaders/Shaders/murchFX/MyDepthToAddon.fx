//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DepthToAddon.fx v1.2 made by murchalloo
// https://github.com/murchalloo/murchFX
// 
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ReShade.fxh"

#define EXPORT_NON_LINEARIZED 1

#define SRGB_CONVERTION 0

#define GAMMA_VALUE 2.2

namespace MyDepthToAddon
{
	//-------------------------------------
// 人眼瞳距（控制左右眼视差强度）
//-------------------------------------
    uniform float EyeIPD
    <
        ui_type  = "slider";
        ui_label = "Eye IPD (Stereo Strength)";
        ui_tooltip = "控制左右眼的视差强度，类似人眼瞳距；数值越大立体感越强";
        ui_min   = 0.0;
        ui_max   = 0.1;
        ui_step  = 0.001;
    > = 0.03;
    texture DepthBufferTex : DEPTH;
    sampler DepthBuffer
    {
        Texture = DepthBufferTex;
    };
	// 游戏原始颜色纹理（ReShade内置，绑定当前渲染的画面）
    texture ColorBufferTex : COLOR;
    sampler ColorBuffer
    {
        Texture = ColorBufferTex;
        AddressU = CLAMP;
        AddressV = CLAMP;
    };
    texture DepthToAddon_ColorTex
    {
        Width = BUFFER_WIDTH;
        Height = BUFFER_HEIGHT;
        Format = RGBA32F;
    }; //{ Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA32F; };
    texture DepthToAddon_DepthTex
    {
        Width = BUFFER_WIDTH;
        Height = BUFFER_HEIGHT;
        Format = RGBA8;
    };
    texture DepthToAddon_SBSTex
    {
        Width = BUFFER_WIDTH;
        Height = BUFFER_HEIGHT;
        Format = RGBA32F;
    }; //{ Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA32F; };

    float GetLinearizedDepth(float2 texcoord)
    {
        if (RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN) // RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
            texcoord.y = 1.0 - texcoord.y;

        texcoord.x /= RESHADE_DEPTH_INPUT_X_SCALE;
        texcoord.y /= RESHADE_DEPTH_INPUT_Y_SCALE;
        texcoord.x -= RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET * RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET;
        texcoord.y += RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET * BUFFER_RCP_HEIGHT;

        float depth = tex2Dfetch(DepthBuffer, float2(texcoord.x * BUFFER_WIDTH, texcoord.y * BUFFER_HEIGHT)).x;

        const float C = 0.01; //0.01
        if (RESHADE_DEPTH_INPUT_IS_LOGARITHMIC)
            depth = (exp(depth * log(C + 1.0)) - 1.0) / C;

        if (RESHADE_DEPTH_INPUT_IS_REVERSED)
            depth = 1.0 - depth;

        const float N = 1.0;
        depth /= RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - depth * (RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - N);
		    // 限制深度值范围（避免异常值）
        depth = saturate(depth);
        return depth;
    }

    void PS_DepthToAddon(in float4 position : SV_Position, in float2 texcoord : TEXCOORD, out float4 colorTex : SV_Target0, out float4 depthTex : SV_Target1, out float4 sbsTex : SV_Target2)
    {
        colorTex = tex2Dfetch(ColorBuffer, float2(texcoord.x * BUFFER_WIDTH, texcoord.y * BUFFER_HEIGHT));
        depthTex = GetLinearizedDepth(texcoord).xxx;
     
		
		    // 1. 判断当前输出像素属于屏幕左半还是右半
        bool isLeftHalf = (texcoord.x < 0.5);

    // 2. 将 [0,0.5]/[0.5,1] 映射回原始场景坐标 [0,1]
    //    这一步得到的是“中心视图”的 UV
        float2 centerUV;
        if (isLeftHalf)
            centerUV = float2(texcoord.x * 2.0, texcoord.y); // 左半屏 → [0,1]
        else
            centerUV = float2((texcoord.x - 0.5) * 2.0, texcoord.y); // 右半屏 → [0,1]

    // 3. 基于中心视图坐标采样深度和原始颜色
  
        float3 depthColor = depthTex.xxx; // 深度可视化颜色（灰度）
        float3 originalColor = tex2Dfetch(ColorBuffer, centerUV).rgb; // 原始画面颜色

    // 4. 根据深度 + 瞳距计算视差大小（越近视差越大）
    //    用反向纹理采样：针对目标像素位置，反推回源纹理中的采样位置
        float disparity = EyeIPD * (1.0 - depthColor.r); // 这里用 depthColor.r == depth

    // 5. 计算左右眼的采样坐标（反向纹理采样视差法的“反向”就体现在这里）
        float2 sampleUV = centerUV;
        if (isLeftHalf)
        {
        // 左眼：从中心视图向左偏移
            sampleUV.x -= disparity * 0.5;
        }
        else
        {
        // 右眼：从中心视图向右偏移
            sampleUV.x += disparity * 0.5;
        }

    // 6. 防止越界（反向采样可能把坐标推到屏幕外）
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0)
        {
        // 越界时退回到未偏移的原始颜色，避免黑边/拉伸
            sampleUV.x = clamp(sampleUV.x, 0.0, 1.0);
            sbsTex = float4(originalColor, 1.0);
            return;
        }

    // 7. 使用偏移后的 UV 从原始颜色纹理中反向采样得到立体视图颜色
        float3 stereoColor = tex2D(ColorBuffer, sampleUV).rgb;

    // 如果想临时检查深度图，可以改成：stereoColor = depthColor;
        sbsTex = float4(stereoColor, 1.0);
    }

    technique MyDepthToAddon
    {
        pass
        {
            VertexShader = PostProcessVS;
            PixelShader = PS_DepthToAddon;
            RenderTarget0 = DepthToAddon_ColorTex;
            RenderTarget1 = DepthToAddon_DepthTex;
            RenderTarget2 = DepthToAddon_SBSTex;
        }
    }
}

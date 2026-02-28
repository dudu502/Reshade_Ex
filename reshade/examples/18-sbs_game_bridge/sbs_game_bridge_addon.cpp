#include <imgui.h>
#include <reshade.hpp>
#include <vector>
#include <shared_mutex>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <Unknwn.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <fstream>
#include <string>
#include "runtime_ETDX_Interface.h"

using Microsoft::WRL::ComPtr;

using namespace reshade::api;

char* ptr_wName = "SBS Game Bridge";
char* ptr_wDesc = "SBS Game Bridge Desc.";

static constexpr const char* kEffectName = "MyDepthToAddon.fx";
static constexpr const char* kEffectNameWithoutExt = "MyDepthToAddon";
static constexpr const char* kExportTexName = "DepthToAddon_DepthTex";
static constexpr const char* kColorTexName = "DepthToAddon_ColorTex";
static constexpr const char* kSbsTexName = "DepthToAddon_SBSTex";
static effect_texture_variable g_depth_tex = { 0 }, g_color_tex = { 0 }, g_sbs_tex = { 0 };
static resource_view srv = { 0 }, srv_srgb = { 0 };

// ---- BOE ETDX interlace state ----
static bool g_etdx_sdk_inited = false;
static bool g_etdx_dx11_inited = false;
static int  g_predict_delay = 65;
static resource_view g_sbs_srv_cached = { 0 }, g_sbs_srv_srgb_cached = { 0 };

// ---- hotkey ----
static bool g_was_fx_feature_pressed;
static bool g_fx_enabled = true;
// 在文件静态变量区附近添加（与其他 static 变量并列）
static int g_etdx_out_format = -1; // 记录 BOE 已初始化时使用的输出格式（DXGI_FORMAT），-1 表示未初始化或未知

// 从 ReShade resource_view (DX11 SRV handle) 获取纹理尺寸和格式
static bool get_srv_info(resource_view view, uint32_t& width, uint32_t& height, DXGI_FORMAT& fmt)
{
	width = 0; height = 0; fmt = DXGI_FORMAT_UNKNOWN;
	if (view.handle == 0) return false;

	ID3D11ShaderResourceView* pSRV = reinterpret_cast<ID3D11ShaderResourceView*>(view.handle);
	ComPtr<ID3D11Resource> res;
	pSRV->GetResource(&res);
	if (!res) return false;

	ComPtr<ID3D11Texture2D> tex;
	if (FAILED(res.As(&tex)) || !tex) return false;

	D3D11_TEXTURE2D_DESC desc{};
	tex->GetDesc(&desc);
	width = desc.Width;
	height = desc.Height;
	fmt = desc.Format;
	return width > 0 && height > 0;
}

static void set_technique_enabled(effect_runtime* runtime, const char* technique_name, bool enabled)
{
	runtime->enumerate_techniques(nullptr,
		[technique_name, enabled](reshade::api::effect_runtime* rt,
			reshade::api::effect_technique technique)
		{
			char name[256] = {};
			rt->get_technique_name(technique, name);
			reshade::log::message(reshade::log::level::error, name);
			if (strcmp(name, technique_name) == 0)
			{
				rt->set_technique_state(technique, enabled);
			}
		});
}

static void on_reshade_present(reshade::api::effect_runtime* runtime) 
{
	// FX 开启关闭的键盘监听
	{
		// 1. 分别检测三个键的按下状态（核心：& 0x8000 只取“当前按下”的状态位）
		bool is_ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		bool is_shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
		bool is_k_down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;

		// 2. 判断组合键是否同时按下
		bool is_combination_down = is_ctrl_down && is_shift_down && is_k_down;

		// 3. 边缘检测：只有“当前按下且上一帧未按下”时，才触发一次
		if (is_combination_down && !g_was_fx_feature_pressed)
		{
			// 组合键触发时执行的逻辑
			g_fx_enabled = !g_fx_enabled;
			set_technique_enabled(runtime, kEffectNameWithoutExt, g_fx_enabled);
		}
		// 4. 更新上一帧状态，供下一帧判断
		g_was_fx_feature_pressed = is_combination_down;
	}
}

// 首次调用时初始化 BOE ETDX SDK + DX11，返回 true 表示已就绪
static bool ensure_etdx_ready(effect_runtime* runtime)
{
	if (!g_etdx_sdk_inited)
	{
		int ret = BOE_ETDX_initSDK("./", 2);
		if (ret == 0)
		{
			reshade::log::message(reshade::log::level::error, "[SBS] BOE_ETDX_initSDK failed");
			return false;
		}
		g_etdx_sdk_inited = true;
		reshade::log::message(reshade::log::level::warning, "[SBS] BOE_ETDX_initSDK OK");
	}

	if (!g_etdx_dx11_inited)
	{
		// 取游戏后台缓冲区尺寸作为输出 viewport
		uint32_t out_w = 0, out_h = 0;
		runtime->get_screenshot_width_and_height(&out_w, &out_h);
		if (out_w == 0 || out_h == 0) return false;

		// 从 runtime 获取原生 ID3D11Device*
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(runtime->get_device()->get_native());
		if (!device) return false;

		BOE_ETDX_initDx11(device,
			0, 0,
			static_cast<int>(out_w), static_cast<int>(out_h),
			DXGI_FORMAT_R8G8B8A8_UNORM,   // 输出 RTV 格式
			g_predict_delay);
		BOE_ETDX_setPupillaryDistance(65);
		g_etdx_dx11_inited = true;
		reshade::log::message(reshade::log::level::warning, "[SBS] BOE_ETDX_initDx11 OK");
	}

	return true;
}

static void draw_texture(effect_runtime* runtime, const char* texName, effect_texture_variable& texVar)
{
	if (texVar == 0)
	{
		texVar = runtime->find_texture_variable(kEffectName, texName);
	}
	if (texVar != 0)
	{
		runtime->get_texture_binding(texVar, &srv, &srv_srgb);
	}
	if (srv.handle != 0)
	{
		uint32_t w = 0, h = 0;
		runtime->get_screenshot_width_and_height(&w, &h);
		const float max_w = 512.0f;
		const float aspect = (h != 0) ? (static_cast<float>(w) / h) : 1.0f;
		ImGui::Image(srv.handle, ImVec2(max_w, max_w / aspect));
	}
	else
	{
		ImGui::TextUnformatted("Texture not ready (check effect compiled/enabled).");
	}
}

static void draw_sbs_overlay(effect_runtime* runtime)
{
	ImGui::BeginGroup();

	ImGui::Text("Preview Depth");
	draw_texture(runtime, kExportTexName, g_depth_tex);
	ImGui::NewLine();
	ImGui::Text("Preview Color");
	draw_texture(runtime, kColorTexName, g_color_tex);
	ImGui::NewLine();
	ImGui::Text("Preview SBS");
	draw_texture(runtime, kSbsTexName, g_sbs_tex);
	ImGui::NewLine();
	if (ImGui::Button("Call Wave"))
	{
		runtime->save_screenshot("sbs");
	}
	ImGui::EndGroup();
}


static void on_reshade_finish_effects(effect_runtime* runtime, command_list* cmd_list, resource_view rtv, resource_view rtv_srgb)
{
	if (!g_fx_enabled)
		return;

	// 确保 SDK 已就绪（仅 init SDK 部分，DX11 init 可后面根据实际 RTV 格式重建）
	if (!ensure_etdx_ready(runtime))
		return;

	// 查找并获取 SBS 输入 SRV（sRGB/linear 两种视图）
	if (g_sbs_tex == 0)
		g_sbs_tex = runtime->find_texture_variable(kEffectName, kSbsTexName);
	if (g_sbs_tex == 0)
		return;
	runtime->get_texture_binding(g_sbs_tex, &g_sbs_srv_cached, &g_sbs_srv_srgb_cached);
	if (g_sbs_srv_cached.handle == 0 && g_sbs_srv_srgb_cached.handle == 0)
		return;

	// 获取输入纹理信息
	uint32_t in_w = 0, in_h = 0;
	DXGI_FORMAT in_fmt = DXGI_FORMAT_UNKNOWN;
	resource_view chosen_srv_view = (g_sbs_srv_cached.handle != 0) ? g_sbs_srv_cached : g_sbs_srv_srgb_cached;
	if (!get_srv_info(chosen_srv_view, in_w, in_h, in_fmt))
		return;

	// 获取 D3D11 DeviceContext（ReShade 传入的 command_list 原生句柄）
	ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
	if (!ctx)
		return;

	// 选择输出 RTV（优先非 sRGB）
	resource_view target = (rtv.handle != 0) ? rtv : rtv_srgb;
	if (target.handle == 0)
		return;
	ID3D11RenderTargetView* dst_rtv = reinterpret_cast<ID3D11RenderTargetView*>(target.handle);

	// 从 dst_rtv 查询底层纹理与格式
	ComPtr<ID3D11Resource> dst_res;
	dst_rtv->GetResource(&dst_res);
	ComPtr<ID3D11Texture2D> dst_tex;
	D3D11_TEXTURE2D_DESC dstDesc{};
	if (FAILED(dst_res.As(&dst_tex)))
	{
		reshade::log::message(reshade::log::level::error, "[SBS] Failed to query DST RTV texture");
		return;
	}
	dst_tex->GetDesc(&dstDesc);

	// 如果 BOE 已初始化但输出格式不一致，则重建 BOE Dx11（先释放）
	if (g_etdx_dx11_inited && g_etdx_out_format != static_cast<int>(dstDesc.Format))
	{
		BOE_ETDX_releaseDx11();
		g_etdx_dx11_inited = false;
		g_etdx_out_format = -1;
	}

	// 如果 BOE Dx11 未初始化，使用 dstDesc.Format 初始化（确保格式匹配）
	if (!g_etdx_dx11_inited)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(runtime->get_device()->get_native());
		if (!device)
			return;
		int initRes = BOE_ETDX_initDx11(device,
			0, 0,
			static_cast<int>(dstDesc.Width), static_cast<int>(dstDesc.Height),
			static_cast<int>(dstDesc.Format),
			g_predict_delay);
		if (initRes == 0)
		{
			reshade::log::message(reshade::log::level::error, "[SBS] BOE_ETDX_initDx11 failed (on reinit)");
			return;
		}
		BOE_ETDX_setPupillaryDistance(70);
		g_etdx_dx11_inited = true;
		g_etdx_out_format = static_cast<int>(dstDesc.Format);
		reshade::log::message(reshade::log::level::warning, "[SBS] BOE_ETDX_initDx11 OK (reinit with RTV format)");
	}

	// 选择输入 SRV（根据格式优先选择 sRGB 视图）
	ID3D11ShaderResourceView* src_srv = nullptr;
	if ((in_fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || in_fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) && g_sbs_srv_srgb_cached.handle != 0)
		src_srv = reinterpret_cast<ID3D11ShaderResourceView*>(g_sbs_srv_srgb_cached.handle);
	else
		src_srv = reinterpret_cast<ID3D11ShaderResourceView*>(g_sbs_srv_cached.handle);
	if (!src_srv) return;

	// 检查 src 的底层资源是否与 dst 的底层资源相同（若相同，需要先复制到临时纹理再传给 BOE）
	ComPtr<ID3D11Resource> src_res;
	src_srv->GetResource(&src_res);
	bool need_temp_copy = (src_res && dst_res && src_res.Get() == dst_res.Get());

	ID3D11ShaderResourceView* srv_for_boe = src_srv; // 默认直接使用
	ComPtr<ID3D11Texture2D> temp_tex;
	ComPtr<ID3D11ShaderResourceView> temp_srv;

	if (need_temp_copy)
	{
		// 从 src_res 获取纹理描述
		ComPtr<ID3D11Texture2D> src_tex;
		if (FAILED(src_res.As(&src_tex)))
		{
			reshade::log::message(reshade::log::level::error, "[SBS] Failed to cast src resource to texture2D");
			return;
		}
		D3D11_TEXTURE2D_DESC srcDesc{};
		src_tex->GetDesc(&srcDesc);

		// 创建一个暂存纹理（与 src 大小和格式相同，可作为 SRV）
		srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		srcDesc.CPUAccessFlags = 0;
		srcDesc.Usage = D3D11_USAGE_DEFAULT;
		srcDesc.MiscFlags = 0;

		ID3D11Device* device = nullptr;
		ctx->GetDevice(&device);
		if (!device) return;

		if (FAILED(device->CreateTexture2D(&srcDesc, nullptr, &temp_tex)))
		{
			reshade::log::message(reshade::log::level::error, "[SBS] Failed to create temporary texture for SRV copy");
			device->Release();
			return;
		}

		// 复制数据到临时纹理
		ctx->CopyResource(temp_tex.Get(), src_tex.Get());

		// 创建 SRV 对临时纹理
		D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
		srvd.Format = srcDesc.Format;
		srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvd.Texture2D.MostDetailedMip = 0;
		srvd.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(temp_tex.Get(), &srvd, &temp_srv)))
		{
			reshade::log::message(reshade::log::level::error, "[SBS] Failed to create SRV for temporary texture");
			temp_tex.Reset();
			device->Release();
			return;
		}
		device->Release();

		srv_for_boe = temp_srv.Get();
		reshade::log::message(reshade::log::level::warning, "[SBS] Source equals destination - copied to temp SRV before interlace");
	}

	// 保存并临时解绑 PS 阶段的 SRV（避免资源同时绑定导致冲突）
	const UINT kSaveSlots = 16;
	ID3D11ShaderResourceView* savedSRVs[kSaveSlots] = {};
	ctx->PSGetShaderResources(0, kSaveSlots, savedSRVs);
	ID3D11ShaderResourceView* nullSRVs[kSaveSlots] = {};
	ctx->PSSetShaderResources(0, kSaveSlots, nullSRVs);

	// 保存并设置 OM / Viewport（保证 BOE 的绘制目标和 viewport 正确）
	ID3D11RenderTargetView* prevRTV = nullptr;
	ID3D11DepthStencilView* prevDSV = nullptr;
	ctx->OMGetRenderTargets(1, &prevRTV, &prevDSV);

	UINT numPrevVP = 16;
	D3D11_VIEWPORT prevVP[16];
	ctx->RSGetViewports(&numPrevVP, prevVP);

	D3D11_VIEWPORT vp{};
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = static_cast<FLOAT>(dstDesc.Width);
	vp.Height = static_cast<FLOAT>(dstDesc.Height);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	ctx->OMSetRenderTargets(1, &dst_rtv, nullptr);

	// 调用 BOE 交织函数（不要在这里调用 Present）
	int sRGBFix = 0;
	if (in_fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || in_fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
		sRGBFix = 1;
	int result = BOE_ETDX_doInterlaceDx11(
		(void*)ctx,
		(void*)dst_rtv,
		(void**)&srv_for_boe,
		static_cast<int>(in_w),
		static_cast<int>(in_h),
		static_cast<int>(in_fmt),
		(DIInterMode)0,
		sRGBFix);

	// 恢复 OM / Viewport
	ctx->OMSetRenderTargets(1, &prevRTV, prevDSV);
	if (prevRTV) { prevRTV->Release(); }
	if (prevDSV) { prevDSV->Release(); }
	ctx->RSSetViewports(numPrevVP, prevVP);

	// 恢复 PS SRV
	ctx->PSSetShaderResources(0, kSaveSlots, savedSRVs);
	for (UINT i = 0; i < kSaveSlots; ++i)
		if (savedSRVs[i]) savedSRVs[i]->Release();

	// 释放临时 SRV/纹理（如果创建了）
	temp_srv.Reset();
	temp_tex.Reset();

	/*
	// 日志返回结果
	char log_buf[256];
	sprintf(log_buf, "[SBS] doInterlace result=%d in:%ux%u in_fmt=%d out_fmt=%d sRGBFix=%d need_copy=%d",
		result, in_w, in_h, in_fmt, static_cast<int>(dstDesc.Format), sRGBFix, need_temp_copy ? 1 : 0);
	reshade::log::message(reshade::log::level::warning, log_buf);
	*/
}

static void on_reshade_reloaded_effects(effect_runtime* runtime)
{
	// 重载后重新查找纹理变量
	g_depth_tex = { 0 };
	g_color_tex = { 0 };
	g_sbs_tex = { 0 };
	srv = { 0 };
	srv_srgb = { 0 };
	g_sbs_srv_cached = { 0 };
	g_sbs_srv_srgb_cached = { 0 };
}

void register_addon_sbs_game_bridge() {
	reshade::log::message(reshade::log::level::warning, "[SBS] Register Addon _sbs_game_bridge");
	reshade::register_overlay(ptr_wName, draw_sbs_overlay);
	reshade::register_event<reshade::addon_event::reshade_finish_effects>(on_reshade_finish_effects);
	reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
	reshade::register_event < reshade::addon_event::reshade_present>(on_reshade_present);
}
void unregister_addon_sbs_game_bridge() {
	reshade::unregister_overlay(ptr_wName, draw_sbs_overlay);
	reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(on_reshade_finish_effects);
	reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
	reshade::unregister_event < reshade::addon_event::reshade_present>(on_reshade_present);
}

#ifndef BUILTIN_ADDON

extern "C" __declspec(dllexport) const char* NAME = ptr_wName;
extern "C" __declspec(dllexport) const char* DESCRIPTION = ptr_wDesc;
BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:
			if (!reshade::register_addon(hModule))
				return FALSE;
			register_addon_sbs_game_bridge();
			break;
		case DLL_PROCESS_DETACH:
			unregister_addon_sbs_game_bridge();
			// 释放 BOE ETDX 资源
			if (g_etdx_dx11_inited) { BOE_ETDX_releaseDx11(); g_etdx_dx11_inited = false; }
			if (g_etdx_sdk_inited) { BOE_ETDX_releaseSDK();  g_etdx_sdk_inited = false; }
			reshade::unregister_addon(hModule);
			break;
	}

	return TRUE;
}

#endif

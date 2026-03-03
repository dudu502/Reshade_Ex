#include <imgui.h>
#include <reshade.hpp>
#include <vector>
#include <shared_mutex>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <Unknwn.h>
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <chrono>
#include <fstream>
#include <string>
#include "runtime_ETDX_Interface.h"

using Microsoft::WRL::ComPtr;
using namespace reshade::api;

// Window title/description exported to ReShade
static char* ptr_wName = "SBS Game Bridge";
static char* ptr_wDesc = "SBS Game Bridge Desc.";

// Effect & texture names
static constexpr const char* kEffectName = "MyDepthToAddon.fx";
static constexpr const char* kEffectNameWithoutExt = "MyDepthToAddon";
static constexpr const char* kExportTexName = "DepthToAddon_DepthTex";
static constexpr const char* kColorTexName = "DepthToAddon_ColorTex";
static constexpr const char* kSbsTexName = "DepthToAddon_SBSTex";

// Effect texture variables & views (shared for DX11/DX12)
static effect_texture_variable g_depth_tex = { 0 }, g_color_tex = { 0 }, g_sbs_tex = { 0 };
static resource_view srv = { 0 }, srv_srgb = { 0 };
static resource_view g_sbs_srv_cached = { 0 }, g_sbs_srv_srgb_cached = { 0 };

// BOE / SDK state
static bool g_etdx_sdk_inited = false;
static bool g_etdx_dx11_inited = false;
static bool g_etdx_dx12_inited = false;
static int  g_predict_delay = 65;
static int  g_etdx_out_format = -1; // DXGI_FORMAT used when initializing DX11 path

// API detection (start assuming D3D11 until a D3D12 device is seen)
static device_api g_cur_game_api = device_api::d3d11;

// DX12: dedicated RTV descriptor heap for BOE interlace output
static ComPtr<ID3D12DescriptorHeap> g_dx12_rtv_heap;

// Hotkey state
static bool g_was_fx_feature_pressed = false;
static bool g_fx_enabled = true;

static void on_init_device(device* device) {
	device_api api = device->get_api();
	switch (api)
	{
		case reshade::api::device_api::d3d12:
			g_cur_game_api = device_api::d3d12;
			break;
		default:
			break;
	}
}
// From ReShade resource_view (DX11 SRV handle) get texture size/format
static bool get_srv_info(resource_view view, uint32_t& width, uint32_t& height, DXGI_FORMAT& fmt)
{
	width = 0;
	height = 0;
	fmt = DXGI_FORMAT_UNKNOWN;
	if (view.handle == 0)
		return false;

	ID3D11ShaderResourceView* pSRV = reinterpret_cast<ID3D11ShaderResourceView*>(view.handle);
	ComPtr<ID3D11Resource> res;
	pSRV->GetResource(&res);
	if (!res)
		return false;

	ComPtr<ID3D11Texture2D> tex;
	if (FAILED(res.As(&tex)) || !tex)
		return false;

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

// Ensure BOE SDK + DX11 are initialized (used only on DX11 path)
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
		uint32_t out_w = 0, out_h = 0;
		runtime->get_screenshot_width_and_height(&out_w, &out_h);
		if (out_w == 0 || out_h == 0)
			return false;

		ID3D11Device* device11 = reinterpret_cast<ID3D11Device*>(runtime->get_device()->get_native());
		if (!device11)
			return false;

		BOE_ETDX_initDx11(device11,
			0, 0,
			static_cast<int>(out_w), static_cast<int>(out_h),
			DXGI_FORMAT_R8G8B8A8_UNORM,
			g_predict_delay);
		BOE_ETDX_setPupillaryDistance(65);
		g_etdx_dx11_inited = true;
		reshade::log::message(reshade::log::level::warning, "[SBS] BOE_ETDX_initDx11 OK");
	}

	return true;
}

// Toggle FX on Ctrl+Shift+F1
static void on_reshade_present(reshade::api::effect_runtime* runtime)
{
	bool is_ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool is_shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	bool is_k_down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
	bool is_combination_down = is_ctrl_down && is_shift_down && is_k_down;

	if (is_combination_down && !g_was_fx_feature_pressed)
	{
		g_fx_enabled = !g_fx_enabled;
		set_technique_enabled(runtime, kEffectNameWithoutExt, g_fx_enabled);
	}

	g_was_fx_feature_pressed = is_combination_down;
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

// Unified DX11/DX12 interlace entry
static void on_reshade_finish_effects(effect_runtime* runtime, command_list* cmd_list, resource_view rtv, resource_view rtv_srgb)
{
	if (!g_fx_enabled)
		return;

	// Initialize BOE SDK once (works for both DX11/DX12)
	if (!g_etdx_sdk_inited)
	{
		int r = BOE_ETDX_initSDK("./", 2);
		if (r == 0)
		{
			reshade::log::message(reshade::log::level::error, "[SBS] BOE_ETDX_initSDK failed");
			return;
		}
		g_etdx_sdk_inited = true;
		reshade::log::message(reshade::log::level::warning, "[SBS] BOE_ETDX_initSDK OK");
	}

	// Lookup SBS texture and cache its SRVs
	if (g_sbs_tex == 0)
		g_sbs_tex = runtime->find_texture_variable(kEffectName, kSbsTexName);
	if (g_sbs_tex == 0)
		return;

	runtime->get_texture_binding(g_sbs_tex, &g_sbs_srv_cached, &g_sbs_srv_srgb_cached);
	if (g_sbs_srv_cached.handle == 0 && g_sbs_srv_srgb_cached.handle == 0)
		return;

	if (g_cur_game_api == device_api::d3d12)
	{
		// ---------------- DX12 PATH ----------------
		resource_view chosen_srv = (g_sbs_srv_cached.handle != 0) ? g_sbs_srv_cached : g_sbs_srv_srgb_cached;

		ID3D12Device* nativeDev = reinterpret_cast<ID3D12Device*>(runtime->get_device()->get_native());
		if (!nativeDev)
		{
			reshade::log::message(reshade::log::level::error, "[SBS] DX12: no native device");
			return;
		}

		ID3D12GraphicsCommandList* cmd12 = reinterpret_cast<ID3D12GraphicsCommandList*>(cmd_list->get_native());
		if (!cmd12)
			return;

		reshade::api::resource sbs_resource = runtime->get_device()->get_resource_from_view(chosen_srv);
		if (sbs_resource.handle == 0)
		{
			reshade::log::message(reshade::log::level::error, "[SBS] DX12: cannot get resource from SBS SRV");
			return;
		}
		ID3D12Resource* inputRes = reinterpret_cast<ID3D12Resource*>(sbs_resource.handle);

		resource_view target_rv = (rtv.handle != 0) ? rtv : rtv_srgb;
		if (target_rv.handle == 0)
			return;
		reshade::api::resource target_resource = runtime->get_device()->get_resource_from_view(target_rv);
		if (target_resource.handle == 0)
		{
			reshade::log::message(reshade::log::level::error, "[SBS] DX12: cannot get resource from RTV");
			return;
		}
		ID3D12Resource* outputRes = reinterpret_cast<ID3D12Resource*>(target_resource.handle);

		// Create dedicated RTV heap (once)
		if (!g_dx12_rtv_heap)
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.NumDescriptors = 1;
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(nativeDev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_dx12_rtv_heap))))
			{
				reshade::log::message(reshade::log::level::error, "[SBS] DX12: failed to create RTV heap");
				return;
			}
			reshade::log::message(reshade::log::level::warning, "[SBS] DX12: own RTV heap created");
		}

		// Create RTV into our heap (safe per-frame)
		D3D12_CPU_DESCRIPTOR_HANDLE ourRtvHandle = g_dx12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
		nativeDev->CreateRenderTargetView(outputRes, nullptr, ourRtvHandle);

		// Input texture info
		D3D12_RESOURCE_DESC inputDesc = inputRes->GetDesc();
		DXGI_FORMAT inFmt = inputDesc.Format;
		uint32_t sbs_w = static_cast<uint32_t>(inputDesc.Width);
		uint32_t sbs_h = static_cast<uint32_t>(inputDesc.Height);

		uint32_t screen_w = 0, screen_h = 0;
		runtime->get_screenshot_width_and_height(&screen_w, &screen_h);
		if (screen_w == 0 || screen_h == 0)
			return;

		static ID3D12Resource* s_initInputRes = nullptr;
		if (!g_etdx_dx12_inited || s_initInputRes != inputRes)
		{
			if (g_etdx_dx12_inited)
			{
				BOE_ETDX_releaseDx12();
				g_etdx_dx12_inited = false;
			}

			char dbg[512];
			sprintf(dbg, "[SBS] DX12 init: dev=%p inputRes=%p outputRes=%p screen=%ux%u sbs=%ux%u inFmt=%d",
				(void*)nativeDev, (void*)inputRes, (void*)outputRes,
				screen_w, screen_h, sbs_w, sbs_h, (int)inFmt);
			reshade::log::message(reshade::log::level::warning, dbg);

			int initRes2 = BOE_ETDX_initDx12((void*)nativeDev, (void*)inputRes,
				0, 0, static_cast<int>(screen_w), static_cast<int>(screen_h),
				static_cast<int>(inFmt), g_predict_delay);
			if (initRes2 == 0)
			{
				reshade::log::message(reshade::log::level::error, "[SBS] BOE_ETDX_initDx12 failed");
				return;
			}
			BOE_ETDX_setPupillaryDistance(70);
			g_etdx_dx12_inited = true;
			s_initInputRes = inputRes;
			reshade::log::message(reshade::log::level::warning, "[SBS] BOE_ETDX_initDx12 OK");
		}

		// Bind RT before interlace (SDK does not bind internally)
		cmd12->OMSetRenderTargets(1, &ourRtvHandle, FALSE, nullptr);

		int rst = BOE_ETDX_doInterlaceDx12((void*)cmd12, (void*)outputRes, (void*)&ourRtvHandle,
			static_cast<int>(sbs_w), static_cast<int>(sbs_h), static_cast<int>(inFmt), DILR);

		//char result_log[256];
		//sprintf(result_log, "[SBS] doInterlace DX12 rst=%d inFmt=%d sbs=%ux%u screen=%ux%u",
		//	rst, (int)inFmt, sbs_w, sbs_h, screen_w, screen_h);
		//reshade::log::message(reshade::log::level::warning, result_log);
	}
	else
	{
		// ---------------- DX11 PATH ----------------
		if (!ensure_etdx_ready(runtime))
			return;

		// Choose input SRV
		resource_view chosen_srv_view = (g_sbs_srv_cached.handle != 0) ? g_sbs_srv_cached : g_sbs_srv_srgb_cached;
		uint32_t in_w = 0, in_h = 0;
		DXGI_FORMAT in_fmt = DXGI_FORMAT_UNKNOWN;
		if (!get_srv_info(chosen_srv_view, in_w, in_h, in_fmt))
			return;

		ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
		if (!ctx)
			return;

		resource_view target = (rtv.handle != 0) ? rtv : rtv_srgb;
		if (target.handle == 0)
			return;
		ID3D11RenderTargetView* dst_rtv = reinterpret_cast<ID3D11RenderTargetView*>(target.handle);

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

		// If output format changed, re-init BOE DX11
		if (g_etdx_dx11_inited && g_etdx_out_format != static_cast<int>(dstDesc.Format))
		{
			BOE_ETDX_releaseDx11();
			g_etdx_dx11_inited = false;
			g_etdx_out_format = -1;
		}

		if (!g_etdx_dx11_inited)
		{
			ID3D11Device* device11 = reinterpret_cast<ID3D11Device*>(runtime->get_device()->get_native());
			if (!device11)
				return;
			int initRes = BOE_ETDX_initDx11(device11,
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

		// Select SRV for BOE (sRGB fix)
		ID3D11ShaderResourceView* src_srv = nullptr;
		if ((in_fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || in_fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) && g_sbs_srv_srgb_cached.handle != 0)
			src_srv = reinterpret_cast<ID3D11ShaderResourceView*>(g_sbs_srv_srgb_cached.handle);
		else
			src_srv = reinterpret_cast<ID3D11ShaderResourceView*>(g_sbs_srv_cached.handle);
		if (!src_srv)
			return;

		ComPtr<ID3D11Resource> src_res;
		src_srv->GetResource(&src_res);
		bool need_temp_copy = (src_res && dst_res && src_res.Get() == dst_res.Get());

		ID3D11ShaderResourceView* srv_for_boe = src_srv;
		ComPtr<ID3D11Texture2D> temp_tex;
		ComPtr<ID3D11ShaderResourceView> temp_srv;

		if (need_temp_copy)
		{
			ComPtr<ID3D11Texture2D> src_tex;
			if (FAILED(src_res.As(&src_tex)))
			{
				reshade::log::message(reshade::log::level::error, "[SBS] Failed to cast src resource to texture2D");
				return;
			}
			D3D11_TEXTURE2D_DESC srcDesc{};
			src_tex->GetDesc(&srcDesc);

			srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			srcDesc.CPUAccessFlags = 0;
			srcDesc.Usage = D3D11_USAGE_DEFAULT;
			srcDesc.MiscFlags = 0;

			ID3D11Device* device11 = nullptr;
			ctx->GetDevice(&device11);
			if (!device11)
				return;

			if (FAILED(device11->CreateTexture2D(&srcDesc, nullptr, &temp_tex)))
			{
				reshade::log::message(reshade::log::level::error, "[SBS] Failed to create temporary texture for SRV copy");
				device11->Release();
				return;
			}

			ctx->CopyResource(temp_tex.Get(), src_tex.Get());

			D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
			srvd.Format = srcDesc.Format;
			srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvd.Texture2D.MostDetailedMip = 0;
			srvd.Texture2D.MipLevels = 1;
			if (FAILED(device11->CreateShaderResourceView(temp_tex.Get(), &srvd, &temp_srv)))
			{
				reshade::log::message(reshade::log::level::error, "[SBS] Failed to create SRV for temporary texture");
				temp_tex.Reset();
				device11->Release();
				return;
			}
			device11->Release();

			srv_for_boe = temp_srv.Get();
			reshade::log::message(reshade::log::level::warning, "[SBS] Source equals destination - copied to temp SRV before interlace");
		}

		const UINT kSaveSlots = 16;
		ID3D11ShaderResourceView* savedSRVs[kSaveSlots] = {};
		ctx->PSGetShaderResources(0, kSaveSlots, savedSRVs);
		ID3D11ShaderResourceView* nullSRVs[kSaveSlots] = {};
		ctx->PSSetShaderResources(0, kSaveSlots, nullSRVs);

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

		ctx->OMSetRenderTargets(1, &prevRTV, prevDSV);
		if (prevRTV) { prevRTV->Release(); }
		if (prevDSV) { prevDSV->Release(); }
		ctx->RSSetViewports(numPrevVP, prevVP);

		ctx->PSSetShaderResources(0, kSaveSlots, savedSRVs);
		for (UINT i = 0; i < kSaveSlots; ++i)
			if (savedSRVs[i]) savedSRVs[i]->Release();

		temp_srv.Reset();
		temp_tex.Reset();

		//char log_buf[256];
		//sprintf(log_buf, "[SBS] doInterlace DX11 result=%d in:%ux%u in_fmt=%d out_fmt=%d", result, in_w, in_h, in_fmt, static_cast<int>(dstDesc.Format));
		//reshade::log::message(reshade::log::level::warning, log_buf);
	}
}

static void on_reshade_reloaded_effects(effect_runtime* runtime)
{
	// Reset cached handles so they are re-resolved after effect reload
	g_depth_tex = { 0 };
	g_color_tex = { 0 };
	g_sbs_tex = { 0 };
	srv = { 0 };
	srv_srgb = { 0 };
	g_sbs_srv_cached = { 0 };
	g_sbs_srv_srgb_cached = { 0 };
}

void register_addon_sbs_game_bridge()
{
	reshade::log::message(reshade::log::level::warning, "[SBS] Register Addon _sbs_game_bridge");
	reshade::register_overlay(ptr_wName, draw_sbs_overlay);
	reshade::register_event<reshade::addon_event::reshade_finish_effects>(on_reshade_finish_effects);
	reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
	reshade::register_event<reshade::addon_event::reshade_present>(on_reshade_present);
	reshade::register_event<reshade::addon_event::init_device>(on_init_device);
}

void unregister_addon_sbs_game_bridge()
{
	reshade::unregister_overlay(ptr_wName, draw_sbs_overlay);
	reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(on_reshade_finish_effects);
	reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
	reshade::unregister_event<reshade::addon_event::reshade_present>(on_reshade_present);
	reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
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
		// Release BOE ETDX resources
		if (g_etdx_dx12_inited) { BOE_ETDX_releaseDx12(); g_etdx_dx12_inited = false; }
		if (g_etdx_dx11_inited) { BOE_ETDX_releaseDx11(); g_etdx_dx11_inited = false; }
		if (g_etdx_sdk_inited) { BOE_ETDX_releaseSDK();  g_etdx_sdk_inited = false; }
		g_dx12_rtv_heap.Reset();
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}

#endif


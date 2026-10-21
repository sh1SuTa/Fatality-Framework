#include <chrono>
#include <cstring>
#include <macros.h>
#include <game/draw_manager.h>
#include <game/game.h>
#include <game/hook_manager.h>
#include <gui/gui.h>
#include <menu/menu.h>
#include <menu/init/init_anim.h>
#include <ren/renderer.h>
#include <ren/adapters/adapter_dx11.h>
#include <sdk/globalvars.h>
#include <sdk/offsets.h>
#include <game/visuals.h>
#include <game/cfg.h>

namespace hooks::steam
{
	using namespace evo::ren;
	using namespace evo::gui;

	namespace
	{
		// present 内有需要对象展开的局部（lock_guard 等），SEH 必须隔离到 noinline 函数
		__declspec(noinline) bool gv_time_seh(float* rt, float* ft)
		{
			__try
			{
				if (!sdk::GlobalVars)
					return false;
				*rt = sdk::GlobalVars->real_time;
				*ft = sdk::GlobalVars->frame_time;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	HRESULT present(IDXGISwapChain*chain, UINT sync, UINT flags)
	{

		

		static ID3D11RenderTargetView *view;

		if (!draw.adapter)
		{
			// fix dx11 memes.
			ID3D11Device *dev{};
			ID3D11DeviceContext *ctx{};

			chain->GetDevice(__uuidof(ID3D11Device), (void **)&dev);
			dev->GetImmediateContext(&ctx);

			ID3D11Texture2D *back_buffer;
			chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back_buffer));

			D3D11_RENDER_TARGET_VIEW_DESC desc{};
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
			dev->CreateRenderTargetView(back_buffer, &desc, &view);

			back_buffer->Release();

			DXGI_SWAP_CHAIN_DESC chain_desc;
			chain->GetDesc(&chain_desc);

			draw.adapter = std::make_shared<adapter_dx11>(chain_desc.OutputWindow, dev, ctx);
			draw.create_layer(game_layer);
			draw.create_layer(17);
			draw.create_layer(63);

			// copy esp layer and make it deferred
			draw_mgr.buf = draw.layers[game_layer];
			draw_mgr.buf->is_deferred = true;
			draw_mgr.buf->skip_dpi = true;

			menu::init_anim.on_adapter_ready();

			draw.refresh();
			draw.create_objects();
		}

		if (menu::init_anim.init_ready && !menu::init_anim.init_done)
		{
			draw.scale = std::clamp(draw.display.y, 1.f, FLT_MAX) / 1080.f;
			ctx = std::make_unique<context>();

			char win_dir_arr[256]{};
			GetSystemWindowsDirectoryA(win_dir_arr, 256);

			const std::string win_dir{win_dir_arr};
			draw.fonts[FNV1A("esp")] = std::make_shared<font_gdi>( ("Smallest Pixel-7"), 11.f, font_flag_outline | font_flag_no_dpi, 0, 0x45F);
			// ESP 名字字体：中文玩家名需要 CJK 字形。SimHei（黑体）是简中 Windows
			// 标配的纯 .ttf；stbtt_InitFont 无法直接初始化 .ttc 集合文件，所以不用
			// msyh.ttc。非中文系统没有 SimHei 时回退 Segoe UI Bold（此时中文名仍
			// 无法显示，属预期）。字形上界扩到 CJK 基本区 0x9FA5，否则 get_glyph
			// 未命中会返回零尺寸 fallback，中文静默不渲染。
			// 注意：font 类按值保存 const char* path，路径字符串必须是静态生命
			// 周期（create() 在 create_objects 时才读它），不能用临时 string。
			static const std::string cjk_font_path{win_dir + ("/fonts/simhei.ttf")};
			static const std::string latin_font_path{win_dir + ("/fonts/segoeuib.ttf")};
			const bool has_cjk_font = GetFileAttributesA(cjk_font_path.c_str()) != INVALID_FILE_ATTRIBUTES;
			draw.fonts[FNV1A("esp_name")] = std::make_shared<font>(
				(has_cjk_font ? cjk_font_path : latin_font_path).c_str(), 13.f, font_flag_shadow | font_flag_no_dpi, 0,
				has_cjk_font ? 0x9FA5 : 0x45F);
			static const std::string lby_font_path{win_dir + ("/fonts/verdanab.ttf")};
			draw.fonts[FNV1A("lby")] = std::make_shared<font>(lby_font_path.c_str(), 27.f, font_flag_shadow, 0, 0x80);
			menu::men.init();

			draw.refresh();
			draw.create_objects();

			menu::init_anim.init_done = true;
		}

		static_cast<ID3D11DeviceContext *>(draw.adapter->get_context())->OMSetRenderTargets(1, &view, nullptr);

		// GlobalVars 不能用 init 时的缓存指针：CS2 切图/大厅会重写槽，旧值悬垂
		// （dump 实锤：present+0x900 读 real_time AV）。与 var() 缓存悬垂同族问题，
		// 每帧重读 + 判空 + SEH 兜底。
		//
		// [font fix] 主菜单/大厅时 client 的 GlobalVars 槽未激活（实测 real_time/
		// frame_time 全 0）—— animator（animator.h）只在 frame_time>0 时推进动画，
		// 时间恒 0 → 菜单文字 fade-in 停在 t=0、alpha=0（症状：菜单框在、字没有）。
		// 引擎时间不可信时改用本地单调时钟兜底，GUI 时序与游戏引擎完全解耦。
		bool gv_time_ok = false;
		if (game->client.handle)
			sdk::GlobalVars = reinterpret_cast<sdk::global_vars_t*>(game->client.deref(sdk::offsets::globals::global_vars));
		float rt = 0.f, ft = 0.f;
		if (gv_time_seh(&rt, &ft))
		{
			// NaN 比较恒 false 也会走兜底；ft<1 排除垃圾大值
			if (rt > 0.f && ft > 0.f && ft < 1.f)
			{
				draw.time = rt;
				draw.frame_time = ft;
				gv_time_ok = true;
			}
		}
		if (!gv_time_ok)
		{
			static LARGE_INTEGER freq = {};
			static LARGE_INTEGER prev = {};
			static float local_time = 0.f;
			if (!freq.QuadPart)
				QueryPerformanceFrequency(&freq);
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			if (prev.QuadPart)
			{
				auto dt = float(double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart));
				if (dt < 0.f)
					dt = 0.f;
				if (dt > 0.1f)
					dt = 0.1f; // 断帧/断点后恢复保护
				local_time += dt;
				draw.time = local_time;
				draw.frame_time = dt;
			}
			prev = now;
		}

		draw.refresh();

		if (menu::init_anim.init_done)
		{
			if (menu::men.full_cycle_reset)
			{
				menu::men.full_cycle_reset = false;
				menu::group::rebuild_legit_layout();
				menu::group::rebuild_rage_layout();
				ctx->reset();
				ctx->should_update_old_value = false;
			}

			std::lock_guard lock(menu::men.render_mtx);
			draw.begin_frame();
			ctx->render();

			menu::men.finalize();
		}
		else
		{
			draw.begin_frame();
			menu::init_anim.render();
		}

		draw.end_frame();

		//handle synced texture resets
		if (draw.layers[game_layer]->deferred_dirty && draw_mgr.dirty_ackn)
		{
			draw.layers[game_layer]->cb->reset(true);
			draw.layers[game_layer]->deferred_dirty = false;
			draw_mgr.dirty_ackn = false;
		}

		draw.render();
		draw.flush();

		var(r_aspectratio);
		if (r_aspectratio)
			r_aspectratio->value.fl = cfg.misc.aspect.get() / 100.f;

		if (menu::init_anim.init_done && evo::ren::draw.adapter && draw_mgr.buf->vb->object && !draw_mgr.buf->is_deferred_ready)
		{
			const bool was_dirty = draw_mgr.buf->deferred_dirty;
			if (!was_dirty && !draw_mgr.dirty_ackn)
			{
				draw_mgr.buf->reset(true);
				draw_mgr.buf->lock();

				visuals.run();

				if (menu::men.finalized)
				{
					const auto ctx = static_cast<ID3D11DeviceContext*>(evo::ren::draw.adapter->get_deferred_context());
					ctx->ClearState();

					const auto cmd_list = reinterpret_cast<ID3D11CommandList**>(evo::ren::draw.adapter->get_deferred_list());
					if (*cmd_list)
						(*cmd_list)->Release();

					draw_mgr.buf->unlock();

					ctx->FinishCommandList(false, cmd_list);
					draw_mgr.buf->is_deferred_ready = true;
				}

			}
			else
			{
				if (was_dirty)
					draw_mgr.dirty_ackn = true;
				draw_mgr.buf->is_deferred_ready = true;
			}
		}

		// handle safe font reset
		if (menu::men.font_reset)
		{
			draw.layers[game_layer]->deferred_dirty = true;
			// update fonts
			for (auto &[_, f] : draw.fonts)
			{
				if (!f)
					continue;

				f->destroy();
				f->create();
			}

			menu::men.font_was_reset = true;
			menu::men.font_reset = false;
		}
		const auto oPresent = hkPresent.GetOriginal();
		return oPresent(chain, sync, flags);
	}
} 

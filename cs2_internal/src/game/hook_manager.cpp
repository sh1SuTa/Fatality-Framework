#include <game/game.h>
#include <game/hook_manager.h>
#include <utils/mem.h>
#include <memory/memory.h>
#include <chrono>
#include <cstring>
#include <thread>

hook_manager_t hook_manager;

namespace
{
	// a stale vtable index must not crash the process during installation
	bool is_readable(const void* p)
	{
		if (p == nullptr)
			return false;
		MEMORY_BASIC_INFORMATION info{};
		if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
			return false;
		const auto protection = info.Protect & 0xff;
		return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
			protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
	}

	bool is_executable(const void* p)
	{
		if (p == nullptr)
			return false;
		MEMORY_BASIC_INFORMATION info{};
		if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
			return false;
		const auto protection = info.Protect & 0xff;
		return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
			protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
	}

	void* safe_vfunc(const void* object, std::size_t index)
	{
		if (!is_readable(object))
			return nullptr;
		const auto vtable = *static_cast<void* const*>(object);
		if (!is_readable(vtable))
			return nullptr;
		const auto target = static_cast<void* const*>(vtable)[index];
		return is_executable(target) ? target : nullptr;
	}
}

void hook_manager_t::init()
{
	if (MH_Initialize() != MH_OK)
	{
		OutputDebugStringA("Fatality: MH_Initialize failed.\n");
		return;
	}

	const auto create_logged = [](const char* szName, bool bAttached, void* pTarget)
		{
			char szBuffer[128];
			wsprintfA(szBuffer, "Fatality: hook %s target=%p -> %s\n", szName, pTarget, bAttached ? "attached" : (pTarget ? "FAILED" : "BAD TARGET"));
			OutputDebugStringA(szBuffer);
		};

	// attach the present hook last: it fires immediately on the render
	// thread and must not interrupt the installation diagnostics below
	{
		char szBuffer[96];
		wsprintfA(szBuffer, "Fatality: Input=%p\n", static_cast<void*>(sdk::Input));
		OutputDebugStringA(szBuffer);
		// MouseInputEnabled 真实索引 = 11（client.dll 静态 RTTI 核验：
		// 槽 11 = "mov rax,[g_InputStackManager]; cmp byte [rax+0x58],0; sete al; ret"，
		// 特征字节 80 78 58 00 0F 94 C0；老索引 13 已随版本漂移）
		const auto mouse_target = safe_vfunc(sdk::Input, 11U);
		create_logged("mouse_input_enabled", mouse_target && hooks::input_system::hkMouseInputEnabled.Create(mouse_target, reinterpret_cast<void*>(&hooks::input_system::mouse_input_enabled)), mouse_target);
	}
	{
		char szBuffer[96];
		wsprintfA(szBuffer, "Fatality: Client=%p\n", static_cast<void*>(sdk::Client));
		OutputDebugStringA(szBuffer);
		const auto render_start_target = safe_vfunc(sdk::Client, 4U);
		create_logged("on_render_start", render_start_target && hooks::client::hkOnRenderStart.Create(render_start_target, reinterpret_cast<void*>(&hooks::client::on_render_start)), render_start_target);
	}
	{
		// [aw-verify] client 同步 trace 漏斗（RVA 0xA1A890）：F5 武装单次 dump，仅日志
		const auto trace_target = reinterpret_cast<void*>(game->client.at(sdk::offsets::functions::client::trace_funnel));
		create_logged("trace_funnel", trace_target && hooks::client::hkTraceFunnel.Create(trace_target, reinterpret_cast<void*>(&hooks::client::trace_funnel)), trace_target);
	}
	{
		// [aw-verify] trace 展开器（RVA 0x9D31A0）：只处理命中记录，漏斗冷时的主捕获点
		const auto expander_target = reinterpret_cast<void*>(game->client.at(sdk::offsets::functions::client::trace_expander));
		create_logged("trace_expander", expander_target && hooks::client::hkTraceExpander.Create(expander_target, reinterpret_cast<void*>(&hooks::client::trace_expander)), expander_target);
	}
	// [aw-verify] bullet_trace_helper（0x8C931B）与 11 个 filter dtor 钩已撤装：
	// 实测 CCSTraceFilterSimple 在客户端零构造（服务端类）、栈上析构不走 vtable
	// slot0，两路钩全程 0 调用 —— 纯启动期补丁风险（大厅注入闪退嫌疑），RVA 保留
	// 在 offsets::filter_dtors / functions::client::bullet_trace_helper 备查。

	const auto present_target = safe_vfunc(sdk::SwapChain->pDXGISwapChain, 8U);
	create_logged("present", present_target && hooks::steam::hkPresent.Create(present_target, reinterpret_cast<void*>(&hooks::steam::present)), present_target);

	///create_hook(frame_stage_notify, game->client.at(sdk::offsets::functions::client::frame_stage_notify), &hooks::client::frame_stage_notify);

	///create_hook(override_view, game->client.at(sdk::offsets::functions::client::override_view), &hooks::client::override_view);

	//create_hook(on_render_start, game->client.at(sdk::offsets::functions::client::on_render_start), &hooks::client::on_render_start);

	///create_hook(get_fov, game->client.at(sdk::offsets::functions::cameramanager::set_fov), &hooks::client::get_fov);

	{
		// create_move (client RVA 0x740e50, unverified - dump first 16 bytes for IDA cross-check)
		const auto create_move_target = reinterpret_cast<void*>(game->client.at(sdk::offsets::functions::input::csgoinput_create_move));
		if (is_executable(create_move_target))
		{
			char szBytes[96] = "Fatality: create_move bytes=";
			auto* p = szBytes + std::strlen(szBytes);
			for (int i = 0; i < 16; ++i)
				p += wsprintfA(p, "%02X", static_cast<const unsigned char*>(create_move_target)[i]);
			p[0] = '\n'; p[1] = '\0';
			OutputDebugStringA(szBytes);
		}
		create_logged("create_move", create_move_target && hooks::client::hkCreateMove.Create(create_move_target, reinterpret_cast<void*>(&hooks::client::create_move)), create_move_target);
	}

	///create_hook(create_move, game->client.at(sdk::offsets::functions::input::csgoinput_create_move), &hooks::client::create_move);

	/////////////////////////////////create_hook(prediction_update, game->client.at(0x7A5860), &hooks::client::prediction_update);
}

void hook_manager_t::attach() const
{
	for (auto &hook : hooks)
		hook->attach();
}

void hook_manager_t::detach() const
{
	// 立即停止所有 detour：后续游戏调用直接进原函数
	MH_DisableHook(MH_ALL_HOOKS);

	// 等待仍在 hook 内部执行的调用退出（present 等都是毫秒级）
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	// 还原窗口过程：必须在模块卸载前完成，否则游戏下次收到消息就崩
	if (sdk::hWindow && sdk::pOldWndProc)
		SetWindowLongPtrW(sdk::hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(sdk::pOldWndProc));

	MH_Uninitialize();
	OutputDebugStringA("Fatality: hooks detached.\n");
}

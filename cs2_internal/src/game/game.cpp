#include <filesystem>
#include <game/cfg.h>
#include <game/draw_manager.h>
#include <game/game.h>
#include <game/hook_manager.h>
#include <ren/renderer.h>
#include <resources/smallest_pixel.h>
#include <sdk/cvar.h>
#include <sdk/engine.h>
#include <sdk/inputsystem.h>
#include <sdk/interface.h>
#include <utils/mem.h>
#include "memory/interfaceless.h"
#include <sdk/offsets_build.h>

uint32_t utils::runtime_basis = 2166136261u;

std::unique_ptr<game_t> game;

namespace fs = std::filesystem;

namespace
{
	template <typename T>
	bool read_swap_chain_value(const void* address, T& value)
	{
		SIZE_T bytes_read = 0;
		return address && ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes_read)
			&& bytes_read == sizeof(value);
	}

	std::uint8_t* find_swap_chain_slot(const std::uint8_t* code, std::size_t code_size,
		const std::uint8_t* module, std::size_t module_size, bool* linked_list = nullptr)
	{
		struct pattern_t { const char* bytes; std::size_t displacement; std::size_t end; bool list; };
		constexpr pattern_t patterns[] = {
			// Supplied renderer: RVA 0x2ba56 -> list storage slot 0x496058.
			// CreateSwapChain (0x3f320) and list traversal (0x338a0) confirm 16-byte entries.
			{ "48 89 2D ? ? ? ? 66 0F 7F 05 ? ? ? ? FF 15 ? ? ? ? 48 8D 0D", 3, 7, true },
			// Historical layouts; keep both RIP displacements wildcarded.
			{ "66 0F 7F 05 ? ? ? ? 66 0F 7F 0D ? ? ? ? 48 89 35", 4, 8, false },
			{ "66 0F 7F 0D ? ? ? ? 48 8B F7 66 0F 7F 05 ? ? ? ?", 4, 8, false },
		};
		std::uint8_t* slot = nullptr;
		bool slot_is_list = false;
		if (linked_list)
			*linked_list = false;
		for (const auto& pattern : patterns)
		{
			std::uint8_t bytes[64]{};
			char mask[64]{};
			const auto count = MEM::PatternToBytes(pattern.bytes, bytes, mask);
			const auto matches = MEM::FindPatternAllOccurrencesEx(code, code_size, bytes, count, mask);
			if (matches.empty())
				continue;
			if (matches.size() != 1)
			{
				OutputDebugStringA("Fatality: swap-chain pattern is ambiguous; initialization stopped.\n");
				return nullptr;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, matches.front() + pattern.displacement, sizeof(displacement));
			const auto target = static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(matches.front()))
				+ pattern.end + displacement;
			const auto module_base = reinterpret_cast<std::uintptr_t>(module);
			const auto address = static_cast<std::uintptr_t>(target);
			if (module_size < sizeof(void*) || address < module_base || address - module_base > module_size - sizeof(void*))
			{
				OutputDebugStringA("Fatality: swap-chain RIP target is outside the renderer module.\n");
				return nullptr;
			}
			auto candidate = reinterpret_cast<std::uint8_t*>(address);
			if (pattern.list && (module_size < 24 || address - module_base < 8 || address - module_base > module_size - 16))
			{
				OutputDebugStringA("Fatality: swap-chain list metadata is outside the renderer module.\n");
				return nullptr;
			}
			if (slot && (slot != candidate || slot_is_list != pattern.list))
			{
				OutputDebugStringA("Fatality: swap-chain patterns resolve to different slots.\n");
				return nullptr;
			}
			slot = candidate;
			slot_is_list = pattern.list;
		}
		if (linked_list)
			*linked_list = slot_is_list;
		if (!slot)
			OutputDebugStringA("Fatality: no supported swap-chain pattern in rendersystemdx11.dll; renderer binary verification is required.\n");
		return slot;
	}

	ISwapChainDx11* read_ready_swap_chain(const void* slot, bool linked_list = false)
	{
		ISwapChainDx11** pointer = nullptr;
		ISwapChainDx11* chain = nullptr;
		IDXGISwapChain* dxgi_chain = nullptr;
		void** vtable = nullptr;
		void* present = nullptr;
		// The current renderer stores active swap chains in a linked list, not a plain pointer vector.
		if (!read_swap_chain_value(slot, pointer) || !pointer)
			return nullptr;
		const void* entry = pointer;
		if (linked_list)
		{
			const auto metadata = static_cast<const std::uint8_t*>(slot);
			std::int32_t count = 0, head = -1;
			std::uint32_t capacity = 0;
			if (!read_swap_chain_value(metadata - 8, count) ||
				!read_swap_chain_value(metadata - 4, capacity) ||
				!read_swap_chain_value(metadata + 8, head) || head < 0 || head >= count ||
				static_cast<std::uint32_t>(head) >= (capacity & 0x7fffffffU))
				return nullptr;
			entry = reinterpret_cast<const std::uint8_t*>(pointer) + static_cast<std::size_t>(head) * 16;
		}
		// Failed/stale pointer reads return false instead of being dereferenced.
		if (!read_swap_chain_value(entry, chain) || !chain ||
			!read_swap_chain_value(&chain->pDXGISwapChain, dxgi_chain) || !dxgi_chain ||
			!read_swap_chain_value(dxgi_chain, vtable) || !vtable ||
			!read_swap_chain_value(vtable + 8, present) || !present)
			return nullptr;
		MEMORY_BASIC_INFORMATION info{};
		if (!VirtualQuery(present, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
			return nullptr;
		const auto protection = info.Protect & 0xff;
		if (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ &&
			protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
			return nullptr;
		return chain;
	}

	ISwapChainDx11* resolve_swap_chain()
	{
		HMODULE renderer = nullptr;
		const auto module_deadline = GetTickCount64() + 5000;
		do
		{
			renderer = GetModuleHandleW(RENDERSYSTEM_DLL);
			if (renderer)
				break;
			Sleep(100);
		} while (GetTickCount64() < module_deadline);
		if (!renderer)
		{
			OutputDebugStringA("Fatality: rendersystemdx11.dll is not loaded after 5 seconds; check renderer selection/loading.\n");
			return nullptr;
		}
		std::uint8_t* code = nullptr;
		std::size_t code_size = 0;
		if (!MEM::GetSectionInfo(renderer, ".text", &code, &code_size))
		{
			OutputDebugStringA("Fatality: renderer .text section is unavailable.\n");
			return nullptr;
		}
		const auto module = reinterpret_cast<const std::uint8_t*>(renderer);
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
		bool linked_list = false;
		const auto slot = find_swap_chain_slot(code, code_size, module, nt->OptionalHeader.SizeOfImage, &linked_list);
		if (!slot)
			return nullptr;
		const auto chain_deadline = GetTickCount64() + 5000;
		do
		{
			if (const auto chain = read_ready_swap_chain(slot, linked_list))
			{
				return chain;
			}
			Sleep(100);
		} while (GetTickCount64() < chain_deadline);
		OutputDebugStringA("Fatality: swap-chain slot found, but pointer chain/0x170 DXGI layout is not ready or valid after 5 seconds.\n");
		return nullptr;
	}
}

game_t::game_t(uintptr_t _base, uint32_t tok) :
	base(_base)
{
	using namespace utils;

	game_dir = ENC2(util::get_game_dir());

	if (!fs::exists(DEC_INLINE(game_dir) + XOR("fatality")))
		fs::create_directories(DEC_INLINE(game_dir) + XOR("fatality"));
	if (!fs::exists(DEC_INLINE(game_dir) + XOR("fatality/scripts")))
		fs::create_directories(DEC_INLINE(game_dir) + XOR("fatality/scripts"));
	if (!fs::exists(DEC_INLINE(game_dir) + XOR("fatality/scripts/remote")))
		fs::create_directories(DEC_INLINE(game_dir) + XOR("fatality/scripts/remote"));
	if (!fs::exists(DEC_INLINE(game_dir) + XOR("fatality/scripts/lib")))
		fs::create_directories(DEC_INLINE(game_dir) + XOR("fatality/scripts/lib"));
}

void game_t::load_fonts()
{
	DWORD n_fonts;
	mem_font_hadles.push_back(AddFontMemResourceEx(smallest_pixel.data(), smallest_pixel.size(), nullptr, &n_fonts));
	smallest_pixel.clear();
	auto vec = std::vector<unsigned char>(smallest_pixel);
	smallest_pixel.swap(vec);
}

void game_t::remove_fonts() const
{
	for (auto &handle : mem_font_hadles)
		RemoveFontMemResourceEx(handle);
}


void game_t::init()
{
	using namespace utils;

	// All module-relative offsets below belong to one pinned game build.
	const auto engine_module = find_module(FNV1A("engine2.dll"));
	const auto client_module = find_module(FNV1A("client.dll"));
	if (!engine_module || !client_module ||
		sdk::offsets::snapshot::engine_build_number + sizeof(uint32_t) > engine_module->size)
	{
		OutputDebugStringA("Fatality: required game modules are unavailable.\n");
		return;
	}
	const auto build_number = *reinterpret_cast<const uint32_t*>(
		engine_module->base + sdk::offsets::snapshot::engine_build_number);
	if (build_number != sdk::offsets::snapshot::build_number)
	{
		OutputDebugStringA("Fatality: game build does not match the offset snapshot.\n");
		return;
	}

	// Previously these library handles were never initialized, including the
	// client handle used by world_to_screen_matrix().
	tier0 = library("tier0.dll");
	sdl3 = library("SDL3.dll");
	gameoverlayrenderer64 = library("GameOverlayRenderer64.dll");
	client = library("client.dll");
	engine2 = library("engine2.dll");
	inputsystem = library("inputsystem.dll");
	localize = library("localize.dll");
	panorama = library("panorama.dll");
	scenesystem = library("scenesystem.dll");

	MEM::Setup();

	static auto enwndcall = [](HWND handle, LPARAM lParam) -> BOOL CALLBACK
		{
			const auto MainWindow = [handle]()
				{
					return GetWindow(handle, GW_OWNER) == nullptr &&
						IsWindowVisible(handle) && handle != GetConsoleWindow();
				};

			DWORD nPID = 0;
			GetWindowThreadProcessId(handle, &nPID);

			if (GetCurrentProcessId() != nPID || !MainWindow())
				return TRUE;

			*reinterpret_cast<HWND*>(lParam) = handle;
			return FALSE;
		};

	const auto pTier0Handle = MEM::GetModuleBaseHandle(TIER0_DLL);
	if (pTier0Handle == nullptr)
		return ;

	const auto pEngineRegisterList = iless::GetRegisterList(ENGINE2_DLL);
	if (pEngineRegisterList == nullptr)
		return ;

	const auto pClientRegister = iless::GetRegisterList(CLIENT_DLL);
	if (pClientRegister == nullptr)
		return;


	const auto pTier0RegisterList = iless::GetRegisterList(TIER0_DLL);
	if (pTier0RegisterList == nullptr)
		return ;

	const auto pInputSystemRegisterList = iless::GetRegisterList(INPUTSYSTEM_DLL);
	if (pInputSystemRegisterList == nullptr)
		return;

	// load interface
	sdk::Client = Capture<ISource2Client>(pClientRegister, SOURCE2_CLIENT);
	sdk::GameResourceService = iless::Capture<sdk::cgame_resource_service>(pEngineRegisterList, GAME_RESOURCE_SERVICE_CLIENT);
	sdk::Engine = iless::Capture<sdk::cengine_client>(pEngineRegisterList, SOURCE2_ENGINE_TO_CLIENT);
	sdk::Cvar = iless::Capture<sdk::ccvar>(pTier0RegisterList, ENGINE_CVAR);
	constexpr std::ptrdiff_t required_globals[] = {
		sdk::offsets::globals::global_vars, sdk::offsets::globals::local_player_controller,
		sdk::offsets::globals::game_entity_system, sdk::offsets::globals::ccsgo_input,
	};
	for (const auto offset : required_globals)
		if (offset < 0 || offset + sizeof(uintptr_t) > client_module->size)
			return;
	sdk::GlobalVars = reinterpret_cast<sdk::global_vars_t*>(client.deref(sdk::offsets::globals::global_vars));
	sdk::LocalPlayerController = reinterpret_cast<sdk::cs2_player_controller*>(client.deref(sdk::offsets::globals::local_player_controller));
	sdk::GameEntitySystem = reinterpret_cast<sdk::cgame_entity_system*>(client.deref(sdk::offsets::globals::game_entity_system));
	// 注意语义差异：dwGameEntitySystem 是"指针全局"（值 = 对象地址，需解引用）；
	// dwCSGOInput 是"对象内联全局"（global_vars 偏移处直接就是 CCSGOInput 对象，
	// 解引用只会拿到它的 vftable 指针 —— 之前 Input=... 恰好等于 vftable RVA 就是
	// 这个原因），这里必须取地址本身，不能再 deref。
	sdk::Input = reinterpret_cast<sdk::ccsgo_input*>(client.at(sdk::offsets::globals::ccsgo_input));
	sdk::SwapChain = resolve_swap_chain();
	if (!sdk::SwapChain)
		return;
	OutputDebugStringA("Fatality: init: swap chain ok, capturing remaining interfaces.\n");
	sdk::InputSystem = iless::Capture<sdk::cinput_system>(pInputSystemRegisterList, INPUT_SYSTEM_VERSION);
	const auto mem_alloc_export = MEM::GetExportAddress(pTier0Handle, CS_XOR("g_pMemAlloc"));
	if (!mem_alloc_export)
	{
		OutputDebugStringA("Fatality: init: g_pMemAlloc export not found.\n");
		return;
	}
	sdk::MemAlloc = *reinterpret_cast<sdk::cmem_alloc**>(mem_alloc_export);
	if (!sdk::Client || !sdk::GameResourceService || !sdk::Engine || !sdk::Cvar ||
		!sdk::Input || !sdk::InputSystem || !sdk::MemAlloc)
	{
		OutputDebugStringA("Fatality: init: one or more required interfaces are missing.\n");
		return;
	}

	// 注意：fn.get_weapon_data 的接线已弃用 —— 实测其偏移（0x4a7f40）在当前 build
	// 指向错误函数，调用后触发 fail-fast 终止（__except 无法兜底，进程直接终止）。
	// ammo 功能已改为 weapon_id 查表获取弹匣容量，见 visuals.cpp 的 mag_sizes。

	/* // old interface
	ui_engine = encrypted_pptr<sdk::cui_engine>(client.at(sdk::offsets::globals::panorama_ui));
	game_ui_funcs = encrypted_ptr<sdk::cgame_ui_funcs>(engine2.at(sdk::offsets::interfaces::engine2::vengine_gameuifuncs_version005));
	loc = encrypted_ptr<sdk::clocalize>(localize.at(sdk::offsets::interfaces::localize::localize_001));
	prediction = encrypted_ptr<sdk::cprediction>(client.at(sdk::offsets::interfaces::client::source2_client_prediction001));

	fn.get_weapon_data = reinterpret_cast<get_weapon_data_t>(client.at(sdk::offsets::functions::client::get_weapon_data));
	fn.sdl_set_relative_mouse_mode = reinterpret_cast<sdl_set_relative_mouse_mode_t>(sdl3.at(sdk::offsets::functions::sdl3::set_relative_mouse_mode));
	fn.sdl_set_window_grab = reinterpret_cast<sdl_set_window_grab_t>(sdl3.at(sdk::offsets::functions::sdl3::set_window_grab));
	fn.set_channel_verbosity = reinterpret_cast<set_channel_verbosity_t>(tier0.at(sdk::offsets::functions::logging_system::set_channel_verbosity));
	fn.register_logging_channel = reinterpret_cast<register_logging_channel_t>(tier0.at(sdk::offsets::functions::logging_system::register_logging_channel));
	fn.log_direct = reinterpret_cast<log_direct_t>(tier0.at(sdk::offsets::functions::logging_system::log_direct));
	fn.find_channel = reinterpret_cast<find_channel_t>(tier0.at(sdk::offsets::functions::logging_system::find_channel));
	fn.set_channel_verbosity(fn.find_channel("Shooting"), sdk::logging_severity_t::LS_HIGHEST_SEVERITY);*/

	while (sdk::hWindow == nullptr)
	{
		EnumWindows(enwndcall, reinterpret_cast<LPARAM>(&sdk::hWindow));
		::Sleep(200U);
	}

	sdk::pOldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(sdk::hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hooks::input_system::wnd_proc)));
	if (sdk::pOldWndProc == nullptr)
		return;

	//load_fonts();

	// load other
	cfg.init();

	OutputDebugStringA("Fatality: init: unlocking cvars.\n");
	{
		// dump the captured CCvar object so the real list offset can be
		// identified offline when the manual layout does not match
		const auto dump = reinterpret_cast<const std::uint32_t*>(sdk::Cvar);
		char buffer[384]{};
		int pos = wsprintfA(buffer, "Fatality: CCvar dump:");
		for (int i = 0; i < 32 && pos < static_cast<int>(sizeof(buffer)) - 16; ++i)
			pos += wsprintfA(buffer + pos, " %08X", dump[i]);
		wsprintfA(buffer + pos, "\n");
		OutputDebugStringA(buffer);
	}
	sdk::Cvar->unlock();
	OutputDebugStringA("Fatality: init: cvars unlocked.\n");

	OutputDebugStringA("Fatality: init: installing hooks.\n");
	hook_manager.init();
	OutputDebugStringA("Fatality: init: done.\n");
}

void game_t::unload() const
{
	OutputDebugStringA("Fatality: unloading.\n");

	// 移除全部 hook 并还原窗口过程，之后游戏线程不再进入本模块代码
	hook_manager.detach();

	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	draw_mgr.destroy_objects();
	sdk::InputSystem->set_input(true);
	evo::ren::draw.destroy_objects();
	evo::ren::draw = {};

	// 原实现先 FreeLibrary 再 FreeLibraryAndExitThread：引用计数到 0 时模块
	// 立即被卸载，后一个调用就是在执行已释放的代码，必崩。只保留后者
	//（它会在 kernel32 内部完成释放并退出线程，是文档保证的安全用法）。
	FreeLibraryAndExitThread(reinterpret_cast<HMODULE>(base), 0);
}

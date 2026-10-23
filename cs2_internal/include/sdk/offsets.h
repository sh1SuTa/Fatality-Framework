// generated on: 11/6/2023

#ifndef SDK_OFFSETS_H
#define SDK_OFFSETS_H

#include <cstdint>

namespace sdk
{
	namespace offsets
	{
		namespace interfaces
		{
			namespace tier0
			{
				constexpr auto vprocess_utils002 = 0x3a1820;
				constexpr auto vstring_token_system001 = 0x3d3300;
				constexpr auto test_script_mgr001 = 0x3a1960;
				constexpr auto vengine_cvar007 = 0x3ac670;
			} // namespace tier0

			namespace animationsystem
			{
				constexpr auto animation_system_utils_001 = 0x83f6d8;
				constexpr auto animation_system_001 = 0x8375f8;
			} // namespace animationsystem

			namespace assetsystem
			{
				constexpr auto asset_system001 = 0x4ff540;
				constexpr auto asset_system_test001 = 0x4a6118;
			} // namespace assetsystem

			namespace filesystem_stdio
			{
				constexpr auto vfile_system017 = 0x2143d0;
				constexpr auto vasync_file_system2_001 = 0x214610;
			} // namespace filesystem_stdio

			namespace imemanager
			{
				constexpr auto imemanager001 = 0x37aa0;
			} // namespace imemanager

			namespace inputsystem
			{
				constexpr auto input_system_version001 = 0x46bc0;
				constexpr auto input_stack_system_version001 = 0x44e90;
			} // namespace inputsystem

			namespace localize
			{
				constexpr auto localize_001 = 0x59120;
			} // namespace localize

			namespace materialsystem2
			{
				constexpr auto vmaterial_system2_001 = 0x163530;
				constexpr auto post_processing_system_001 = 0x14bc60;
				constexpr auto material_utils_001 = 0x14bd30;
				constexpr auto text_layout_001 = 0x14bcc0;
				constexpr auto font_manager_001 = 0x1638e0;
			} // namespace materialsystem2

			namespace meshsystem
			{
				constexpr auto mesh_system001 = 0x180ab0;
			} // namespace meshsystem

			namespace navsystem
			{
				constexpr auto nav_system001 = 0x12c000;
			} // namespace navsystem

			namespace particles
			{
				constexpr auto particle_system_mgr003 = 0x65aeb0;
			} // namespace particles

			namespace physicsbuilder
			{
				constexpr auto physics_builder_mgr001 = 0x909c20;
			} // namespace physicsbuilder

			namespace resourcesystem
			{
				constexpr auto resource_system013 = 0x892b0;
			} // namespace resourcesystem

			namespace scenefilecache
			{
				constexpr auto scene_file_cache002 = 0x11d478;
				constexpr auto response_rules_cache001 = 0x11d350;
			} // namespace scenefilecache

			namespace scenesystem
			{
				constexpr auto scene_utils_001 = 0x676760;
				constexpr auto scene_system_002 = 0x91fb20;
				constexpr auto rendering_pipelines_001 = 0x675a00;
			} // namespace scenesystem

			namespace schemasystem
			{
				constexpr auto schema_system_001 = 0x76710;
			} // namespace schemasystem

			namespace soundsystem
			{
				constexpr auto sound_op_system001 = 0x535a90;
				constexpr auto sound_op_system_edit001 = 0x5359a0;
				constexpr auto sound_system001 = 0x535350;
				constexpr auto vmix_edit_tool001_callback = 0x71740;
			} // namespace soundsystem

			namespace steamaudio
			{
				constexpr auto steam_audio001 = 0x35c1a0;
			} // namespace steamaudio

			namespace vphysics2
			{
				constexpr auto vphysics2_handle_interface_001 = 0x37e6f0;
				constexpr auto vphysics2_interface_001 = 0x460e60;
			} // namespace vphysics2

			namespace vscript
			{
				constexpr auto vscript_manager010 = 0x13e430;
			} // namespace vscript

			namespace worldrenderer
			{
				constexpr auto world_renderer_mgr001 = 0x236d00;
			} // namespace worldrenderer

			namespace client
			{
				constexpr auto source2_client002 = 0x255a3a0;
				constexpr auto legacy_game_ui001 = 0x223c0e0;
				constexpr auto empty_world_service001_client = 0x2213230;
				constexpr auto source2_client_ui001 = 0x223a960;
				constexpr auto source2_client_prediction001 = 0x255c2d0;
				constexpr auto client_tools_info_001 = 0x222f7b0;
				constexpr auto source2_client_config001 = 0x24b7240;
				constexpr auto game_client_exports001 = 0x222c458;
			} // namespace client

			namespace engine2
			{
				constexpr auto engine_game_ui001 = 0x6206e0;
				constexpr auto game_event_system_client_v001 = 0x91c9e0;
				constexpr auto simple_engine_loop_service_001 = 0x623650;
				constexpr auto game_resource_service_client_v001 = 0x622dc0;;
				constexpr auto client_server_engine_loop_service_001 = 0x91ce30;
				constexpr auto game_resource_service_server_v001 = 0x622e20;
				constexpr auto key_value_cache001 = 0x6235f0;
				constexpr auto vprof_service_001 = 0x6233f0;
				constexpr auto host_state_mgr001 = 0x623540;
				constexpr auto game_event_system_server_v001 = 0x91cb10;
				constexpr auto engine_service_mgr001 = 0x91c700;
				constexpr auto inetsupport_001 = 0x61bc30;
				constexpr auto tool_service_001 = 0x6233b0;
				constexpr auto stats_service_001 = 0x91bc80;
				constexpr auto split_screen_service_001 = 0x6232b0;
				constexpr auto sound_service_001 = 0x622fd0;
				constexpr auto screenshot_service001 = 0x91b940;
				constexpr auto game_uiservice_001 = 0x8dbc40;
				constexpr auto render_service_001 = 0x91b680;
				constexpr auto network_service_001 = 0x622f90;
				constexpr auto network_server_service_001 = 0x91b410;
				constexpr auto source2_engine_to_client_string_table001 = 0x620050;
				constexpr auto network_p2_pservice_001 = 0x91b260;
				constexpr auto network_client_service_001 = 0x91af20;
				constexpr auto map_list_service_001 = 0x91ad90;
				constexpr auto input_service_001 = 0x8dbf20;
				constexpr auto bug_service001 = 0x8db7f0;
				constexpr auto benchmark_service001 = 0x622c80;
				constexpr auto vengine_gameuifuncs_version005 = 0x620770;
				constexpr auto source2_engine_to_server_string_table001 = 0x6200f0;
				constexpr auto source2_engine_to_server001 = 0x6200c8;
				constexpr auto source2_engine_to_client001 = 0x61fff0;
			} // namespace engine2

			namespace panorama
			{
				constexpr auto panorama_uiengine001 = 0x586f60;
			} // namespace panorama

		} // namespace interfaces

		namespace functions
		{
			constexpr auto con_msg = 0x74630;
			constexpr auto con_color_msg = 0x74550;
			constexpr auto load_text_file = 0x72ff10;

			namespace mem_alloc
			{
				constexpr auto str_dup_func = 0x109eb0;
			} // namespace mem_alloc

			namespace client
			{
				constexpr auto frame_stage_notify = 0x71a620;
				constexpr auto get_weapon_data = 0x4a7f40;
				// [14191] cvar 索引→ConVar 数据（FindVar 后半段）：tools/tp_rescan2_14191.py 静态实锤
				// （函数 @0x19ACA80：out2 预置{-1,0} → vcall+0x148 单例[0x279CD58] → 兜底 idx*0x58
				// 注册表[0x22ACD40] → 写 {idx, ConVar*}；与 ConvarGet 0x19AD630 同簇位移 +0x5E20 精确一致）。
				// ConVar 布局不变（ConvarGet 14191 体仍读 flags@0x30/value@0x58/type@0x28，cvar.h 无需动）
				// [14192] 0x19ACA80 + 0x2260 = 0x19AECE0 (NOTE: the earlier 0x19ACEE0 was a one-digit
				// typo; it lands mid-instruction inside function 0x19AC8C1, which is what produced
				// exception 0xc000001d / illegal instruction at client+0x19ACEE1).
				// Disasm at 0x19AECE0 confirms the full recipe:
				// mov [rcx],0xFFFFFFFF / mov [rcx+8],0 (out) -> [rip+0xDEFFE8] (pCvar 0x279ECF8)
				// -> call [rax+0x148] -> fallback imul rsi,0x58 + registry [rip+0x900038]
				// -> writes {idx, ConVar*} and returns out. ConVar layout unchanged.
				constexpr auto cvar_from_index = 0x19aece0;
				// [14191] ClientMode::OverrideView(this, CViewSetup*)：thirdperson 摄像机驱动点
				//（tp_rescan_14191.py pattern 唯一命中 0xD02E30，14190 pattern 字节原样可用）。
				// 旧 0xD02310/0x7515d0 均过期，勿信
				constexpr auto override_view = 0xd04490; // [14192] cs2-sdk pattern 命中（旧 0xD02E30 +0x1660）
				// [14191] thirdperson/firstperson ConCommand 回调（tp_rescan_14191.py pattern 唯一命中）。
				// 回调反汇编逐条对齐 14190：无 cheat 门、状态门 +0x229 快照 +0x230/+0x234、
				// +0x238=0x41F00000(30.0f 硬编码)、+0x6A8 清零、vcall+0x9D8(1)/(0)、vcall+0x310 查询全同
				constexpr auto cmd_thirdperson = 0xb64fd0; // [14192] old 0xB64190 +0xE40
				constexpr auto cmd_firstperson = 0xb64ef0; // [14192] old 0xB640B0 +0xE40
				// [14192] 第三人称状态单例（thirdperson 回调 r8 宿主 = [client+0x223DC00]）：
				// 对象布局零变化（+0x229/+0x230/+0x234/+0x238/+0x6A8 反汇编复核）
				constexpr auto tp_state_singleton = 0x223dc00;
				constexpr auto get_camera = 0x841a70;
				constexpr auto view_render = 0x732450;
				constexpr auto on_render_start = 0x7b4060;
				// [14191 已验 2026-10-18] trace 系 RVA 全量重扫（tools/trace_rescan_14191.py，pattern+结构+RTTI 三路交叉）：
				// GameTraceLine 0xA1A9A0（pattern 唯一命中 + 0x0F00FFFF 掩码 + 核心 0xA10A20/0xA1A510 同簇 +0xEC0）
				// wrapper 0x84EDB0（GTL call @+0xEE 与 14190 同位、singleton load @+0xC0 同位、imm 0x07000000）
				// singleton 0x2227058（pGameTraceManager pattern + wrapper 引用 + 全 DLL 149 函数 xref 与 14190 精确一致）
				// attrs desc 0x1AC29C8（wrapper +0x7F 唯一 .rdata 12B 全零引用）
				// expander 0x9D2F60（TraceShape call @+0x21F 与 14190 同位 + AutowallTraceData +0x38F 交叉调用）
				// 调用配方不变：GameTraceLine(mgr, &attrs, &start, &end, nullptr, (void*)1, &out[0xC0])，
				// attrs=desc 12B 拷贝+float@+0xC=0，out 预置 0 清零+[0x6C]=-1+[0xB0]=-1，
				// fraction=[+0xAC]、hit 实体=[+0x08]、hit byte=[+0xBB]（14190 实锤沿用）
				constexpr auto game_trace_line = 0xa1b7d0; // [14192] old 0xA1A9A0 +0xE30
				// [14191] 游戏高层 line trace wrapper：bool(start_vec rcx, end_vec rdx, float xmm2=0, out_copy r9)，
				// 内部自带 out 预置(+0x74=0x07000000)+attrs 构造+GameTraceLine+结果判定
				// （1.0>frac 或 hit byte!=0 → true）。裸调 GameTraceLine 全 miss 时的对照/替代路径
				constexpr auto trace_wrapper = 0x84fbe0; // [14192] old 0x84EDB0 +0xE30（反汇编复核：call GameTraceLine/InitTraceInfo）
				// [14191] attrs 描述符（12B，.rdata 全零，wrapper 体内唯一引用）
				constexpr auto trace_attrs_desc = 0x1ac49d0; // [14192] old 0x1AC29C8（wrapper 内 lea 实读）
				// [14191] 0x48 记录→CGameTrace 展开器（TraceShape call@+0x21F → 0x9D2F60，AW +0x38F 交叉）
				constexpr auto trace_expander = 0x9d2f60;
				// [14191] 物理查询单例槽：pattern 实锤 + 149 函数 xref（与 14190 数量一致）
				constexpr auto physics_query_singleton = 0x2229020; // [14192] pGameTraceManager（wrapper 内 mov rcx,[rip+...] 实读）
				// [14191] CCSTraceFilterSimple 使用现场：唯一 lea→ccs vtable(0x1C11D00) 的函数
				// （两个构造点 @+0x287/+0x29C；簇位移 -0x250 与 0x8BE dtor 簇一致）
				constexpr auto bullet_trace_helper = 0x8c90cb;
				// [14191 未验] CGameTrace::Init（佐证位，全工程零代码引用；旧 0x17761D0 已过期，
				// 同簇 InitTraceInfo-analog 新址 0x1779300 位移 +0x5E20 可作下轮线索）
				constexpr auto cgame_trace_init = 0x17761d0;
			} // namespace client

			// [aw] client.dll .rdata filter vtable RVA（RTTI 核验；[14191 重扫 2026-10-18]：
			// TD 在 .data，vtable[-1]=烧录 VA(imagebase 0x180000000)+COL，全名匹配唯一命中）
			namespace trace_vtables
			{
				constexpr auto ctrace_filter = 0x1ad4838;        // 旧 1AD1848 +0x2FF0
				constexpr auto ccs_trace_filter_simple = 0x1c11d00; // 旧 1C0EF50 +0x2DB0
				constexpr auto no_npcs_or_player = 0x1bbbd60;    // 旧 1BB8F80 +0x2DE0
				constexpr auto player_movement_cs = 0x1c10f30;   // 旧 1C0E180 +0x2DB0
				constexpr auto entity_sweep = 0x1c2e9c8;         // 旧 1C2BBE8 +0x2DE0
				constexpr auto omit_players = 0x1c87c98;         // 旧 1C84EA8 +0x2DF0
				constexpr auto no_combat_characters = 0x1c4ada8; // 旧 1C47FC8 +0x2DE0
			} // namespace trace_vtables

			// [aw] 全部 trace filter 的 scalar-deleting-dtor RVA（vtable slot0；[14191 重扫 2026-10-18]
			// 由新 vtable 直接取 slot0，VA 烧录已减 imagebase；同簇 dtor +0x30 间距与 14190 一致）。
			// 栈上 filter 在 trace 函数返回前必析构一次 —— dtor 钩 + _ReturnAddress 反查开火
			// trace 函数本体。顺序=hooks 侧 aw_filter_dtor_id。
			namespace filter_dtors
			{
				constexpr auto trace_filter = 0x205cb0;              // CTraceFilter（旧 205A10，背景探针也用，热）
				constexpr auto ccs_simple = 0x8bead0;                // CCSTraceFilterSimple（旧 8BED20，客户端零构造=死）
				constexpr auto no_npcs_or_player = 0x782a30;         // 旧 783620
				constexpr auto player_movement_cs = 0x8beb30;        // 旧 8BED80
				constexpr auto entity_sweep = 0x9c8370;              // 旧 9C8550
				constexpr auto entity_push = 0x9c8340;               // 旧 9C8520
				constexpr auto omit_players = 0xc62520;              // 旧 C62300
				constexpr auto no_combat_characters = 0xae2e20;      // 旧 AE2C00
				constexpr auto knife_ignore_teammates = 0x82c790;    // 旧 82CFA0
				constexpr auto taser_ignore_teammates = 0x82c7c0;    // 旧 82CFD0
				constexpr auto for_player_head_collision = 0x8beb00; // 旧 8BED50
			} // namespace filter_dtors

			namespace sdl3
			{
				constexpr auto set_relative_mouse_mode = 0x20030;
				constexpr auto set_window_grab = 0x20210;
				constexpr auto warp_mouse_in_window = 0x20690;
			} // namespace sdl3

			namespace gameoverlayrenderer
			{
				constexpr auto present = 0x8b2a0;
			} // namespace gameoverlayrenderer

			namespace cgame_entity_system
			{
				constexpr auto get_entity_by_index = 0x60afe0;
			} // namespace cgame_entity_system

			namespace input
			{
				constexpr auto get_view_angles = 0x74bd10;
			constexpr auto set_view_angles = 0x756060;
			// [14191 未验] 运行时走 safe_vfunc(sdk::Input, 11) 自适应钩装（槽 11 目标 14191 静态
			// 定位未果——指纹两候选均无主虚表槽 11 引用；注入日志 "hook mouse_input_enabled
			// target=%p" 的 RVA 即当前槽 11 实况，有异常贴回），此值仅存档
			constexpr auto mouse_input_enabled = 0x878390;
			// [14191] create_move（dump 崩溃复盘实锤 2026-10-18）：真入口 0xD02130，头为 14190 同款
			// 真序言（mov rax,rsp / [rax+18],r8 / [rax+8],rcx / push rbp），且位移 +0xB20 与
			// override_view（0xD02310→0xD02E30，pattern 独立验证）精确一致 —— 本簇整体平移。
			// 注意：0xD02780 是 create_move 中段碎片（0xD022E6 的 `cmp ebx,4; jle 0xD02780` 跳入的
			// EH 块），钩它会 mid-function 断帧 → 毒栈传入 fill → C0000005（已踩坑，勿再钩）
			constexpr auto csgoinput_create_move = 0xd03790;
			// [14192] old 0xCFA6B0 +0x1660：WriteSubtickFromEntry（cs2-sdk pattern 命中 0xCFBD10）：
			// 命令视角填充（rcx=视角快照，rdx=上下文，[rdx+18h]=CUserCmd）语义沿用
			constexpr auto csgoinput_fill_cmd_angles = 0xcfbd10;
			// [14192] dwCSGOInput 同址（a2x dwCSGOInput 键）
			constexpr auto csgoinput_ctx_global = 0x257d780;
			// [14192] old 0xA66F10 +0xE30：ForceButtonsDown（cs2-sdk pattern 命中 0xA67D40）：
			// 头部 cmp [rcx+0x38] 设备句柄检查与旧版语义一致；a1=64 位按钮位集注入方案沿用
			constexpr auto csgoinput_force_buttons_down = 0xa67d40;

				// ===== pattern 表备查（cs2-sdk 14190 实测 RVA，未接线；autofire/autowall 下一步钥匙）=====
				// 外层 CreateMove（input-stack 层）= 0xB64E60：其 call 目标 = GetCUserCmdArray
				// 0x943800 / GetCUserCmdBySequenceNumber 0x9435E0 / GetUserCmdManager 0x943670
				// 按钮与命令族：ReadFrameInput 0xD04A20（紧邻内层 create_move 簇）、
				// ProcessSubTickInput 0xB5F7B0、CreateNewSubtickMoveStep 0x4F7120、ForceButtonsDown 0xA66050
				// autowall/trace 族：AutowallTraceData 0xA18FD0、TraceShape 0xA19140、
				// TraceHandleBulletPen 0x8A0960、TestSurfaces 0x884860、GetSurfaceData 0x9E26A0、
				// CSBaseGunFireData 0x15F8720、ProcessImpacts 0xA651B0、TracePlayerBBox 0x8DF8E0、
				// pGameTraceManager 0x221F058（.data）
				// 其他：OverrideView 0xD02310（14190 新位）、pCvar 0x27939D8（.data）、
				// GetLocalPlayerController 0x9699F0 / GetLocalPawn 0x969A30（函数版）
				// =================================================================================
			} // namespace input

			namespace inputsystem
			{
				constexpr auto vk_to_button_code = 0x3d50;
				constexpr auto wnd_proc = 0x34a0;
				constexpr auto set_cursor_pos = 0x54e0;
			} // namespace inputsystem

			namespace cgame_ui_funcs
			{
				constexpr auto get_binding_for_button_code = 0x118030;
			} // namespace cgame_ui_funcs

			namespace utlbuffer
			{
				constexpr auto ctor = 0x180550;
			} // namespace utlbuffer

			namespace panorama
			{
				constexpr auto load_svg = 0x107810;
			} // namespace panorama

			namespace baseentity
			{
				constexpr auto get_abs_origin = 0x149f60;
			} // namespace baseentity

			namespace cameramanager
			{
				constexpr auto set_fov = 0x5136a0;
			} // namespace cameramanager

			namespace proto
			{
				constexpr auto repeated_ptr_field_push = 0xc6b000;
				constexpr auto subtick_movestep_ctor = 0x25f890;
			} // namespace proto

			namespace logging_system
			{
				constexpr auto set_channel_verbosity = 0x106900;
				constexpr auto log_direct = 0x106db0;
				constexpr auto register_logging_channel = 0x105bb0;
				constexpr auto find_channel = 0x106580;
			} // namespace logging_system

		} // namespace functions

		namespace globals
		{
			// 14192 快照（cs2-dumper 2026-10-23 11:21 UTC，src/output/offsets.hpp 逐键对应）
			constexpr auto mem_alloc = 0x368d98; // [未验] 新 dump 无对应键；当前无代码引用，仅存档
			constexpr auto global_vars = 0x2232088; // dwGlobalVars
			constexpr auto ccsgo_input = 0x257d780; // dwCSGOInput
			constexpr auto game_entity_system = 0x271c8b8; // dwGameEntitySystem
			constexpr auto local_player_controller = 0x253eef8; // dwLocalPlayerController
			constexpr auto screen_transform = 0x256cfe0; // == dwViewMatrix
			constexpr auto view_render = 0x256d9a8; // dwViewRender
			constexpr auto panorama_ui = 0x18f1c40; // [14191 未验] 新 dump 无对应键；ui_engine 赋值后无消费者，死值无害
			constexpr auto ui_event_with_priority_vtable = 0x130c350; // [14191 未验] 无代码引用
		} // namespace globals

		namespace cgame_resource_service
		{
			constexpr auto game_entity_system = 0x58;
		} // namespace cgame_resource_service

		namespace cgame_entity_system
		{
			constexpr auto last_entity_index = 0x2120;
		} // namespace cgame_entity_system

		namespace cinput_system
		{
			constexpr auto relative_mouse_mode = 0x4f;
			constexpr auto sdl_window = 0x2670;
		} // namespace cinput_system

		namespace ccsgo_input
		{
			// [已证伪] current_cmd_base_ptr=0x2598：as-hdr dump 显示目标是武器图标信息结构
			// （"icons\equipment\knife_t" 字符串 + vtable），非命令对象，勿用。
			// 活命令正确通道 = fill_cmd_angles 钩 [a1+0x18]（silent aim 实证）。
			constexpr auto current_command = 0x6034;
			constexpr auto frame_command = 0x61e0;
			constexpr auto input_msg_frame_slots = 0x6330;
			// build 14188 实测（2026-10-05 自扫描三帧 diff 实锤）：CCSGOInput+0x688
			// = 当前视角 vec3(pitch,yaw,roll)，每帧随鼠标实时更新。
			// 旧头里的 angViewAngles 深偏移及 get/set_view_angles RVA 全系 2023 过期。
			constexpr auto view_angles = 0x688;
		} // namespace ccsgo_input

	} // namespace offsets

} // namespace sdk

#endif // SDK_OFFSETS_H

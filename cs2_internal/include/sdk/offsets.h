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
				constexpr auto source2_client_prediction001 = 0x25605a0;
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
				constexpr auto override_view = 0x7515d0;
				constexpr auto get_camera = 0x841a70;
				constexpr auto view_render = 0x732450;
				constexpr auto on_render_start = 0x7b4060;
				// [aw] 同步 trace 漏斗：5 个查询入口（TraceShape 族）共用（2026-10-05 server 对称分析）
				constexpr auto trace_funnel = 0xa1a890;
				// [aw] 0x48 记录→CGameTrace 展开器（surface 拷贝发生处，佐证 RVA）
				constexpr auto trace_expander = 0x9d31a0;
				// [aw] 自调用同步 trace 配方（0x80CE65 现场实锤 2026-10-05）：
				// CTraceFilter 栈构造（返回属性结构，qdesc=16B 拷贝+flag@+0x28）、
				// .data 描述符全局、物理查询单例槽（149 函数共读；diag a0=其值）
				constexpr auto trace_filter_ctor = 0x203d00;
				constexpr auto trace_filter_desc = 0x1abf9c8;
				constexpr auto physics_query_singleton = 0x2223148;
				// [aw] CCSTraceFilterSimple 使用现场 —— 本身即函数起始（.pdata 实证
				// 0x8C931B..0x8C962B，首指令取该 filter vtable=栈上构造 filter 的经典模式），
				// "用 CS filter 做 trace" 的封装函数，子弹链路必经（第三捕获点）
				constexpr auto bullet_trace_helper = 0x8c931b;
				// [aw] CGameTrace::Init（佐证 RVA）
				constexpr auto cgame_trace_init = 0x17761d0;
			} // namespace client

			// [aw] client.dll .rdata filter vtable RVA（RTTI 核验，2026-10-04）
			namespace trace_vtables
			{
				constexpr auto ctrace_filter = 0x1ad1848;
				constexpr auto ccs_trace_filter_simple = 0x1c0ef50;
				constexpr auto no_npcs_or_player = 0x1bb8f80;
				constexpr auto player_movement_cs = 0x1c0e180;
				constexpr auto entity_sweep = 0x1c2bbe8;
				constexpr auto omit_players = 0x1c84ea8;
				constexpr auto no_combat_characters = 0x1c47fc8;
			} // namespace trace_vtables

			// [aw] 全部 trace filter 的 scalar-deleting-dtor RVA（vtable slot0，2026-10-05 静态解析）。
			// 栈上 filter 在 trace 函数返回前必析构一次 —— dtor 钩 + _ReturnAddress 反查开火
			// trace 函数本体。顺序=hooks 侧 aw_filter_dtor_id。
			namespace filter_dtors
			{
				constexpr auto trace_filter = 0x205a10;              // CTraceFilter（背景探针也用，热）
				constexpr auto ccs_simple = 0x8bed20;                // CCSTraceFilterSimple（客户端零构造=死）
				constexpr auto no_npcs_or_player = 0x783620;
				constexpr auto player_movement_cs = 0x8bed80;
				constexpr auto entity_sweep = 0x9c8550;
				constexpr auto entity_push = 0x9c8520;
				constexpr auto omit_players = 0xc62300;
				constexpr auto no_combat_characters = 0xae2c00;
				constexpr auto knife_ignore_teammates = 0x82cfa0;
				constexpr auto taser_ignore_teammates = 0x82cfd0;
				constexpr auto for_player_head_collision = 0x8bed50;
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
				constexpr auto mouse_input_enabled = 0x878390;
				constexpr auto csgoinput_create_move = 0xd01b20; // IDA 对照序言：mov rax,rsp / [rax+18],r8 / sub rsp,1D8（build 14188，0x740e50 系误配）
				// [silent] 命令视角填充（2026-10-05 用户 IDA+FPU 断点+NOP 实验实锤）：
				// sub_CFA140，create_move+5A4 与 sub_CDEC40+87 调用。rcx=视角快照
				// (+0x10 pitch/+0x14 yaw/+0x18 roll)，rdx=上下文，[rdx+18h]=CUserCmd
				// （+10h dirty 位图、+18h/1Ch/20h=viewangles）。NOP 两条 movss → 射向 0/0。
				constexpr auto csgoinput_fill_cmd_angles = 0xcfa140;
				// [btn2] create_move 反汇编实锤的全局单例（.data qword_24C4FA0，r15 宿主对象）：
				// [gs+0xBD0]=0x60 步长输入样本表，gs 的 +0x50..0x5C/+0xBC0/+0xBC4 为状态字段，
				// byte[gs+0x58] 在 D01F52/D02060 两处被当按钮态读 —— autofire 头号直写候选
				constexpr auto csgoinput_ctx_global = 0x24c4fa0;
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
			constexpr auto mem_alloc = 0x368d98;
			constexpr auto global_vars = 0x222be98;
			constexpr auto ccsgo_input = 0x2576150;
			constexpr auto game_entity_system = 0x2715818;
			constexpr auto local_player_controller = 0x2538008;
			constexpr auto screen_transform = 0x2566910;
			constexpr auto view_render = 0x2565d20;
			constexpr auto panorama_ui = 0x18f1c40;
			constexpr auto ui_event_with_priority_vtable = 0x130c350;
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

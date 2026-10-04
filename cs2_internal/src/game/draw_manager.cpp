#include <ranges>
#include <fstream>
#include <game/draw_manager.h>
#include <sdk/client.h>
#include <sdk/panorama.h>
#include "tinyformat.h"

draw_manager_t draw_mgr;

using namespace evo::ren;

void draw_manager_t::destroy_objects()
{
	for (auto &tex : textures | std::views::values)
		if (tex.tex)
			tex.tex->destroy();
}

void draw_manager_t::create_objects()
{
	const auto buf_copy = textures;
	textures.clear();

	for (const auto &tex : buf_copy | std::views::values)
		if (tex.name != invalid_name)
			get_svg_texture(tex.name, tex.height);
		else
			get_panorama_texture(tex.path, tex.height);

}

void draw_manager_t::add_icon(
	const vec2 &pos, const color &col, const std::shared_ptr<texture> &texture, const std::optional<text_params> params, const bool dropshadow) const
{
	if (!texture)
		return;

	const auto sz = texture->get_size();
	auto p = vec2{pos.x, pos.y};

	if (params)
	{
		if (params->h == align_right)
			p.x -= sz.x;
		else if (params->h == align_center)
			p.x -= sz.x * .5f;

		if (params->v == align_bottom)
			p.y -= sz.y;
		else if (params->v == align_center)
			p.y -= sz.y * .5f;
	}

	buf->g.texture = texture->obj;
	if (dropshadow)
		buf->add_rect_filled(rect(p).size(sz).translate({1.f, 1.f}), color::black().mod_a(.6f * col.value.a));
	buf->add_rect_filled(rect(p).size(sz), col);
	buf->g.texture = {};
}

std::shared_ptr<texture> draw_manager_t::load_svg_to_texture(uint8_t *data, size_t sz, uint32_t w, uint32_t h)
{
	std::vector<uint8_t> texture(0xFFFFFF);
	sdk::image_data_t img(texture);

	if (img.load_svg(data, sz, &w, &h) && w && h)
	{
		auto tex = std::make_shared<evo::ren::texture>(texture.data(), w, h, w * 4);
		tex->create();
		return tex;
	}

	return nullptr;
}

std::shared_ptr<texture> draw_manager_t::get_panorama_texture(const std::string &path, const uint32_t target_height)
{
	const auto hash = utils::fnv1a(path.c_str()) + target_height;
	auto ico = textures.find(hash);
	if (ico == textures.end())
	{
		// 资产为预光栅化的 RGBA 原始数据（tools/rasterize_icons.py 生成，格式：uint32 w + uint32 h + RGBA8）。
		// 原实现走 image_data_t::load_svg → panorama::load_svg / utlbuffer::ctor 游戏函数，
		// 偏移未核验（fail-fast 风险，曾致闪退），故解码整体移至构建期
		const auto data = read_asset(path + ".raw");
		if (data.size() > 8)
		{
			const auto w = *reinterpret_cast<const uint32_t *>(data.data());
			const auto h = *reinterpret_cast<const uint32_t *>(data.data() + 4);
			if (w && h && data.size() >= 8 + static_cast<size_t>(w) * h * 4)
			{
				// texture 的 rgba 构造不拷贝数据，只存指针 —— data(string) 在 create() 消费期间必须存活
				const auto tex = std::make_shared<texture>(reinterpret_cast<void *>(const_cast<char *>(data.data()) + 8), w, h, w * 4);
				tex->create();
				ico = textures.insert_or_assign(hash, icon_info_t{invalid_name, path, target_height, tex}).first;
			}
			else
				OutputDebugStringA(("Fatality: icon: bad data " + path + "\n").c_str());
		}
		else
			OutputDebugStringA(("Fatality: icon: no file " + path + "\n").c_str());

		// 加载失败也缓存 null 纹理，避免每帧文件 IO
		if (ico == textures.end())
			ico = textures.insert_or_assign(hash, icon_info_t{invalid_name, path, target_height, nullptr}).first;
	}

	return ico->second.tex;
}

std::string draw_manager_t::read_asset(const std::string &path)
{
	// 资产目录 = 本 DLL 所在目录 \assets\（static 只推导一次）
	static const std::string root = []()
	{
		// 用本模块内的全局对象地址反查自身模块句柄
		HMODULE self{};
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&draw_mgr), &self);
		char buf[MAX_PATH]{};
		GetModuleFileNameA(self, buf, MAX_PATH);
		std::string dir{buf};
		const auto r = dir.substr(0, dir.find_last_of('\\')) + "\\assets\\";
		OutputDebugStringA(("Fatality: assets root=" + r + "\n").c_str());
		return r;
	}();

	std::ifstream f(root + path, std::ios::binary);
	if (!f)
		return {};

	return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::shared_ptr<texture> draw_manager_t::get_svg_texture(const uint32_t name, const uint32_t target_height)
{
	if (!svg_icons.contains(name))
		return nullptr;

	const auto hash = name + target_height;
	auto ico = textures.find(hash);
	if (ico == textures.end())
	{
		DEC(svg, svg_icons[name]);
		std::vector<uint8_t> texture(0xFFFFFF);
		sdk::image_data_t img(texture);
		uint32_t w{}, h{};
		if (!img.load_svg((uint8_t *)svg.data(), svg.size(), &w, &h))
			return nullptr;

		const auto width_to_height = static_cast<float>(w) / static_cast<float>(h);
		const auto tex = load_svg_to_texture(
			(uint8_t *)svg.data(), svg.size(), target_height ? static_cast<uint32_t>(width_to_height * target_height) : 0, target_height);
		if (!tex)
			return nullptr;

		ico = textures.insert_or_assign(hash, icon_info_t{name, {}, target_height, tex}).first;
	}

	return ico->second.tex;
}

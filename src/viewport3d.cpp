/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file viewport3d.cpp Stage-3 GL renderer: ground heightfield mesh with a free orbit camera. */

#include "stdafx.h"
#include "core/camera3d.hpp"
#include "landscape.h"
#include "map_func.h"
#include "map_type.h"
#include "landscape_type.h"
#include "clear_map.h"
#include "settings_type.h"
#include "tile_map.h"
#include "viewport_type.h"
#include "viewport_drawer.h"
#include "window_type.h"
#include "zoom_func.h"
#include "spritecache.h"
#include "sprite.h"
#include "blitter/32bpp_sse2.hpp"

#include <unordered_map>

#include <GL/glcorearb.h>
#include <GL/glext.h>
#include <SDL.h>

#include "debug.h"
#include "safeguards.h"

/** Number of tiles rendered around the camera target. */
static constexpr int ORBIT_RADIUS_TILES = 60;
/** World size of one tile (matches RemapCoords units). */
static constexpr int TILE_WORLD = TILE_SIZE;
/** World height of one height level. */
static constexpr int HEIGHT_WORLD = TILE_HEIGHT;

/* --- GL function bindings (loaded once via SDL_GL_GetProcAddress) --- */

#define GL_FUNC(name, type) static type p_##name = nullptr;
GL_FUNC(glGenBuffers, PFNGLGENBUFFERSPROC)
GL_FUNC(glBindBuffer, PFNGLBINDBUFFERPROC)
GL_FUNC(glBufferData, PFNGLBUFFERDATAPROC)
GL_FUNC(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC)
GL_FUNC(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC)
GL_FUNC(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC)
GL_FUNC(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)
GL_FUNC(glCreateShader, PFNGLCREATESHADERPROC)
GL_FUNC(glShaderSource, PFNGLSHADERSOURCEPROC)
GL_FUNC(glCompileShader, PFNGLCOMPILESHADERPROC)
GL_FUNC(glGetShaderiv, PFNGLGETSHADERIVPROC)
GL_FUNC(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)
GL_FUNC(glCreateProgram, PFNGLCREATEPROGRAMPROC)
GL_FUNC(glAttachShader, PFNGLATTACHSHADERPROC)
GL_FUNC(glLinkProgram, PFNGLLINKPROGRAMPROC)
GL_FUNC(glGetProgramiv, PFNGLGETPROGRAMIVPROC)
GL_FUNC(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)
GL_FUNC(glUseProgram, PFNGLUSEPROGRAMPROC)
GL_FUNC(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)
GL_FUNC(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC)
GL_FUNC(glViewport, PFNGLVIEWPORTPROC)
GL_FUNC(glClear, PFNGLCLEARPROC)
GL_FUNC(glClearColor, PFNGLCLEARCOLORPROC)
GL_FUNC(glEnable, PFNGLENABLEPROC)
GL_FUNC(glDisable, PFNGLDISABLEPROC)
GL_FUNC(glDrawArrays, PFNGLDRAWARRAYSPROC)
GL_FUNC(glReadPixels, PFNGLREADPIXELSPROC)
GL_FUNC(glDeleteShader, PFNGLDELETESHADERPROC)
GL_FUNC(glDeleteProgram, PFNGLDELETEPROGRAMPROC)
GL_FUNC(glGenTextures, PFNGLGENTEXTURESPROC)
GL_FUNC(glBindTexture, PFNGLBINDTEXTUREPROC)
GL_FUNC(glTexImage2D, PFNGLTEXIMAGE2DPROC)
GL_FUNC(glTexParameteri, PFNGLTEXPARAMETERIPROC)
GL_FUNC(glActiveTexture, PFNGLACTIVETEXTUREPROC)
GL_FUNC(glUniform1i, PFNGLUNIFORM1IPROC)
GL_FUNC(glBlendFunc, PFNGLBLENDFUNCPROC)
GL_FUNC(glPixelStorei, PFNGLPIXELSTOREIPROC)
#undef GL_FUNC

static bool _gl_loaded = false;
static bool _gl_ok = false;

static bool LoadGL()
{
	if (_gl_loaded) return _gl_ok;
	_gl_loaded = true;
#define GL_FUNC(name) \
	p_##name = reinterpret_cast<decltype(p_##name)>(SDL_GL_GetProcAddress(#name)); \
	if (p_##name == nullptr) return false;
	GL_FUNC(glGenBuffers) GL_FUNC(glBindBuffer) GL_FUNC(glBufferData)
	GL_FUNC(glGenVertexArrays) GL_FUNC(glBindVertexArray) GL_FUNC(glEnableVertexAttribArray) GL_FUNC(glVertexAttribPointer)
	GL_FUNC(glCreateShader) GL_FUNC(glShaderSource) GL_FUNC(glCompileShader) GL_FUNC(glGetShaderiv) GL_FUNC(glGetShaderInfoLog)
	GL_FUNC(glCreateProgram) GL_FUNC(glAttachShader) GL_FUNC(glLinkProgram) GL_FUNC(glGetProgramiv) GL_FUNC(glGetProgramInfoLog)
	GL_FUNC(glUseProgram) GL_FUNC(glGetUniformLocation) GL_FUNC(glUniformMatrix4fv)
	GL_FUNC(glViewport) GL_FUNC(glClear) GL_FUNC(glClearColor) GL_FUNC(glEnable) GL_FUNC(glDisable)
	GL_FUNC(glDrawArrays) GL_FUNC(glReadPixels) GL_FUNC(glDeleteShader) GL_FUNC(glDeleteProgram)
	GL_FUNC(glGenTextures) GL_FUNC(glBindTexture) GL_FUNC(glTexImage2D) GL_FUNC(glTexParameteri) GL_FUNC(glActiveTexture) GL_FUNC(glUniform1i)
	GL_FUNC(glBlendFunc) GL_FUNC(glPixelStorei)
#undef GL_FUNC
	_gl_ok = true;
	return true;
}

/* --- Shader --- */

static GLuint _program = 0;
static GLint _u_mvp = -1;
static GLuint _bill_program = 0;
static GLint _u_mvp_bill = -1;
static GLint _u_tex_bill = -1;

static bool CompileShader()
{
	const char *vs_src =
		"#version 330 core\n"
		"layout(location = 0) in vec3 a_pos;\n"
		"layout(location = 1) in vec3 a_col;\n"
		"uniform mat4 u_mvp;\n"
		"out vec3 v_col;\n"
		"void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); v_col = a_col; }\n";
	const char *fs_src =
		"#version 330 core\n"
		"in vec3 v_col;\n"
		"out vec4 frag;\n"
		"void main() { frag = vec4(v_col, 1.0); }\n";

	GLuint vs = p_glCreateShader(GL_VERTEX_SHADER);
	p_glShaderSource(vs, 1, &vs_src, nullptr);
	p_glCompileShader(vs);
	GLint ok = 0;
	p_glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		p_glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
		Debug(misc, 0, "viewport3d: vertex shader error: {}", log);
		return false;
	}
	GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER);
	p_glShaderSource(fs, 1, &fs_src, nullptr);
	p_glCompileShader(fs);
	p_glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		p_glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
		Debug(misc, 0, "viewport3d: fragment shader error: {}", log);
		return false;
	}
	_program = p_glCreateProgram();
	p_glAttachShader(_program, vs);
	p_glAttachShader(_program, fs);
	p_glLinkProgram(_program);
	p_glGetProgramiv(_program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		p_glGetProgramInfoLog(_program, sizeof(log), nullptr, log);
		Debug(misc, 0, "viewport3d: program link error: {}", log);
		return false;
	}
	p_glDeleteShader(vs);
	p_glDeleteShader(fs);
	_u_mvp = p_glGetUniformLocation(_program, "u_mvp");

	/* Billboard shader: textured quads (camera-facing sprites). */
	const char *bvs_src =
		"#version 330 core\n"
		"layout(location = 0) in vec3 a_pos;\n"
		"layout(location = 1) in vec2 a_uv;\n"
		"uniform mat4 u_mvp;\n"
		"out vec2 v_uv;\n"
		"void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); v_uv = a_uv; }\n";
	const char *bfs_src =
		"#version 330 core\n"
		"in vec2 v_uv;\n"
		"uniform sampler2D u_tex;\n"
		"out vec4 frag;\n"
		"void main() { vec4 t = texture(u_tex, v_uv); if (t.a < 0.5) discard; frag = vec4(t.rgb, 1.0); }\n";
	GLuint bvs = p_glCreateShader(GL_VERTEX_SHADER);
	p_glShaderSource(bvs, 1, &bvs_src, nullptr);
	p_glCompileShader(bvs);
	p_glGetShaderiv(bvs, GL_COMPILE_STATUS, &ok);
	if (!ok) return false;
	GLuint bfs = p_glCreateShader(GL_FRAGMENT_SHADER);
	p_glShaderSource(bfs, 1, &bfs_src, nullptr);
	p_glCompileShader(bfs);
	p_glGetShaderiv(bfs, GL_COMPILE_STATUS, &ok);
	if (!ok) return false;
	_bill_program = p_glCreateProgram();
	p_glAttachShader(_bill_program, bvs);
	p_glAttachShader(_bill_program, bfs);
	p_glLinkProgram(_bill_program);
	p_glGetProgramiv(_bill_program, GL_LINK_STATUS, &ok);
	if (!ok) return false;
	p_glDeleteShader(bvs);
	p_glDeleteShader(bfs);
	_u_mvp_bill = p_glGetUniformLocation(_bill_program, "u_mvp");
	_u_tex_bill = p_glGetUniformLocation(_bill_program, "u_tex");
	return true;
}

/** Cache of uploaded sprite textures, keyed by sprite id. */
static std::unordered_map<SpriteID, GLuint> _sprite_textures;

/**
 * Upload (and cache) a sprite as a GL texture, using the RGBA mip-map data
 * of the current zoom level from the active SSE blitter.
 */
static GLuint GetSpriteTexture(SpriteID sprite, ZoomLevel zoom)
{
	const auto it = _sprite_textures.find(sprite);
	if (it != _sprite_textures.end()) return it->second;

	const Sprite *spr = GetSprite(sprite, SpriteType::Normal);
	if (spr == nullptr || spr->width == 0 || spr->height == 0) return 0;
	const auto *sd = reinterpret_cast<const Blitter_32bppSSE_Base::SpriteData *>(spr->data);
	const auto &si = sd->infos[zoom];
	const int tw = si.sprite_width;
	const int th = UnScaleByZoom(spr->height, zoom);
	if (tw <= 0 || th <= 0) return 0;

	GLuint tex = 0;
	p_glGenTextures(1, &tex);
	p_glBindTexture(GL_TEXTURE_2D, tex);
	p_glPixelStorei(GL_UNPACK_ROW_LENGTH, si.sprite_line_size / 4);
	p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_BGRA, GL_UNSIGNED_BYTE, sd->data + si.sprite_offset);
	p_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_sprite_textures.emplace(sprite, tex);
	return tex;
}

/* --- Ground colours --- */

static Vec3 GroundColor(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case TileType::Water:   return { 0.22f, 0.42f, 0.72f };
		case TileType::Clear:
			switch (GetClearGround(tile)) {
				case ClearGround::Grass:    return { 0.40f, 0.60f, 0.28f };
				case ClearGround::Rough:    return { 0.34f, 0.52f, 0.26f };
				case ClearGround::Rocks:    return { 0.52f, 0.50f, 0.45f };
				case ClearGround::Fields:   return { 0.68f, 0.60f, 0.28f };
				case ClearGround::Desert:   return { 0.82f, 0.75f, 0.42f };
				default:                    return { 0.40f, 0.60f, 0.28f };
			}
		case TileType::Trees:  return { 0.28f, 0.46f, 0.20f };
		case TileType::Railway: return { 0.42f, 0.40f, 0.36f };
		case TileType::Road:   return { 0.38f, 0.36f, 0.33f };
		case TileType::House:  return { 0.58f, 0.53f, 0.48f };
		case TileType::Industry: return { 0.52f, 0.48f, 0.44f };
		default:               return { 0.48f, 0.48f, 0.48f };
	}
}

/* --- Mesh buffer --- */

static GLuint _vao = 0, _vbo = 0;
static GLuint _bill_vao = 0, _bill_vbo = 0;
static std::vector<float> _mesh_data;

/**
 * Render the main viewport with the stage-3 GL renderer: the ground as a
 * coloured heightfield mesh from the orbit camera. The result is read back
 * into the software screen buffer so screenshots and presentation keep
 * working.
 */
void RenderViewport3D(const Viewport &vp, const DrawPixelInfo &dpi)
{
	if (!LoadGL()) return;
	if (!CompileShader()) return;

	const int width = UnScaleByZoom(dpi.width, dpi.zoom);
	const int height = UnScaleByZoom(dpi.height, dpi.zoom);
	if (width <= 0 || height <= 0) return;
	const ZoomLevel zoom = dpi.zoom;

	/* --- Camera: target = tile under the viewport centre --- */
	Point centre = InverseRemapCoords(vp.virtual_left + vp.virtual_width / 2, vp.virtual_top + vp.virtual_height / 2);
	Camera3D cam;
	cam.target = { static_cast<float>(centre.x), static_cast<float>(centre.y), static_cast<float>(TileHeight(TileXY(Clamp(centre.x / TILE_WORLD, 0, Map::SizeX() - 1), Clamp(centre.y / TILE_WORLD, 0, Map::SizeY() - 1))) * HEIGHT_WORLD) };
	cam.distance = 3200.0f;
	cam.yaw = 45.0f;
	cam.pitch = 5.0f + _settings_client.gui.three_d_pitch * 0.5f;
	cam.aspect = static_cast<float>(width) / static_cast<float>(height);

	/* --- Build the heightfield mesh --- */
	_mesh_data.clear();
	const int cx = Clamp(centre.x / TILE_WORLD, 0, Map::SizeX() - 1);
	const int cy = Clamp(centre.y / TILE_WORLD, 0, Map::SizeY() - 1);
	const int x0 = std::max(0, cx - ORBIT_RADIUS_TILES);
	const int x1 = std::min(static_cast<int>(Map::SizeX()), cx + ORBIT_RADIUS_TILES);
	const int y0 = std::max(0, cy - ORBIT_RADIUS_TILES);
	const int y1 = std::min(static_cast<int>(Map::SizeY()), cy + ORBIT_RADIUS_TILES);

	for (int ty = y0; ty < y1; ty++) {
		for (int tx = x0; tx < x1; tx++) {
			const TileIndex tile = TileXY(tx, ty);
			auto [slope, z] = GetTileSlopeZ(tile);
			const Vec3 col = GroundColor(tile);
			const float wx = static_cast<float>(tx * TILE_WORLD);
			const float wy = static_cast<float>(ty * TILE_WORLD);
			const float zn = static_cast<float>(z + ((slope & SLOPE_N) ? HEIGHT_WORLD : 0));
			const float ze = static_cast<float>(z + ((slope & SLOPE_E) ? HEIGHT_WORLD : 0));
			const float zs = static_cast<float>(z + ((slope & SLOPE_S) ? HEIGHT_WORLD : 0));
			const float zw = static_cast<float>(z + ((slope & SLOPE_W) ? HEIGHT_WORLD : 0));
			const float ts = static_cast<float>(TILE_WORLD);
			/* Two triangles: (N, E, S) and (N, S, W). */
			const Vec3 verts[4] = {
				{ wx,      wy,      zn }, /* N */
				{ wx + ts, wy,      ze }, /* E */
				{ wx + ts, wy + ts, zs }, /* S */
				{ wx,      wy + ts, zw }, /* W */
			};
			const int idx[6] = { 0, 1, 2, 0, 2, 3 };
			for (int i = 0; i < 6; i++) {
				const Vec3 &v = verts[idx[i]];
				_mesh_data.push_back(v.x);
				_mesh_data.push_back(v.y);
				_mesh_data.push_back(v.z);
				_mesh_data.push_back(col.x);
				_mesh_data.push_back(col.y);
				_mesh_data.push_back(col.z);
			}
		}
	}
	if (_mesh_data.empty()) return;

	if (_vao == 0) {
		p_glGenVertexArrays(1, &_vao);
		p_glGenBuffers(1, &_vbo);
	}
	p_glBindVertexArray(_vao);
	p_glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	p_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_mesh_data.size() * sizeof(float)), _mesh_data.data(), GL_DYNAMIC_DRAW);
	p_glEnableVertexAttribArray(0);
	p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
	p_glEnableVertexAttribArray(1);
	p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));

	/* --- Render --- */
	p_glViewport(0, 0, width, height);
	p_glClearColor(0.55f, 0.70f, 0.95f, 1.0f); /* sky */
	p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	p_glEnable(GL_DEPTH_TEST);
	p_glDisable(GL_CULL_FACE);

	const Mat4 mvp = cam.ViewProjMatrix();
	p_glUseProgram(_program);
	p_glUniformMatrix4fv(_u_mvp, 1, GL_FALSE, mvp.m);
	p_glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(_mesh_data.size() / 6));

	/* --- Billboards: camera-facing quads for the collected parent sprites --- */
	p_glUseProgram(_bill_program);
	p_glUniformMatrix4fv(_u_mvp_bill, 1, GL_FALSE, mvp.m);
	p_glActiveTexture(GL_TEXTURE0);
	p_glUniform1i(_u_tex_bill, 0);

	if (_bill_vao == 0) {
		p_glGenVertexArrays(1, &_bill_vao);
		p_glGenBuffers(1, &_bill_vbo);
	}
	p_glBindVertexArray(_bill_vao);
	p_glBindBuffer(GL_ARRAY_BUFFER, _bill_vbo);

	std::vector<float> bill_data;
	std::vector<std::pair<GLuint, int>> bill_groups;
	if (!_vd.parent_sprites_to_draw.empty()) {
		/* Horizontal camera direction (normalised). */
		Vec3 fwd = cam.target - cam.Eye();
		fwd.z = 0;
		const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
		if (fl > 1e-6f) {
			fwd.x /= fl;
			fwd.y /= fl;
		}
		const Vec3 right = { -fwd.y, fwd.x, 0 };

		for (const ParentSpriteToDraw &ps : _vd.parent_sprites_to_draw) {
			const SpriteID real = ps.image & SPRITE_MASK;
			const Sprite *spr = GetSprite(real, SpriteType::Normal);
			if (spr == nullptr || spr->width == 0 || spr->height == 0) continue;
			const GLuint tex = GetSpriteTexture(real, zoom);
			if (tex == 0) continue;
			/* Sprite pixel -> world: 0.5 world units per pixel (tile = 16
			 * world units = 32 screen pixels in the legacy projection). */
			const float hw = spr->width * 0.25f;  /* half world width */
			const float h = spr->height * 0.5f;   /* world height */
			const float bx = static_cast<float>(ps.xmin + ps.xmax) * 0.5f;
			const float by = static_cast<float>(ps.ymin + ps.ymax) * 0.5f;
			const float bz = static_cast<float>(ps.zmin);
			const float blx = bx - right.x * hw;
			const float bly = by - right.y * hw;
			const float brx = bx + right.x * hw;
			const float bry = by + right.y * hw;
			const float tz = bz + h;
			/* Bottom edge v=1, top edge v=0 (the texture is uploaded with the
			 * first sprite row — the top — as the bottom texture row). */
			const float v[6][5] = {
				{ blx, bly, bz, 0.0f, 1.0f },
				{ brx, bry, bz, 1.0f, 1.0f },
				{ brx, bry, tz, 1.0f, 0.0f },
				{ blx, bly, bz, 0.0f, 1.0f },
				{ brx, bry, tz, 1.0f, 0.0f },
				{ blx, bly, tz, 0.0f, 0.0f },
			};
			bill_groups.emplace_back(tex, static_cast<int>(bill_data.size() / 5));
			bill_data.insert(bill_data.end(), &v[0][0], &v[0][0] + 30);
		}
	}

	if (!bill_data.empty()) {
		p_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bill_data.size() * sizeof(float)), bill_data.data(), GL_DYNAMIC_DRAW);
		p_glEnableVertexAttribArray(0);
		p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
		p_glEnableVertexAttribArray(1);
		p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
		for (const auto &g : bill_groups) {
			p_glBindTexture(GL_TEXTURE_2D, g.first);
			p_glDrawArrays(GL_TRIANGLES, g.second, 6);
		}
	}

	/* --- Read back into the software screen buffer (y flipped) --- */
	std::vector<uint8_t> tmp(static_cast<size_t>(width) * height * 4);
	p_glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
	uint32_t *dst = static_cast<uint32_t *>(dpi.dst_ptr);
	const int pitch_words = dpi.pitch / 4;
	for (int y = 0; y < height; y++) {
		const uint8_t *src_row = tmp.data() + static_cast<size_t>(height - 1 - y) * width * 4;
		for (int x = 0; x < width; x++) {
			dst[y * pitch_words + x] = static_cast<uint32_t>(src_row[x * 4]) << 16 |
			                           static_cast<uint32_t>(src_row[x * 4 + 1]) << 8 |
			                           static_cast<uint32_t>(src_row[x * 4 + 2]);
		}
	}
}

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file viewport_drawer.h Types for the viewport draw pipeline (shared with the stage-3 GL renderer). */

#ifndef VIEWPORT_DRAWER_H
#define VIEWPORT_DRAWER_H

#include "viewport_sprite_sorter.h"
#include "core/enum_type.hpp"
#include "core/geometry_type.hpp"
#include "core/projective.hpp"
#include "gfx_type.h"

#include <string>
#include <vector>

struct StringSpriteToDraw {
	std::string string;
	uint16_t width;
	Colours colour;
	ViewportStringFlags flags;
	int32_t x;
	int32_t y;
};

struct TileSpriteToDraw {
	SpriteID image;
	PaletteID pal;
	const SubSprite *sub;           ///< only draw a rectangular part of the sprite
	int32_t x;                        ///< screen X coordinate of sprite
	int32_t y;                        ///< screen Y coordinate of sprite
};

struct ChildScreenSpriteToDraw {
	SpriteID image;
	PaletteID pal;
	const SubSprite *sub;           ///< only draw a rectangular part of the sprite
	int32_t x;
	int32_t y;
	bool relative;
	int next;                       ///< next child to draw (-1 at the end)
};

/** Enumeration of multi-part foundations */
enum class FoundationPart : uint8_t {
	None = 0xFF, ///< Neither foundation nor groundsprite drawn yet.
	Normal = 0, ///< First part (normal foundation or no foundation)
	Halftile = 1, ///< Second part (halftile foundation)
	End, ///< End marker.
};

/**
 * Mode of "sprite combining"
 * @see StartSpriteCombine
 */
enum class SpriteCombineMode : uint8_t {
	None, ///< Every #AddSortableSpriteToDraw start its own bounding box
	Pending, ///< %Sprite combining will start with the next unclipped sprite.
	Active, ///< %Sprite combining is active. #AddSortableSpriteToDraw outputs child sprites.
};

typedef std::vector<TileSpriteToDraw> TileSpriteToDrawVector;
typedef std::vector<StringSpriteToDraw> StringSpriteToDrawVector;
typedef std::vector<ParentSpriteToDraw> ParentSpriteToDrawVector;
typedef std::vector<ChildScreenSpriteToDraw> ChildScreenSpriteToDrawVector;

constexpr int LAST_CHILD_NONE = -1; ///< There is no last_child to fill.
constexpr int LAST_CHILD_PARENT = -2; ///< Fill last_child of the most recent parent sprite.

/** Data structure storing rendering information */
struct ViewportDrawer {
	DrawPixelInfo dpi;

	CameraParams camera; ///< Perspective camera for this frame (3D mode).

	StringSpriteToDrawVector string_sprites_to_draw;
	TileSpriteToDrawVector tile_sprites_to_draw;
	ParentSpriteToDrawVector parent_sprites_to_draw;
	ParentSpriteToSortVector parent_sprites_to_sort; ///< Parent sprite pointer array used for sorting
	ChildScreenSpriteToDrawVector child_screen_sprites_to_draw;

	int last_child;

	SpriteCombineMode combine_sprites; ///< Current mode of "sprite combining". @see StartSpriteCombine

	FoundationPart foundation_part; ///< Currently active foundation for ground sprite drawing.
	EnumIndexArray<int, FoundationPart, FoundationPart::End> foundation; ///< Foundation sprites (index into parent_sprites_to_draw).
	EnumIndexArray<int, FoundationPart, FoundationPart::End> last_foundation_child; ///< Tail of ChildSprite list of the foundations. (index into child_screen_sprites_to_draw)
	EnumIndexArray<Point, FoundationPart, FoundationPart::End> foundation_offset; ///< Pixel offset for ground sprites on the foundations.
};

/** The global viewport drawer state of the current frame. */
extern ViewportDrawer _vd;

#endif /* VIEWPORT_DRAWER_H */

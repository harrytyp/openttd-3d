/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file viewport3d.h Stage-3 GL renderer for the main viewport. */

#ifndef VIEWPORT3D_H
#define VIEWPORT3D_H

#include "core/geometry_type.hpp"

struct Viewport;
struct DrawPixelInfo;

/** Render the main viewport with the stage-3 GL renderer (ground heightfield mesh). */
void RenderViewport3D(const Viewport &vp, const DrawPixelInfo &dpi);

#endif /* VIEWPORT3D_H */

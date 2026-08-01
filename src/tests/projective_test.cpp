/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file projective_test.cpp Test functionality from core/projective.hpp (3D camera). */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/projective.hpp"

#include "../safeguards.h"

TEST_CASE("Projective - disabled camera is identity")
{
	const CameraParams c = MakeCameraParams(false, 50, 100, 200, 640, 480);
	CHECK(c.enabled == false);

	for (int i = 0; i < 8; i++) {
		const Point p{ 100 + i * 137, 200 + i * 91 };
		CHECK(CameraProject(c, p).x == p.x);
		CHECK(CameraProject(c, p).y == p.y);
		CHECK(CameraUnproject(c, p).x == p.x);
		CHECK(CameraUnproject(c, p).y == p.y);
	}
}

TEST_CASE("Projective - points recede towards the focus")
{
	const CameraParams c = MakeCameraParams(true, 50, 0, 0, 640, 480);
	REQUIRE(c.enabled);

	/* Centre of the viewport. */
	const int cx = 320;
	const int cy = 240;
	CHECK(c.center_x == cx);
	CHECK(c.center_y == cy);
	/* Focus line is at 40% of the height. */
	CHECK(c.focus_y == 192);

	/* Point above the horizon (far away) must move towards the centre... */
	const Point far{ cx + 200, c.focus_y - 100 };
	const Point pf = CameraProject(c, far);
	CHECK(std::abs(pf.x - cx) < 200);
	CHECK(std::abs(pf.y - cy) < std::abs(far.y - cy));

	/* ... and a point below it (near) must move away from the centre. */
	const Point near{ cx - 200, c.focus_y + 100 };
	const Point pn = CameraProject(c, near);
	CHECK(std::abs(pn.x - cx) > 200);
	CHECK(std::abs(pn.y - cy) > std::abs(near.y - cy));
}

TEST_CASE("Projective - roundtrip project/unproject")
{
	const CameraParams c = MakeCameraParams(true, 50, 0, 0, 640, 480);

	/* Sample the visible area below the horizon. */
	for (int y = c.focus_y + 1; y < 480; y += 17) {
		for (int x = -200; x < 840; x += 53) {
			const Point iso{ x, y };
			const Point proj = CameraProject(c, iso);
			const Point back = CameraUnproject(c, proj);
			CHECK(std::abs(back.x - iso.x) <= 1);
			CHECK(std::abs(back.y - iso.y) <= 1);
		}
	}
}

TEST_CASE("Projective - strength changes focal length")
{
	const CameraParams weak = MakeCameraParams(true, 0, 0, 0, 640, 480);
	const CameraParams strong = MakeCameraParams(true, 100, 0, 0, 640, 480);
	CHECK(weak.focal > strong.focal);
}

TEST_CASE("Projective - pitch moves the horizon")
{
	const CameraParams def = MakeCameraParams(true, 50, 0, 0, 640, 480);
	const CameraParams steep = MakeCameraParams(true, 50, 0, 0, 640, 480, 0);
	const CameraParams flat = MakeCameraParams(true, 50, 0, 0, 640, 480, 100);

	/* Default pitch keeps the historic horizon at 40% of the height. */
	CHECK(def.focus_y == 0 + 480 * 2 / 5);
	/* Steep: horizon higher up. */
	CHECK(steep.focus_y < def.focus_y);
	/* Flat: horizon closer to (or at) the centre. */
	CHECK(flat.focus_y > def.focus_y);
	CHECK(flat.focus_y <= def.center_y);
	/* Pitch does not affect the focal length or centre. */
	CHECK(steep.focal == def.focal);
	CHECK(steep.center_x == def.center_x);
	CHECK(steep.center_y == def.center_y);
}

TEST_CASE("Projective - depth scaling steps")
{
	const CameraParams c = MakeCameraParams(true, 50, 0, 0, 640, 480);
	REQUIRE(c.enabled);

	/* Reference: the bottom viewport edge is the nearest visible point. */
	CHECK(ZoomScaleForDepth(c, c.iso_ref) == 0);
	CHECK(ZoomScaleForDepth(c, c.iso_ref + 1000) == 0);
	/* The centre is still above the first threshold at the default pitch. */
	CHECK(ZoomScaleForDepth(c, c.center_y) == 0);
	/* The horizon (iso_y == focus_y) has relative scale ~0.76 -> full size. */
	CHECK(ZoomScaleForDepth(c, c.focus_y) == 0);
	/* Relative scale ~0.5 -> half size. */
	CHECK(ZoomScaleForDepth(c, c.focus_y + 50 * (c.focal + c.iso_ref - c.focus_y) / 100 - c.focal) == 1);
	/* Relative scale ~0.3 -> quarter size. */
	CHECK(ZoomScaleForDepth(c, c.focus_y + 30 * (c.focal + c.iso_ref - c.focus_y) / 100 - c.focal) == 2);
	/* Behind the camera -> quarter size. */
	CHECK(ZoomScaleForDepth(c, c.focus_y - c.focal) == 2);

	/* Disabled camera never scales. */
	const CameraParams off = MakeCameraParams(false, 50, 0, 0, 640, 480);
	CHECK(ZoomScaleForDepth(off, 0) == 0);
	CHECK(ZoomScaleForDepth(off, 100000) == 0);
}

TEST_CASE("Projective - flatter pitch scales the distance more")
{
	const CameraParams def = MakeCameraParams(true, 50, 0, 0, 640, 480);
	const CameraParams flat = MakeCameraParams(true, 50, 0, 0, 640, 480, 100);
	REQUIRE(flat.pitch == 100);

	/* Same isometric depth: with a flat camera the same sprite shrinks
	 * one step more than with the default pitch. */
	const int iso_y = def.focus_y + 40 * (def.focal + def.iso_ref - def.focus_y) / 100 - def.focal;
	CHECK(ZoomScaleForDepth(def, iso_y) == 1);
	CHECK(ZoomScaleForDepth(flat, iso_y) == 2);
}

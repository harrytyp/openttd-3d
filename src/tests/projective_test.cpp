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

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file camera3d_test.cpp Unit tests for the stage-3 free camera math. */

#include "../3rdparty/catch2/catch.hpp"
#include "../core/camera3d.hpp"

TEST_CASE("Camera3D - matrix inverse round trip")
{
	const Mat4 m = Mat4::LookAt({ 10, 20, 30 }, { 0, 0, 0 }, { 0, 0, 1 });
	const Mat4 inv = m.Inverse();
	const Mat4 id = m * inv;
	const Vec3 p{ 1, 2, 3 };
	const Vec3 r = id.Transform(p);
	CHECK(std::abs(r.x - p.x) < 1e-4f);
	CHECK(std::abs(r.y - p.y) < 1e-4f);
	CHECK(std::abs(r.z - p.z) < 1e-4f);
}

TEST_CASE("Camera3D - projection and unprojection round trip")
{
	Camera3D cam;
	cam.target = { 0, 0, 0 };
	cam.distance = 500.0f;
	cam.yaw = 120.0f;
	cam.pitch = 55.0f;
	cam.aspect = 1.5f;

	const Vec3 world{ 100, 50, 8 };
	const Vec3 ndc = cam.WorldToNDC(world);
	CHECK(ndc.x > -1.0f);
	CHECK(ndc.x < 1.0f);
	CHECK(ndc.y > -1.0f);
	CHECK(ndc.y < 1.0f);

	/* Unproject the NDC point back and check the ray passes through the world point. */
	const auto [origin, dir] = cam.ScreenRay(
			(ndc.x + 1.0f) * 0.5f * 800.0f,
			(1.0f - ndc.y) * 0.5f * 600.0f,
			800.0f, 600.0f);
	const Vec3 to_point = world - origin;
	const float along = dir.Dot(to_point);
	const float dist_sq = to_point.Dot(to_point) - along * along;
	CHECK(dist_sq < 0.01f);
}

TEST_CASE("Camera3D - legacy isometric equivalence (yaw 45, pitch 30)")
{
	/* The classic OpenTTD isometric projection (RemapCoords) is proportional to
	 * an orthographic camera at yaw = 45 deg, pitch = 30 deg:
	 *   x axis -> screen (-2, +1), y axis -> (+2, +1), z axis -> (0, +1).
	 * With a large distance the perspective camera approximates this. */
	Camera3D cam;
	cam.target = { 0, 0, 0 };
	cam.distance = 100000.0f;
	cam.yaw = 45.0f;
	cam.pitch = 30.0f;
	cam.aspect = 1.0f;

	const Vec3 base = cam.WorldToNDC({ 0, 0, 0 });
	const Vec3 dx = cam.WorldToNDC({ 1, 0, 0 }) - base;
	const Vec3 dy = cam.WorldToNDC({ 0, 1, 0 }) - base;
	const Vec3 dz = cam.WorldToNDC({ 0, 0, 1 }) - base;

	/* Screen ratio |dy|/|dx| must be 0.5 for both horizontal axes. */
	CHECK(std::abs(std::abs(dx.y / dx.x) - 0.5f) < 0.02f);
	CHECK(std::abs(std::abs(dy.y / dy.x) - 0.5f) < 0.02f);
	/* The z axis is vertical: no horizontal component. Note: unlike the legacy
	 * isometric projection (where height grows *downwards* on screen), the 3D
	 * camera uses the natural convention — heights grow upwards (towards the
	 * sky), so dz.y is positive. */
	CHECK(std::abs(dz.x) < 1e-4f);
	CHECK(dz.y > 0.0f);
	/* x and y axes are symmetric. */
	CHECK(std::abs(dx.x + dy.x) < 1e-4f);
}

TEST_CASE("Camera3D - eye position follows yaw and pitch")
{
	Camera3D cam;
	cam.target = { 0, 0, 0 };
	cam.distance = 100.0f;
	cam.yaw = 0.0f;   /* looking along +y */
	cam.pitch = 90.0f; /* straight down */
	const Vec3 eye = cam.Eye();
	CHECK(std::abs(eye.x) < 1e-3f);
	CHECK(std::abs(eye.y) < 1e-3f);
	CHECK(std::abs(eye.z - 100.0f) < 1e-3f);

	cam.pitch = 0.0f;
	cam.yaw = 90.0f; /* looking along +x */
	const Vec3 eye2 = cam.Eye();
	CHECK(std::abs(eye2.x + 100.0f) < 1e-3f);
	CHECK(std::abs(eye2.y) < 1e-3f);
	CHECK(std::abs(eye2.z) < 1e-3f);
}

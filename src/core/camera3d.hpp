/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file camera3d.hpp Stage-3 free camera: minimal 3D vector/matrix math and an orbit camera. */

#ifndef CAMERA3D_HPP
#define CAMERA3D_HPP

#include <cmath>
#include <utility>

/** A 3D vector (single precision is enough for rendering). */
struct Vec3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;

	Vec3() = default;
	constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

	Vec3 operator+(const Vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
	Vec3 operator-(const Vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
	Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
	Vec3 operator-() const { return { -x, -y, -z }; }
	float Dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
	Vec3 Cross(const Vec3 &o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
	float Length() const { return std::sqrt(x * x + y * y + z * z); }
	Vec3 Normalized() const
	{
		const float len = Length();
		return len > 1e-12f ? *this * (1.0f / len) : Vec3{};
	}
};

/**
 * A 4x4 matrix in column-major order (OpenGL convention), used for the
 * view/projection transforms of the stage-3 camera.
 */
struct Mat4 {
	/* Column-major: m[col * 4 + row]. */
	float m[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	static Mat4 Identity()
	{
		Mat4 r;
		r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
		return r;
	}

	/** Perspective projection (right-handed, NDC y up). */
	static Mat4 Perspective(float fov_y_rad, float aspect, float near, float far)
	{
		Mat4 r;
		const float f = 1.0f / std::tan(fov_y_rad / 2.0f);
		r.m[0] = f / aspect;
		r.m[5] = f;
		r.m[10] = (far + near) / (near - far);
		r.m[11] = -1.0f;
		r.m[14] = 2.0f * far * near / (near - far);
		return r;
	}

	/** Right-handed look-at view matrix. */
	static Mat4 LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
	{
		const Vec3 f = (center - eye).Normalized();
		const Vec3 s = f.Cross(up).Normalized();
		const Vec3 u = s.Cross(f);
		Mat4 r = Identity();
		r.m[0] = s.x; r.m[1] = u.x; r.m[2] = -f.x;
		r.m[4] = s.y; r.m[5] = u.y; r.m[6] = -f.y;
		r.m[8] = s.z; r.m[9] = u.z; r.m[10] = -f.z;
		r.m[12] = -s.Dot(eye);
		r.m[13] = -u.Dot(eye);
		r.m[14] = f.Dot(eye);
		return r;
	}

	/** Transform a point (w = 1), returns the xyz part. */
	Vec3 Transform(const Vec3 &v) const
	{
		return {
			m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
			m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
			m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14],
		};
	}

	/** Full 4D transform, returns homogeneous coordinates. */
	std::pair<Vec3, float> Transform4(const Vec3 &v) const
	{
		return {
			{ m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
			  m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
			  m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] },
			m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15],
		};
	}

	Mat4 operator*(const Mat4 &o) const
	{
		Mat4 r;
		for (int c = 0; c < 4; c++) {
			for (int row = 0; row < 4; row++) {
				float sum = 0.0f;
				for (int k = 0; k < 4; k++) sum += m[k * 4 + row] * o.m[c * 4 + k];
				r.m[c * 4 + row] = sum;
			}
		}
		return r;
	}

	/** General 4x4 inverse (adjugate method). */
	Mat4 Inverse() const
	{
		Mat4 r;
		const float *a = this->m;
		float *inv = r.m;

		inv[0] =  a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
		inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
		inv[8] =  a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
		inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
		inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
		inv[5] =  a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
		inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
		inv[13] =  a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
		inv[2] =  a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
		inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
		inv[10] =  a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
		inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
		inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
		inv[7] =  a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
		inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
		inv[15] =  a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

		const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
		if (std::abs(det) < 1e-12f) return Identity();
		const float inv_det = 1.0f / det;
		for (float &v : r.m) v *= inv_det;
		return r;
	}
};

/**
 * Free orbit camera for stage 3: pivots around a target point.
 *
 * Conventions (match the legacy isometric projection): yaw = 0° looks along
 * +y, yaw = 45° / pitch = 30° is the classic OpenTTD isometric view
 * (proportional to #RemapCoords). World coordinates are the OpenTTD world
 * units (tile x/y and height z).
 */
struct Camera3D {
	Vec3 target{};      ///< Orbit pivot (world units).
	float distance = 400.0f; ///< Eye distance from the target.
	float yaw = 45.0f;  ///< Rotation around the world up axis (degrees).
	float pitch = 30.0f; ///< Elevation above the ground plane (degrees).
	float fov = 50.0f;  ///< Vertical field of view (degrees).
	float aspect = 1.0f; ///< Viewport width / height.

	/** Eye position derived from target/distance/yaw/pitch. */
	Vec3 Eye() const
	{
		const float y = yaw * static_cast<float>(M_PI) / 180.0f;
		const float p = pitch * static_cast<float>(M_PI) / 180.0f;
		/* View direction (camera -> target): pitch 90 looks straight down. */
		const Vec3 dir{ std::cos(p) * std::sin(y), std::cos(p) * std::cos(y), -std::sin(p) };
		return target - dir * distance;
	}

	Mat4 ViewMatrix() const { return Mat4::LookAt(Eye(), target, { 0, 0, 1 }); }
	Mat4 ProjectionMatrix() const
	{
		/* near/far chosen for depth precision: with near=0.1 and far=100000
		 * the 24-bit depth resolution at z=1600 is only ~1.5 world units,
		 * so distant ground faces collide in the depth test and leave sky
		 * gaps (visible as horizontal bands). near=1/far=20000 keeps the
		 * whole map (max distance ~6000) well within the precision budget. */
		return Mat4::Perspective(fov * static_cast<float>(M_PI) / 180.0f, aspect, 1.0f, 20000.0f);
	}
	Mat4 ViewProjMatrix() const { return ProjectionMatrix() * ViewMatrix(); }

	/**
	 * Project a world point to normalised device coordinates (x/y in [-1, 1],
	 * z in [0, 1] with 0 = near).
	 */
	Vec3 WorldToNDC(const Vec3 &world) const
	{
		const auto [v, w] = ViewProjMatrix().Transform4(world);
		return { v.x / w, v.y / w, (v.z / w + 1.0f) / 2.0f };
	}

	/**
	 * Unproject a screen point (pixel coordinates, origin top-left) into a
	 * world-space ray (origin + direction).
	 */
	std::pair<Vec3, Vec3> ScreenRay(float screen_x, float screen_y, float width, float height) const
	{
		const float ndc_x = 2.0f * screen_x / width - 1.0f;
		const float ndc_y = 1.0f - 2.0f * screen_y / height;
		const Mat4 inv = ViewProjMatrix().Inverse();
		/* Two points on the ray: near and far plane (NDC z = 0 and 1). */
		const auto [n, nw] = inv.Transform4({ ndc_x, ndc_y, -1.0f });
		const auto [f, fw] = inv.Transform4({ ndc_x, ndc_y, 1.0f });
		const Vec3 near_p{ n.x / nw, n.y / nw, n.z / nw };
		const Vec3 far_p{ f.x / fw, f.y / fw, f.z / fw };
		const Vec3 dir = (far_p - near_p).Normalized();
		return { near_p, dir };
	}
};

#endif /* CAMERA3D_HPP */

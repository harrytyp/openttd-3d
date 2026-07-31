/*
 * This file is part of OpenTTD 3D camera experiment.
 *
 * Perspective projection for the main viewport ("2.5D" mode).
 *
 * The game world is first projected with the classic isometric RemapCoords()
 * into "virtual viewport space". This module then applies a perspective
 * projection on top of that space: points below the horizon line are scaled
 * up, points above it are scaled down, so the map appears to recede into
 * the distance (mode-7 style). All functions are pure and unit-testable.
 */

#ifndef PROJECTIVE_HPP
#define PROJECTIVE_HPP

#include "../stdafx.h"
#include "geometry_type.hpp"

/** Parameters of the perspective camera, derived from the viewport size. */
struct CameraParams {
	bool enabled = false;    ///< Perspective active?
	int focal = 0;           ///< Focal length in virtual pixels (larger = flatter perspective).
	int focus_y = 0;         ///< Horizon line (y) in virtual pixels.
	int center_x = 0;        ///< Projection center x in virtual pixels.
	int center_y = 0;        ///< Projection center y in virtual pixels.
};

/**
 * Build camera parameters for a viewport of the given size.
 * @param enabled Perspective on/off.
 * @param strength 0..100: 0 = almost orthographic, 100 = strong perspective.
 * @param vp_left, vp_top, vp_width, vp_height Viewport rectangle in virtual pixels.
 */
inline CameraParams MakeCameraParams(bool enabled, uint8_t strength, int vp_left, int vp_top, int vp_width, int vp_height)
{
	CameraParams c;
	c.enabled = enabled;
	if (!enabled) return c;

	/* Focal length: 2.0x viewport height at strength 0, 1.0x at strength 100. */
	c.focal = vp_height * (200 - std::min<uint8_t>(strength, 100)) / 100;
	if (c.focal < vp_height / 2) c.focal = vp_height / 2;

	/* Horizon line at 40% of the viewport height. */
	c.focus_y = vp_top + vp_height * 2 / 5;

	c.center_x = vp_left + vp_width / 2;
	c.center_y = vp_top + vp_height / 2;
	return c;
}

/**
 * Project an isometric point (in virtual viewport space) to perspective space.
 * @param c Camera parameters.
 * @param iso Point from RemapCoords().
 * @return Perspective-projected point.
 */
inline Point CameraProject(const CameraParams &c, Point iso)
{
	if (!c.enabled) return iso;

	const int dy = iso.y - c.focus_y;             /* Distance below horizon. */
	const int scale_num = c.focal + dy;           /* Numerator of scale factor. */
	if (scale_num <= 0) return { c.center_x, c.center_y }; /* Behind the camera: collapse to horizon. */

	/* scale = scale_num / focal; project around the focus point. */
	const int64_t x = c.center_x + (static_cast<int64_t>(iso.x) - c.center_x) * scale_num / c.focal;
	const int64_t y = c.center_y + (static_cast<int64_t>(iso.y) - c.center_y) * scale_num / c.focal;
	return { static_cast<int>(x), static_cast<int>(y) };
}

/**
 * Inverse of #CameraProject: map a point in perspective (virtual viewport)
 * space back to isometric space. Returns the input unchanged when disabled.
 * @param c Camera parameters.
 * @param s Point in perspective space.
 * @return Point in isometric space.
 */
inline Point CameraUnproject(const CameraParams &c, Point s)
{
	if (!c.enabled) return s;

	/* Solve: s.y = center_y + (py - center_y) * (focal + py - focus_y) / focal
	 * for py, then px from s.x. Quadratic in py:
	 *   py^2 + py*(focal - focus_y - center_y) + center_y*(focus_y - focal) - focal*(s.y - center_y) = 0 */
	const int64_t a = 1;
	const int64_t b = c.focal - c.focus_y - c.center_y;
	const int64_t cc = static_cast<int64_t>(c.center_y) * (c.focus_y - c.focal) - static_cast<int64_t>(c.focal) * (s.y - c.center_y);

	int64_t disc = b * b - 4 * a * cc;
	if (disc < 0) return { s.x, s.y };
	int64_t root = static_cast<int64_t>(std::sqrt(static_cast<double>(disc)));
	/* Take the larger root: the point in front of the camera. */
	int64_t py = (-b + root) / (2 * a);
	if (py < c.focus_y - c.focal) py = (-b - root) / (2 * a);

	const int64_t scale_num = c.focal + py - c.focus_y;
	if (scale_num <= 0) return { s.x, s.y };
	const int64_t px = c.center_x + (static_cast<int64_t>(s.x) - c.center_x) * c.focal / scale_num;

	return { static_cast<int>(px), static_cast<int>(py) };
}

#endif /* PROJECTIVE_HPP */

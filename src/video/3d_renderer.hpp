#ifndef VIDEO_3D_RENDERER_HPP
#define VIDEO_3D_RENDERER_HPP

#include "../gfx_type.h"
#include "../viewport_type.h"
#include <vector>

/* Forward declaration for any necessary types from viewport.cpp if we need to pass the whole vectors */
/* For now we'll define simple interface to pass the collected data */

struct Viewport;
struct DrawPixelInfo;

/* We'll need to share these types or redeclare them if they are internal to viewport.cpp. 
 * For simplicity in this POC, we'll assume they are accessible if we include viewport_func.h 
 * but they are actually internal to viewport.cpp. Let's move them to a common header or 
 * just use opaque access. */

struct TileSpriteToDraw;
struct ParentSpriteToDraw;
struct ViewportDrawer;

extern const ViewportDrawer* GetViewportDrawer();

namespace Renderer3D {

bool IsEnabled();
void SetEnabled(bool enabled);

void Render(const Viewport &vp, const DrawPixelInfo &dpi);

} // namespace Renderer3D

#endif /* VIDEO_3D_RENDERER_HPP */

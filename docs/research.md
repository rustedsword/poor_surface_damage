# How others track damage

This document looks at how presentation APIs, compositors, toolkits, browsers and mobile platforms handle two things:

- **Frame damage**: telling the presentation engine what changed since the last frame.
- **Repair**: knowing what a reused buffer has missed, so that only that has to be redrawn.

Everything below was read from source code and specifications on 2026-10-18. File and function names are included so
the findings can be checked again later. [design.md](design.md) describes what this library does with them.

## Specifications

| API | What it carries | Notes |
|---|---|---|
| [`wl_surface.damage_buffer`](https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml) | frame damage, in buffer coordinates, origin at the top left | Damage is always relative to the previous commit, never to a particular buffer. Pending damage accumulates until the commit. New clients shouldn't use `wl_surface.damage`, which takes surface coordinates. A buffer may only be reused after `wl_buffer.release`, and if the release comes before the frame callback, a single buffer is enough. Version 7 (Wayland 1.25) added `get_release` for releases per commit. |
| [EGL_EXT_buffer_age](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_buffer_age.txt) | the age of the back buffer | An age of 0 means the contents are undefined. On every swap, the swapped buffer gets age 1, and every other buffer with an age gets one more, so double buffering gives ages of 2 and triple buffering ages of 3. The age stays valid until the next frame and may be reset by a resize. |
| [EGL_KHR_partial_update](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_partial_update.txt) | the repair, origin at the bottom left | It is called once per frame, after querying the age and before rendering. Rendering outside the region makes the whole framebuffer undefined. It lets tiled GPUs skip loading and storing tiles that didn't change ([Weston df2095fa](https://gitlab.freedesktop.org/wayland/weston/-/commit/df2095fa35fe84e4ebc30754dd9f25e50bd1aa47)). |
| [EGL_KHR_swap_buffers_with_damage](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_swap_buffers_with_damage.txt) | frame damage, origin at the **bottom left** | It is only a hint, so the whole buffer must still be correct. Zero rects mean everything changed. Mesa flips Y when it turns the rects into `damage_buffer`. |
| [EGL_NV_post_sub_buffer](https://registry.khronos.org/EGL/extensions/NV/EGL_NV_post_sub_buffer.txt), [GLX_MESA_copy_sub_buffer](https://registry.khronos.org/OpenGL/extensions/MESA/GLX_MESA_copy_sub_buffer.txt), `EGL_BUFFER_PRESERVED` | — | Older approaches that either copy one rect to the front buffer or keep the back buffer's contents. On tiled GPUs, keeping the contents costs "a full screen blit" every frame, according to Arm. |
| [VK_KHR_incremental_present](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_incremental_present.html) | frame damage, in image pixels, origin at the top left | Since revision 2 the rects are relative to `currentTransform`. Zero rects mean the whole image changed, while rects that are all empty mean nothing did. Pixels outside the rects must still be correct. |
| [Vulkan WSI](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html) | no buffer age at all | "Presentation is a read-only operation that will not affect the content of the presentable images". A re-acquired image still holds what was drawn into it, but only with `clipped = VK_FALSE`, and only if the image is transitioned from `PRESENT_SRC_KHR` (not `UNDEFINED`) and loaded with `LOAD_OP_LOAD`. New swapchain images are `UNDEFINED`. Applications have to track ages per image index themselves. |
| [DXGI Present1](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-2-presentation-improvements) | dirty and scroll rects | With flip-sequential swap chains the *runtime* copies everything outside the dirty rects from the previous frame. The application "must not update any pixel outside of the dirty rectangles", and has to update every pixel inside them. `FLIP_DISCARD` doesn't allow partial presentation. |
| [X11 DAMAGE](https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/damageproto.txt), [Present](https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/presentproto.txt) | — | DAMAGE reports pixel changes to consumers like compositors or VNC servers, anywhere from raw rects down to a single bounding box. `PresentPixmap` takes a `valid-area` and an `update-area`. |
| [NSView](https://developer.apple.com/documentation/appkit/nsview/setneedsdisplay(_:)), CAMetalLayer | invalid rects | `setNeedsDisplay(_:)` adds to an invalid region, and `getRectsBeingDrawn` returns rects that don't overlap. Metal has no partial presentation, and nothing guarantees that a drawable keeps its contents, so it has to be treated as age 0. |
| [Mali transaction elimination](https://www.arm.com/technologies/graphics-technologies/transaction-elimination) | — | The GPU keeps a CRC for every 16×16 tile and doesn't write tiles that are the same as in the previous frame. This saves memory bandwidth, not shading. Transitions from `UNDEFINED` throw the CRCs away. |

**The origins differ.** EGL rects start at the bottom left, while Wayland, Vulkan and DXGI start at the top left.

## How buffers are repaired

### A history indexed by buffer age

Keep the damage of the last few frames. A buffer of age *a* then has to redraw the union of the last *a*−1 frames. If
the age is 0, or older than the history, redraw everything.

| Project | Where | Frames kept | Notes |
|---|---|---|---|
| KWin | `src/utils/damagejournal.h` | 10 | `accumulate(age, Region::infinite())`. Slot ages follow the EGL rule in `EglSwapchain::release()`, right after rendering. `KWIN_USE_BUFFER_AGE=0` turns it off. |
| Mutter | `clutter/clutter/clutter-damage-history.c` | 16 | Redraws everything for the first 3 frames ("junk frames when starting up"). The repair goes to `eglSetDamageRegion`, and the frame damage to the swap. |
| GTK (GL) | `gdk/gdkglcontext.c` | 4 | Passes the repair instead of the frame damage to `eglSwapBuffersWithDamage`, so it reports more than changed. It also has a bug: after a resize, an empty slot gives a partial union instead of everything. |
| Weston up to 12 | `gl-renderer.c` `output_get_damage` | 2 | Weston 13 keeps "virtual renderbuffers" per EGL age instead. |
| wlroots up to 0.18 | `wlr_damage_ring_get_buffer_damage` | 2 ("for triple buffering") | Removed in 0.19. |
| WebRender | `renderer/mod.rs` `BufferDamageTracker` | 4 | Uses a single rect. |
| HWUI | `CanvasContext::computeDirtyRect` | 8 ("at least 4 because we do quad buffer") | Uses a single SkRect. A frame that wasn't drawn is recorded as an `INT_MAX` rect. |
| Flutter (Android, old) | `android_egl_surface.cc` | 10 | Never checked whether the age was older than the history, and didn't record full frames. It is disabled now (flutter#123353). |

### Damage accumulated per buffer

Every buffer keeps the damage it has missed. The damage of each frame is added to all other buffers, and the buffer
that was drawn into is cleared. There are no ages and no limits, buffers can come back in any order, but you need to
know which buffer is which.

| Project | Where | Notes |
|---|---|---|
| GTK (Vulkan, Wayland SHM) | `gdk/gdkvulkancontext.c` | Adds the damage to every image before acquiring. It sends the whole repaint as `VkPresentRegionsKHR`, so it reports more than changed. |
| Chromium | `buffer_queue.cc` `UpdateBufferDamage`, `SkiaOutputDeviceVulkan::damage_of_images_` | Uses a single `gfx::Rect`. A skipped present damages every buffer, and a client frame that was skipped between aggregations becomes full damage. |
| Weston 13+ | `gl_renderer_update_renderbuffers`, `pixman_renderer_repaint_output` | Uses explicit renderbuffers. The Vulkan renderer sets its scissor to the bounding box of the damage. |
| wlroots 0.18+ | `wlr_damage_ring_rotate_buffer` | Keeps a list of buffers, most recent first, each with the damage of the frame drawn into it. The repair is the union of all newer entries, and an unknown buffer gets full damage. The swapchain holds up to 4 buffers. |
| Flutter (Metal) | `gpu_surface_metal_impeller.mm` | Keyed by texture pointer. Entries are never removed. |
| Qt (Wayland SHM, macOS, linuxfb DRM) | `qtbase/src/plugins/platforms/wayland/qwaylandshmbackingstore.cpp` | Keeps a dirty region per buffer and fills it by copying, see below and [Qt in detail](#qt-in-detail). |

### Copying instead of redrawing

The client copies whatever the new buffer missed from the last presented buffer, and then draws only the new damage.
This suits retained drawing, where the client can't easily redraw an arbitrary area.

- **Qt on Wayland SHM** picks the free buffer with the lowest age, copies its dirty region from the front buffer when
  flushing (`finalizeBackBuffer`), and sends only the flushed region as damage. It keeps at most 5 buffers.
- **Android's `Surface::lock`** copies `mDirtyRegion − newDirty` from the front buffer. It has a bug:
  `mDirtyRegion.subtract(slot.dirtyRegion)` throws its result away, so the region only grows. That copies too much,
  but never too little.
- **Gecko on Wayland SHM** (`WindowSurfaceWaylandMultiBuffer.cpp`) keeps only one frame of history.
- **DXGI flip-sequential** has the runtime do the copy.

### Mesa, between applications and compositors

- **EGL on Wayland** (`platform_wayland.c`):
  - It has up to 4 buffers and counts ages with the EGL rule.
  - It picks the released buffer with the **smallest positive age**.
  - Released buffers older than 20 frames are freed.
  - `EGL_KHR_partial_update` is only available on drivers that implement it: etnaviv, iris, lima, panfrost and zink.
- **Vulkan WSI on Wayland** (`wsi_common_wayland.c`):
  - `VkPresentRegionsKHR` becomes one `damage_buffer` per rect, and no rects at all become `INT32_MAX` damage.
  - Acquiring returns the released image with the lowest index, or with explicit sync, the least recently presented
    one.
- **The headless WSI** hands out images in the order 0, 1, 0, 2, 0, 1, 0, 3, so ages go up to 2^N−1, and short
  histories often fall back to redrawing everything.

## Qt in detail

This is Qt 6 `dev` (qtbase `7faa468198d1`, qtdeclarative `5c096a1c4f6f`). The Wayland client now lives in qtbase,
under `src/plugins/platforms/wayland/`.

**Widgets are precise.**

- `QWidgetRepaintManager` keeps exact `QRegion`s: one for every dirty widget, and one for the top-level window that
  collects moves, resizes, shows and hides.
- Opaque siblings and children are subtracted.
- Opaque widgets that nothing covers are painted directly, without painting their parents first.
- The flushed region is what was painted, plus the areas of scroll and move blits.
- There is no limit on the number of rects and no fallback to a bounding box.
- Everything is repainted only when the backing store no longer has the size of the window and the platform can't keep
  static contents (only Windows and macOS can).
- Repaints are posted as `UpdateRequest` events and aren't paced by frame callbacks.

**The raster backends repair buffers correctly**, per buffer, by copying.

- **Wayland SHM** (`qwaylandshmbackingstore.cpp`):
  - Every buffer has a `QRegion`. A painted region is removed from the back buffer's region and added to the others.
  - When flushing, `finalizeBackBuffer` copies whatever the back buffer is still missing from the front buffer. It does
    this after painting, and skips what was just painted.
  - Buffers are reused least recently used first, with at most 5 of them, and dropped once their age reaches 50.
  - Damage is sent as one `damage_buffer` per rect, without a limit.
  - How this code evolved:
    - 6.7 copied the whole previous buffer.
    - 6.8 added the per-buffer regions ([qtwayland 20477f13](https://github.com/qt/qtwayland/commit/20477f135136ecc4d5b4bba172390e658cb01683), 2024).
    - 6.10 added `scroll()`, and stopped copying what is about to be painted anyway.
    - 6.11 moved the copy after painting ([6f25f703](https://github.com/qt/qtbase/commit/6f25f703fd37a900c139e14a33a4639502bfeae7)).
- macOS (`QCALayerBackingStore`, 3 IOSurfaces) works the same way.
- The DRM backend of linuxfb does textbook repair of 2 buffers from a shadow image.
- XCB keeps a single image in MIT-SHM. Before repainting an area that the X server might still be reading, it waits for
  a round trip.

**Everything that goes through the RHI or GL redraws the whole window and reports it all as damage.**

- `rhiFlush` uploads the bounding box of the dirty region, then clears and composites the whole window again. It also
  copies the whole image ([QTBUG-120565](https://qt-project.atlassian.net/browse/QTBUG-120565)).
- QRhi has no API for damage or buffer ages ([QTBUG-135832](https://qt-project.atlassian.net/browse/QTBUG-135832)).
- On Wayland, EGL windows call plain `eglSwapBuffers`. With client-side decorations, the whole FBO is blitted on every
  swap. A patch for swapping with damage hasn't been merged
  ([Gerrit 684340](https://codereview.qt-project.org/c/qt/qtbase/+/684340)).
- This affects every widget window with a QOpenGLWidget, QQuickWidget or QRhiWidget inside, and Android, iOS and EGLFS.
  On D3D11, Qt uses `FLIP_DISCARD` and `Present()`, never `Present1`.

**Qt Quick redraws every frame, by design.**

- Requests for buffer age, partial updates and swapping with damage were declined
  ([QTBUG-82860](https://qt-project.atlassian.net/browse/QTBUG-82860),
  [QTBUG-101976](https://qt-project.atlassian.net/browse/QTBUG-101976): "we will always render the entire surface").
  Proper damage reporting "is impossible by design" (QTBUG-135832, 2025).
- Only the software renderer computes dirty regions, taking occlusion into account (`optimizeRenderList`), and it
  turns them off at fractional scales.

**Weak spots on Wayland:**

- Widget repaints aren't paced, so the pool grows to 5 buffers and then blocks.
- Every flush of a subsurface allocates a new buffer.
- Decorations are regenerated at the full size of the surface.
- Scrolling and partial updates in Qt Quick are turned off at fractional scales.
- Open bugs: [QTBUG-151525](https://qt-project.atlassian.net/browse/QTBUG-151525) (reusing buffers ignores the device
  pixel ratio) and [QTBUG-148631](https://qt-project.atlassian.net/browse/QTBUG-148631).

## Representing damage

| Representation | Used by | Trade-off |
|---|---|---|
| Banded regions (pixman, QRegion, cairo, SkRegion, Android's Region) | GTK, Qt, KWin, Weston, wlroots, Mutter | Exact, but they allocate memory and the number of rects is unbounded. |
| A single bounding box | Chromium, WebRender (partial presentation is limited to 1 rect on Windows, Android and GTK), HWUI, Flutter | Cheap, but two changes far apart repaint everything between them. |
| A limited number of rects | wlroots switches to the bounding box above 20 rects, Mutter uses 16 for copies to secondary GPUs, and Gecko's `nsRegion::SimplifyOutward(8)` first merges bands, then falls back to the bounding box | Few scissors or draw calls. |
| Tiles | WebRender's picture cache (1024×512 tiles, with quadtree invalidation down to 8×8 per tile). Flutter aligns damage to 32×32 on Android GL (because of glitches on Pixel phones, flutter#97482), and Impeller redraws everything once damage covers 70% of the frame. Mali's transaction elimination | Fixed memory and cheap unions, but coarse. |

## Where damage comes from

- **Invalidation**: widgets mark rects as dirty, as in Qt's `QWidgetRepaintManager::markDirty`, NSView or Android's
  `RenderNode::damageSelf`. HWUI's `DamageAccumulator` maps the rects of children through their transforms, and gives
  up with infinite damage under perspective.
- **Diffing trees**: GSK's `gsk_render_node_diff`, and Flutter's `DiffContext`, which matches layers by id and grows the
  damage under backdrop filters. Chromium's `DamageTracker` damages the old and the new rects of changed layers.
- **Compositors**:
  - Client damage is converted from buffer to output coordinates and clipped to what is visible. Moving or restacking a
    window damages both its old and its new visible region (`wlr_scene`, Weston's `paint_node_add_damage`).
  - Scaling grows the damage by the reach of the filter (wlroots uses `ceil(scale/2)`), and transformed views use their
    bounding box.
  - The damage of the output also goes down to the hardware through DRM's `FB_DAMAGE_CLIPS`, which helps panels with
    self refresh.

## Debugging

- `WLR_SCENE_DEBUG_DAMAGE=highlight|rerender`
- Weston: Mod+Shift+Space, then `M` (GL) or `R` (pixman)
- KWin's Show Paint effect
- `CLUTTER_PAINT=damage-region`
- GTK Inspector's "Show Graphic Updates", and `GSK_DEBUG=full-redraw`
- `debug.hwui.show_dirty_regions`
- `vk-damage --flash`

## Lessons

1. **Keep the two kinds of damage apart.** Send the frame damage to the presentation engine, and redraw the repair.
   GTK sends the repair, and so reports more than changed.
2. **When in doubt, redraw everything**: for age 0, for ages older than the history, for new buffers, after a resize,
   after a skipped frame, and for the first frames. All the bugs found are in this area: GTK's partial union, old
   Flutter's missing age check and Android's discarded subtraction.
3. **Knowing the buffer beats knowing its age.** Projects are moving from age histories to damage per buffer (Weston 13,
   wlroots 0.18), because it works in any order and without a limit. Ages are still needed for EGL.
4. **Pick the most recently presented buffer** when the client picks (Mesa's EGL, Qt). Most of the time that turns 3 or
   4 buffers into 2.
5. **A few rects are enough.** Almost everyone collapses damage to a single rect, so even a handful of rects is a cheap
   improvement.
6. **Repairing in Vulkan is allowed, but has to be explicit**: `clipped = VK_FALSE`, transition from `PRESENT_SRC_KHR`
   and `LOAD_OP_LOAD`, and treat new images, which are `UNDEFINED`, as empty. Zero present rects mean "everything", so a
   frame without changes needs one empty rect.
7. **Watch out for the origin**: EGL rects start at the bottom left.

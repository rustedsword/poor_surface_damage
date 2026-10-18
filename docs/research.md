# Damage tracking out there

How presentation APIs, compositors, toolkits, browsers and mobile stacks handle two things:

- **Frame damage**: telling the presentation engine what changed since the last frame.
- **Repair**: knowing what a reused buffer lacks, so that only that is redrawn.

Everything below was read from source and specifications on 2026-10-18. File and function names are given so that the
findings can be checked again. [design.md](design.md) describes what this library does with them.

## Specifications

| API | What it carries | Notes |
|---|---|---|
| [`wl_surface.damage_buffer`](https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml) | frame damage, buffer coordinates, top-left | Damage is relative to the previous commit, never to a buffer. Pending damage accumulates until commit. `wl_surface.damage` (surface coordinates) is "not to be used" by new clients. A buffer is reused only after `wl_buffer.release`. A release before the frame callback means a single buffer is enough. `get_release` (version 7, Wayland 1.25) gives per-commit releases. |
| [EGL_EXT_buffer_age](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_buffer_age.txt) | age of the back buffer | 0 means undefined contents. At each swap the swapped buffer gets age 1 and every other buffer with an age gets +1. 2 is double buffering, 3 triple. Valid until the next frame boundary. May reset on resize. |
| [EGL_KHR_partial_update](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_partial_update.txt) | repair (buffer damage), bottom-left | Called once per frame, before rendering, after the age was queried. Rendering outside it makes the whole framebuffer undefined. It lets tilers skip loading and storing unchanged tiles ([Weston df2095fa](https://gitlab.freedesktop.org/wayland/weston/-/commit/df2095fa35fe84e4ebc30754dd9f25e50bd1aa47)). |
| [EGL_KHR_swap_buffers_with_damage](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_swap_buffers_with_damage.txt) | frame damage, **bottom-left** | Only a hint, and the whole buffer must still be right. 0 rects means everything. Mesa flips Y into `damage_buffer`. |
| [EGL_NV_post_sub_buffer](https://registry.khronos.org/EGL/extensions/NV/EGL_NV_post_sub_buffer.txt), [GLX_MESA_copy_sub_buffer](https://registry.khronos.org/OpenGL/extensions/MESA/GLX_MESA_copy_sub_buffer.txt), `EGL_BUFFER_PRESERVED` | — | Older schemes that copy one rect to the front buffer or keep the back buffer. Preserving costs "a full screen blit" per frame on tilers (Arm). |
| [VK_KHR_incremental_present](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_incremental_present.html) | frame damage, top-left, image pixels | Rects are relative to `currentTransform` (revision 2). 0 rects means the whole image. All-empty rects mean nothing changed. Pixels outside must still be right. |
| [Vulkan WSI](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html) | no age at all | "Presentation is a read-only operation that will not affect the content of the presentable images". A re-acquired image holds what it held, but only if `clipped = VK_FALSE` and the image leaves `PRESENT_SRC_KHR` (not `UNDEFINED`) with `LOAD_OP_LOAD`. New swapchain images are `UNDEFINED`. Apps track ages per image index. |
| [DXGI Present1](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-2-presentation-improvements) | dirty and scroll rects | Flip-sequential: the *runtime* copies everything outside the dirty rects from the previous frame. The app "must not update any pixel outside of the dirty rectangles", and must update every pixel inside them. `FLIP_DISCARD` forbids partial presentation. |
| [X11 DAMAGE](https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/damageproto.txt), [Present](https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/presentproto.txt) | — | DAMAGE reports pixel changes to consumers (compositors, VNC), from raw rects down to a bounding box. `PresentPixmap` takes a `valid-area` and an `update-area`. |
| [NSView](https://developer.apple.com/documentation/appkit/nsview/setneedsdisplay(_:)), CAMetalLayer | invalid rects | `setNeedsDisplay(_:)` grows an invalid region and `getRectsBeingDrawn` returns disjoint rects. Metal has no partial present, and nothing promises that a drawable keeps its contents, so it is treated as age 0. |
| [Mali transaction elimination](https://www.arm.com/technologies/graphics-technologies/transaction-elimination) | — | A CRC per 16×16 tile. A tile that equals the previous frame is not written. It saves bandwidth, not shading. Transitions from `UNDEFINED` invalidate the signatures. |

**Origins differ.** The EGL rects are bottom-left. Wayland, Vulkan and DXGI are top-left.

## How repair is done

### Age-indexed history

Keep the damage of the last few frames, and for a buffer of age *a* redraw the union of the last *a*−1 frames. If the
age is 0 or older than the history, redraw everything.

| Project | Where | History | Notes |
|---|---|---|---|
| KWin | `src/utils/damagejournal.h` | 10 | `accumulate(age, Region::infinite())`. Slot ages follow the EGL rule in `EglSwapchain::release()`, right after rendering. `KWIN_USE_BUFFER_AGE=0` disables it. |
| Mutter | `clutter/clutter/clutter-damage-history.c` | 16 | Redraws whole for the first 3 frames ("junk frames when starting up"). The repair goes to `eglSetDamageRegion`, the frame damage to the swap. |
| GTK (GL) | `gdk/gdkglcontext.c` | 4 | Sends the repair, not the frame damage, to `eglSwapBuffersWithDamage`, so it over-reports. Bug: an empty slot after a resize returns a partial union instead of everything. |
| Weston ≤ 12 | `gl-renderer.c` `output_get_damage` | 2 | Weston 13 keeps "virtual renderbuffers" per EGL age instead. |
| wlroots ≤ 0.18 | `wlr_damage_ring_get_buffer_damage` | 2 ("for triple buffering") | Removed in 0.19. |
| WebRender | `renderer/mod.rs` `BufferDamageTracker` | 4 | A single rect. |
| HWUI | `CanvasContext::computeDirtyRect` | 8 ("at least 4 because we do quad buffer") | A single SkRect. A frame that wasn't drawn is recorded as an `INT_MAX` rect. |
| Flutter (Android, old) | `android_egl_surface.cc` | 10 | Never checked that the age exceeds the history, and didn't record full frames. Now disabled (flutter#123353). |

### Per-buffer accumulated damage

Each buffer keeps what it lacks. A frame's damage goes into every other buffer, and the drawn one is cleared. There is
no age and no limit, any order works, and it needs the identity of the buffer.

| Project | Where | Notes |
|---|---|---|
| GTK (Vulkan, Wayland SHM) | `gdk/gdkvulkancontext.c` | Unions into every image before acquiring. Sends the whole repaint as `VkPresentRegionsKHR`, so it over-reports. |
| Chromium | `buffer_queue.cc` `UpdateBufferDamage`, `SkiaOutputDeviceVulkan::damage_of_images_` | A single `gfx::Rect`. A skipped present damages every buffer. A client frame skipped between aggregations becomes full damage. |
| Weston 13+ | `gl_renderer_update_renderbuffers`, `pixman_renderer_repaint_output` | Explicit renderbuffers. The Vulkan renderer scissors to the extents. |
| wlroots 0.18+ | `wlr_damage_ring_rotate_buffer` | A list of buffers, most recent first, each holding the damage of the frame drawn into it. Repair is the union of the newer entries. An unknown buffer gets full damage. Swapchain capacity is 4. |
| Flutter (Metal) | `gpu_surface_metal_impeller.mm` | Keyed by texture pointer. Entries are never erased. |
| Qt (Wayland SHM, macOS, linuxfb DRM) | `qtbase/src/plugins/platforms/wayland/qwaylandshmbackingstore.cpp` | Per-buffer dirty regions, filled by copying (below). See [Qt in detail](#qt-in-detail). |

### Copying instead of redrawing

The client fills what the new buffer lacks by copying from the last presented buffer, then draws only the new damage.
This suits retained drawing, where the client can't redraw arbitrary areas.

- **Qt Wayland SHM** picks the free buffer of the least age, copies its dirty region from the front buffer at flush
  (`finalizeBackBuffer`), then sends only the flush region. It keeps at most 5 buffers.
- **Android `Surface::lock`** copies `mDirtyRegion − newDirty` from the front buffer. It has a bug:
  `mDirtyRegion.subtract(slot.dirtyRegion)` discards its result, so the region only grows (over-copying, never wrong).
- **Gecko's Wayland SHM** (`WindowSurfaceWaylandMultiBuffer.cpp`) keeps only one frame of history.
- **DXGI flip-sequential** has the runtime do the copy.

### Mesa, between applications and compositors

- **EGL on Wayland** (`platform_wayland.c`):
  - Up to 4 buffers, with the EGL age rule.
  - It picks the released buffer of the **smallest positive age**.
  - Buffers released and older than 20 are freed.
  - `EGL_KHR_partial_update` exists only on drivers that implement it (etnaviv, iris, lima, panfrost, zink).
- **Vulkan WSI on Wayland** (`wsi_common_wayland.c`):
  - `VkPresentRegionsKHR` becomes one `damage_buffer` per rect. No rects means `INT32_MAX` damage.
  - Acquire returns the lowest-index released image, or the least recently presented one with explicit sync.
- **Headless WSI** returns images in the order 0,1,0,2,0,1,0,3, so ages go up to 2^N−1 and short histories fall back to
  whole redraws often.

## Qt in detail

Qt 6 `dev` (qtbase `7faa468198d1`, qtdeclarative `5c096a1c4f6f`). The Wayland client now lives in qtbase under
`src/plugins/platforms/wayland/`.

**Widgets are precise.**
- `QWidgetRepaintManager` keeps exact `QRegion`s: one per dirty widget, and a top-level one for moves, resizes, shows
  and hides.
- Opaque siblings and children are subtracted.
- Opaque widgets that nothing covers are painted directly, without composing their parents.
- The flush region is what was painted, plus scroll and move blits.
- There is no rect-count or bounding-rect heuristic.
- Everything is repainted only when the backing store size no longer matches the window and static contents are
  unavailable (only Windows and macOS have them).
- Repaints are posted as `UpdateRequest`s, not paced by frame callbacks.

**The raster backends repair buffers correctly**, per buffer, by copying.
- **Wayland SHM** (`qwaylandshmbackingstore.cpp`):
  - Each buffer has a `QRegion`. Painted regions are subtracted from the back buffer and added to the others.
  - At flush, `finalizeBackBuffer` copies what the back buffer still lacks from the front buffer, after painting and
    excluding what was painted.
  - Buffer choice is LRU, at most 5 buffers, pruned at age 50.
  - One `damage_buffer` per rect, with no cap.
  - History of this code:
    - 6.7 copied the whole previous buffer.
    - 6.8 added per-buffer regions ([qtwayland 20477f13](https://github.com/qt/qtwayland/commit/20477f135136ecc4d5b4bba172390e658cb01683), 2024).
    - 6.10 added `scroll()` and skips copying what is about to be painted.
    - 6.11 moved the copy after painting ([6f25f703](https://github.com/qt/qtbase/commit/6f25f703fd37a900c139e14a33a4639502bfeae7)).
- macOS (`QCALayerBackingStore`, 3 IOSurfaces) uses the same logic.
- linuxfb's DRM backend does textbook 2-buffer repair from a shadow image.
- XCB keeps a single retained MIT-SHM image. It does a synchronous round trip before repainting an area the server may
  still be reading.

**Everything on the RHI or GL redraws and damages the whole window.**
- `rhiFlush` uploads the bounding rect of the dirty region, then clears and recomposites the whole window. It also
  copies the whole image ([QTBUG-120565](https://qt-project.atlassian.net/browse/QTBUG-120565)).
- QRhi has no damage or age API ([QTBUG-135832](https://qt-project.atlassian.net/browse/QTBUG-135832)).
- Wayland EGL is a plain `eglSwapBuffers`. With client-side decorations, the whole FBO is blitted on every swap. A
  patch for swap with damage is unmerged ([Gerrit 684340](https://codereview.qt-project.org/c/qt/qtbase/+/684340)).
- This applies to any widget window that contains a QOpenGLWidget, QQuickWidget or QRhiWidget, and to Android, iOS and
  EGLFS. D3D11 uses `FLIP_DISCARD` and `Present()`, never `Present1`.

**Qt Quick redraws every frame by design.**
- Buffer age, partial update and swap with damage were turned down ([QTBUG-82860](https://qt-project.atlassian.net/browse/QTBUG-82860),
  [QTBUG-101976](https://qt-project.atlassian.net/browse/QTBUG-101976): "we will always render the entire surface").
  Proper surface damage reporting "is impossible by design" (QTBUG-135832, 2025).
- Only the software adaptation computes occlusion-aware dirty regions (`optimizeRenderList`), and it disables them at
  fractional scale.

**Weak spots on Wayland:**
- Unpaced widget repaints grow the pool to 5 buffers, and then it blocks.
- Every subsurface flush allocates a new buffer.
- Decorations are regenerated at full surface size.
- Scrolling and partial Quick updates are off at fractional scale.
- Open bugs: [QTBUG-151525](https://qt-project.atlassian.net/browse/QTBUG-151525) (buffer reuse ignores the device pixel
  ratio) and [QTBUG-148631](https://qt-project.atlassian.net/browse/QTBUG-148631).

## Representing damage

| Representation | Used by | Cost |
|---|---|---|
| Banded region (pixman, QRegion, cairo, SkRegion, Android Region) | GTK, Qt, KWin, Weston, wlroots, Mutter | Exact. Allocates, and the rect count is unbounded. |
| One bounding rect | Chromium, WebRender (partial present caps at 1 rect on Windows, Android and GTK), HWUI, Flutter | Cheap, and two far-apart changes repaint everything between them. |
| Capped rects | wlroots: >20 → extents. Mutter: 16 for secondary-GPU copies. Gecko `nsRegion::SimplifyOutward(8)`: collapses bands, then extents | Few scissors or draw calls. |
| Tiles | WebRender picture cache (1024×512 tiles, quadtree invalidation up to 8×8 per tile). Flutter aligns damage to 32×32 on Android GL (Pixel glitches, flutter#97482). Impeller redraws whole past 70% of the frame. Mali TE | Fixed memory and cheap unions. Quantized. |

## Where damage comes from

- **Invalidation**: widgets mark rects. Qt's `QWidgetRepaintManager::markDirty`, NSView, Android `RenderNode::damageSelf`.
  HWUI's `DamageAccumulator` maps child rects through transforms, and perspective means infinite damage.
- **Diffing trees**: GSK `gsk_render_node_diff`, Flutter `DiffContext`, which keys layers by id and grows damage under
  backdrop filters. Chromium cc's `DamageTracker` damages the old and new rects of changed layers.
- **Compositors**:
  - Client damage goes from buffer to output coordinates, clipped to what is visible. Moving or restacking damages the
    old and new visible regions (`wlr_scene`, Weston `paint_node_add_damage`).
  - Scaling grows damage by the filter's reach (wlroots: `ceil(scale/2)`). Transformed views use their bounding box.
  - The output damage of a frame also goes to the hardware: DRM `FB_DAMAGE_CLIPS`, with panel self-refresh downstream.

## Debugging

- `WLR_SCENE_DEBUG_DAMAGE=highlight|rerender`
- Weston: Mod+Shift+Space then `M` (GL) or `R` (pixman)
- KWin's Show Paint effect
- `CLUTTER_PAINT=damage-region`
- GTK Inspector's "Show Graphic Updates", `GSK_DEBUG=full-redraw`
- `debug.hwui.show_dirty_regions`
- `vk-damage --flash`

## Lessons

1. **Keep the two damages apart.** Send the frame damage to the presentation engine and redraw the repair. GTK sends the
   repair and over-reports.
2. **Unknown means everything:** age 0, too old, a new buffer, a resize, a skipped frame, the first frames. The bugs
   found are all in this corner: GTK's partial union, old Flutter's missing age check, Android's discarded subtract.
3. **Buffer identity beats ages.** Projects move from age rings to per-buffer damage (Weston 13, wlroots 0.18): it
   works in any order with no cap. Ages remain for EGL.
4. **Pick the freshest buffer** when the client picks (Mesa EGL, Qt). It turns 3–4 buffers into 2 most of the time.
5. **Few rects are fine.** Nearly everyone collapses to one rect. A handful of rects is a cheap improvement.
6. **Vulkan repair is legal but explicit:** `clipped = VK_FALSE`, `PRESENT_SRC_KHR` → `LOAD_OP_LOAD`, `UNDEFINED` for
   new images. Zero present rects mean "everything", so an empty frame needs one empty rect.
7. **Mind the origin:** the EGL rects are bottom-left.

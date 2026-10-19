# poor_surface_damage: design, test harness and examples

This document explains what the library does, how it is tested, and what it costs. How other projects handle damage
is covered in [research.md](research.md).

## Two kinds of damage

Every frame deals with two different regions, and confusing them is the most common mistake (GTK does it, see the
research):

- **Frame damage** is what changed since the previous frame. This is what you send to the compositor or presentation
  engine: `wl_surface.damage_buffer`, `VkPresentRegionsKHR`, `eglSwapBuffersWithDamage` or DXGI dirty rects.
- **Repair** is what you have to redraw into the buffer you are about to draw into: the frame damage, plus the damage
  of every frame that went into other buffers since this one was last used. This is what you redraw, with scissors, a
  clip, `eglSetDamageRegionKHR` or a render area.

With a single buffer that keeps its contents (beam racing, or a compositor that copies the buffer) the two are the
same and no tracker is needed. With more buffers they differ, and redrawing only the frame damage leaves stale pixels
behind. The harness has a `naive` tracker that does exactly that, and it catches the mistake on every frame.

The frame loop looks the same with every tracker:

```
damage = what the application changed (old and new bounds of whatever moved), clipped to the surface
buffer = acquire()                  // picked by the WSI (Vulkan), or by the client (wl_shm, EGL)
repair = missed(buffer) ∪ damage
draw the scene inside repair, keep everything else (LOAD_OP_LOAD, oldLayout PRESENT_SRC_KHR)
drawn(buffer, damage)
present(buffer, damage)             // the frame damage, not the repair
```

## Region

A region holds a limited number of rects:

- Rects that overlap or share an edge are merged into their bounding box.
- When the region is full, the two rects (the new one included) whose bounding box adds the least undamaged area are
  merged, and the result absorbs any rect it now touches.
- So the rects never overlap, and redrawing a region takes at most as many scissored passes as its capacity.
- Adding a rect costs O(n) while there is room, and O(n²) once the region is full.
- `poor_damage_region_clip()` cuts the rects to the surface. Vulkan scissors and present rects must stay inside the
  image.
- Edges are computed with 64 bits, so the Wayland habit of damaging `INT32_MAX`-sized rects is safe.

The number of rects is the main trade-off. With 3 buffers at 640×480 and the harness scene (12 boxes), this is the
share of pixels redrawn per frame:

| Rects | 1 (bounding box) | 2 | 4 | 8 | 16 | 64 |
|---|---|---|---|---|---|---|
| redrawn | 53.0% | 29.1% | 7.8% | 3.3% | 3.3% | 3.3% |

A single rect, which Chromium, WebRender, HWUI and Flutter all use, repaints everything between two changes that are
far apart. A few rects avoid most of that. Once there are as many rects as separate things that change, more rects
don't help.

## Trackers

All three tell you what buffer *i* is missing. They differ in what they store and in how far back they can look.

| Tracker | Stores | `drawn()` | `get()` / `since()` | Looks back |
|---|---|---|---|---|
| buffers | a region per buffer | adds the damage to every other buffer, O(buffers) | copies one region | forever, in any order |
| history | a region per frame | stores one region | unions the last age−1 regions | as many frames as it keeps |
| map | the last frame that changed each cell | stamps the damaged cells | scans the cells | forever, in any order |

### buffers

This is what GTK (Vulkan, Wayland SHM), Chromium (BufferQueue, SkiaOutputDeviceVulkan), Weston 13+, Flutter on Metal
and Qt's raster backing stores do. You need to know which buffer you got: a Vulkan image index, a `wl_buffer` or a GBM
bo.

```c
swapchain_damage tracker;
damage_region damage = POOR_DAMAGE_REGION_INIT, repair;

poor_damage_buffers_init(&tracker, width, height);
...
poor_damage_buffers_get(&tracker, index, &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw repair */
poor_damage_buffers_drawn(&tracker, index, &damage);
```

`poor_damage_buffers_forget()` marks a single buffer as empty, for example after it was recreated.

### history

This is the EGL_EXT_buffer_age approach used by KWin, Mutter, GTK (GL), Weston up to 12, wlroots up to 0.18, HWUI and
WebRender.

- Frames are numbered from 1, and a buffer is described by the number of the frame it holds (0 means none).
- `poor_damage_frame_of_age(&history, age)` turns an EGL buffer age into that number.
- If you know which buffer you got, you can store the frame number that `drawn()` returns instead. KWin and Mesa count
  ages the same way: the buffer that was just swapped gets age 1, and every other buffer that has an age gets one more.

```c
frame_damage history = POOR_DAMAGE_HISTORY_INIT(width, height);
...
poor_damage_history_since(&history, poor_damage_frame_of_age(&history, egl_age), &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw repair */
poor_damage_history_drawn(&history, &damage);
```

A buffer that is older than the history is redrawn completely. That happens when the compositor holds on to a buffer
for a long time, or with Mesa's headless WSI, which hands out images in the order 0, 1, 0, 2, 0, 1, 0, 3. When buffers
come back in random order, the length of the history matters:

| Frames kept | 2 | 4 | 8 | 16 | buffers tracker |
|---|---|---|---|---|---|
| 3 buffers: redrawn, frames redrawn completely | 23.8%, 20.2% | 14.5%, 9.9% | 11.2%, 6.2% | 9.1%, 3.6% | 7.7% |
| 4 buffers | 48.9%, 46.2% | 21.8%, 16.6% | 12.7%, 5.9% | 10.9%, 3.7% | 9.6% |

### map

Think of the map as a texture that damage is drawn into, except that each cell stores the number of the frame that
last changed it instead of a color. What changed since frame N is then every cell with a number above N.

- This works for any N, any order of buffers and any number of buffers.
- It also works for things that aren't buffers, such as a screencast or VNC client that last saw frame N.
- Because each cell keeps only the latest frame, the map can look back forever with fixed memory.
- The array of cells is just a budget, and `init` picks a layout that fits the surface. You ask for a minimum cell
  size, and the cells are scaled up by the same factor in both directions until they fit into the budget. 2304 cells
  of 64 pixels cover 1920×1080 as 30×17 cells, and 8000×500 as 125×8 cells, while 1500×15000 gets 12×118 cells of 128
  pixels. A layout fixed at compile time would waste most of the cells on very wide or very tall surfaces.
- With one cell per pixel the map would be exact. Bigger cells simply trade precision for memory and speed.
- Rows with the same changed cells are merged into bands before they are turned into rects.
- The answer is always made of whole cells.

```c
struct screen screen;
uint64_t holds[3] = {};                                  /* the frame that each buffer holds */

poor_damage_map_init(&screen.map, width, height, 64, 64);
...
poor_damage_map_since(&screen.map, holds[index], &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw repair */
holds[index] = poor_damage_map_drawn(&screen.map, &damage);
```

Cell size trades precision for speed (3 buffers, 640×480, a budget of 16384 cells):

| Minimum cell | 1 px | 8 px | 16 px | 32 px | 64 px |
|---|---|---|---|---|---|
| redrawn | 8.2% | 8.7% | 9.8% | 13.1% | 22.7% |
| compared to the exact union | 1.26 | 1.33 | 1.50 | 2.01 | 3.48 |
| µs per frame | 13.1 | 5.8 | 2.3 | 1.3 | 0.9 |

### Sizing for a use case

| Case | Declaration | Bytes |
|---|---|---|
| beam racing: one buffer that is presented and kept | just a region, since the repair is the damage | 72 |
| desktop window with triple buffering | `poor_damage_buffers_declare(t, 3, 4)` | 232 |
| EGL with buffer age | `poor_damage_history_declare(h, 4, 4)`, for ages up to 5 | 304 |
| 4K output with consumers asking what changed since frame N | a map over `uint64_t cells[2304]`, 64 px cells | 18472 |
| text editor stretched in VR, 1500×15000 | a map over `uint64_t cells[5760]`, 64 px cells | 46120 |
| SQL result with many columns, 8000×500 | a map over `uint64_t cells[1024]`, 64 px cells | 8232 |

Huge surfaces where only a few small areas change also work well with `buffers` and 8–16 rects. That costs a few
hundred bytes and repairs exactly, but rects get merged when the damage is spread out.

### When buffers are recreated

After a resize, or with a new swapchain or buffer pool:

- `buffers`: call init again, or `forget()` the buffers that were recreated.
- `history` and `map`: call init again, and reset the frame of every buffer to 0.
- Damage everything in the first frame. The presentation engine has nothing to show yet, and Mutter even redraws the
  first 3 frames completely because some drivers produce garbage at first.

### Which tracker to use

- **You know which buffer you got** (Vulkan, wl_shm, GBM): `buffers`. It is the simplest, and exact for any order and
  any age.
- **You only know the EGL buffer age**: `history`, as long as ages stay within the number of frames it keeps.
- **Something asks what changed since frame N for any N, or there are many buffers**: `map`.
- **You pick the buffer yourself** (wl_shm, EGL on Mesa): take the free buffer that was presented most recently. In
  the simulation, taking the newest buffer first with 4 buffers redraws 5.5% of the pixels against 9.2% in FIFO order,
  and `shm-damage` on KWin shows the same: 5.3% against 8.8%. Mesa's EGL and Qt pick buffers this way.

## Test harness

`harness/` contains the code shared by the tests and the examples:

- `scene`: boxes over a checkerboard of 16 pixel squares that bounce, blink, grow and change colors. Every 240 frames
  the checkerboard changes colors, which damages the whole surface. Each step reports its damage: the old and new
  bounds of whatever moved, or the strip a progress bar grew by.
- `engine`: a fake presentation engine that hands buffers back in one of three orders:
  - `fifo`: round robin,
  - `newest`: the most recently presented free buffer, like Mesa's EGL,
  - `random`: buffers are held for random times, sometimes for 4–27 frames.
- `tracker`: the library's trackers behind one interface, plus `whole` (redraw everything) and `naive` (redraw only
  the frame damage). Capacities are chosen at runtime: the storage is sized for the largest ones, and the library gets
  views of it.
- `sim`: draws every frame into the engine's buffers, redrawing only the repair, and compares the result with a frame
  drawn from scratch. Buffers start out with garbage, and the surface can be resized every N frames.

`tests/sim-test` runs every tracker with every buffer order and 1–4 buffers, with resizes, and with extreme
capacities: regions of 1 rect, a history of 1 frame, maps of 1 and 16384 cells, and cells of 7×2 pixels. It checks
that:

- no frame ever comes out wrong,
- every repair covers the exact union of the damage that the buffer missed,
- `naive` fails with 2 buffers and works with 1,
- more rects redraw less.

`damage-sim` prints the full comparison and accepts the capacity options. By default it runs 640×480 for 2000 frames.
The table below is for 3 buffers. *redrawn* is the share of pixels that were redrawn, */exact* compares the repair with
the exact union of the damage the buffer missed, and *µs* is the time spent in the tracker per frame.

| Tracker | fifo | newest | random | random: frames redrawn completely | /exact (fifo) | µs |
|---|---|---|---|---|---|---|
| whole | 100% | 100% | 100% | 100% | 15.9 | 0.1 |
| naive | wrong | wrong | wrong | — | — | 0.2 |
| buffers | 7.34% | 5.53% | 7.22% | 1.4% | 1.16 | 0.6–0.7 |
| history | 7.34% | 5.53% | 10.74% | 6.1% | 1.16 | 0.5–0.7 |
| map | 9.36% | 7.33% | 8.50% | 1.4% | 1.48 | 2.2–2.4 |

With 1 buffer every tracker redraws 3.55%, which is exactly the frame damage. With 4 buffers in random order,
`history` redraws 12.3% against 9.0% for `buffers`, because buffers older than the history are redrawn completely. All
trackers redraw about 3–4 times more pixels than actually changed, but that comes from rect damage itself (the old and
new bounds of a moving box), not from the trackers.

`damage-bench` times the trackers without drawing anything. It records the damage of five workloads (the scene at
640×480 and 1920×1080, widgets that change at random, small rects scattered over 3840×2160, and typing), replays it
with every buffer order, 1–4 buffers and 4–64 rects, and prints the CPU cycles that the tracker takes per frame, the
share of them spent in `drawn()`, and the share of pixels redrawn. It counts cycles with perf, so it needs
`kernel.perf_event_paranoid = -1`. `--verify` checks every repair pixel by pixel instead. To compare two builds, run it
in both and pass the outputs to `harness/compare-bench.py`.

## Examples

The examples are built with `-Dexamples=true` and need Vulkan, wayland-client, wayland-protocols, wayland-scanner and
glslc. Both accept the harness options `--tracker`, `--rects`, `--history`, `--cells` and `--cell-size`.

### vk-damage

Draws the scene with Vulkan 1.3, using dynamic rendering and synchronization2, into one of these targets:

- `wayland`: an xdg-shell window,
- `headless`: a real swapchain without a display, through `VK_EXT_headless_surface`,
- `offscreen`: images handed out by the harness engine in any order.

Repairing swapchain images relies on these guarantees of the Vulkan spec:

- Presenting is read-only, so a re-acquired image still holds what was drawn into it. This requires
  `clipped = VK_FALSE`, `oldLayout = PRESENT_SRC_KHR` and `LOAD_OP_LOAD`. New images start out `UNDEFINED`, and the
  trackers treat them as empty.
- Each repair rect is drawn in its own scissored pass. The render area is the bounding box of the repair, and
  `LOAD_OP_DONT_CARE` is used when everything is redrawn.
- `VkPresentRegionsKHR` carries the frame damage. Zero rects would mean that everything changed, so a frame without
  changes passes one empty rect.
- There is one present semaphore per swapchain image, as the Vulkan guide recommends.

Useful options:

- `--verify` draws a reference frame from scratch on the GPU every frame, reads both images back and compares them.
- `--flash` tints what was redrawn.
- `--resize N` recreates everything every N frames.
- `--present`, `--images`, `--order` and `--device`.

`meson test --suite vulkan` runs the offscreen and headless targets with resizes and verification for every tracker.
Results:

| Run | buffers | history | map |
|---|---|---|---|
| KWin window, RADV, 4 images, FIFO, 240 frames | 8.82% redrawn, 0 wrong | 8.82% redrawn, 0 wrong | 11.54% redrawn, 0 wrong |
| Mesa headless WSI, 5 images, 300 frames | 12.81%, 10 complete redraws | 18.38%, 35 complete redraws | 15.69%, 10 complete redraws |

All of this ran under `VK_LAYER_KHRONOS_validation` on RADV and lavapipe without a single message.

### shm-damage

A `wl_shm` client. It picks one of the buffers that the compositor has released, by default the one presented most
recently (`--oldest` picks the oldest), and sends the frame damage with `wl_surface.damage_buffer`.

- It draws a new frame on a frame callback, after a configure event, or after 100 ms without a frame callback, like Qt
  does. A window that the compositor doesn't show gets no frame callbacks.
- `--copy` repairs buffers the way Qt, Android and Gecko do:
  1. copy what the buffer missed from the last presented buffer,
  2. redraw only the frame damage.

  On KWin with 3 buffers it redraws 3.6% of the pixels and copies 3.6%, instead of redrawing 5.3%.
- `--verify` compares every frame with one drawn from scratch.

## Ideas for later

- Resolution for every tracker. Cell size isn't specific to the map: storing damage in cells is an alternative to
  storing a limited number of rects, and either works with any kind of bookkeeping:

  | Keeps | Rects (limited count) | Cells (cell size) |
  |---|---|---|
  | per buffer | `buffers` | a bitmap per buffer, 1 bit per cell, where adding damage is an OR |
  | per frame | `history` | a bitmap per frame |
  | per place, the latest frame | rects that carry the frame that last changed them; a merge keeps the newer frame | `map` |

  The cheapest first step would be a cell size for regions that snaps rects as they are added. Snapping saves little
  memory, but rects merge more often and scissors stay aligned.

- An exact region with a larger rect limit for the damage sent to compositors, since they accept any number of rects.
  wlroots limits it to 20 rects and Gecko to 8.
- `EGL_KHR_partial_update`: the repair is exactly what `eglSetDamageRegionKHR` expects. An EGL example would show
  `history` with real buffer ages.
- A history that merges old frames into coarser buckets, so it can look further back with fixed memory.
- Refining damage by hashing cells, like wayvnc does in software and Mali's transaction elimination does in hardware,
  for producers that can't tell what changed.

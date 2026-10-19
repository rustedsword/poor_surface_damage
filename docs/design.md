# poor_surface_damage: algorithms, harness and examples

What the library does, how it is checked, and what it costs. Research on how others do it is in
[research.md](research.md).

## Two kinds of damage

A frame has two regions, and mixing them up is the most common mistake (see GTK in the research):

- **Frame damage**: what changed since the previous frame. It goes to the presentation engine:
  `wl_surface.damage_buffer`, `VkPresentRegionsKHR`, `eglSwapBuffersWithDamage`, DXGI dirty rects.
- **Repair**: what the buffer about to be drawn lacks. It is the frame damage plus the damage of every frame drawn since
  the frame that buffer holds. It is what gets redrawn (scissors, clip, `eglSetDamageRegionKHR`, render area).

With one buffer whose content is kept (beam racing, a copying compositor), the two are equal and no tracker is needed.
With N buffers they differ, and redrawing only the frame damage leaves stale pixels. The `naive` tracker in the harness
does exactly that, and the harness catches it on every frame.

Every frame of an application does the same thing whatever the tracker:

```
damage   = what the application changed (old and new bounds of what moved), clipped to the surface
buffer   = acquire()                  // the WSI picks it (Vulkan), or the client does (wl_shm, EGL)
repair   = lacks(buffer) ∪ damage
draw the scene within repair, keeping everything else (LOAD_OP_LOAD, oldLayout PRESENT_SRC_KHR)
drawn(buffer, damage)
present(buffer, damage)               // the frame damage, not the repair
```

## Layout

One header and one source per module, following poor_loop:

| Header | Source | What |
|---|---|---|
| `poor_damage_region.h` | `region.c` | rects and regions |
| `poor_damage_surface.h` | — | size and frame counter shared by trackers, EGL ages |
| `poor_damage_buffers.h` | `buffers.c` | what each buffer lacks |
| `poor_damage_history.h` | `history.c` | damage of the last frames |
| `poor_damage_map.h` | `map.c` | raster of the last frame that changed each cell |
| `poor_surface_damage.h` | — | includes all of them |

## Capacities belong to the application

Nothing has a fixed size. Each type is declared with the capacities the application needs, like poor_loop's groups,
and lives wherever the application puts it: on the stack, in a static, or inside its own structs.

```c
poor_damage_region_declare(damage_region, 4);           /* up to 4 rects */
poor_damage_buffers_declare(swapchain_damage, 3, 4);    /* 3 buffers, 4 rects each */
poor_damage_history_declare(frame_damage, 8, 4);        /* 8 frames, 4 rects each */
poor_damage_map_declare(screen_map, 64, 36);            /* a raster of up to 64 x 36 cells */
```

- The macros take the capacities from the array sizes, with poor_base's `ARRAY_SIZE`.
- They pass the arrays to `_impl` functions as pointers to variable length arrays.
- Regions of different capacities mix freely. A tracker can store 4 rects per buffer and answer into a repair region of
  16 rects.
- Capacities that are only known at run time use the `_impl` functions directly, with `arrview` views of storage sized
  for the largest case. The harness does this: `--rects`, `--history`, `--grid` and `--cell` pick capacities per run.
- Trailing macro arguments are variadic, so `poor_damage_region_add(&r, (struct poor_damage_rect){ x, y, w, h })` works.
- The macros may evaluate the tracker or region argument more than once.
- Following AGENTS.md:
  - `POOR_DAMAGE_REGION_INIT` and `POOR_DAMAGE_HISTORY_INIT(w, h)` set only what is required.
  - The init functions do the same.
  - Buffers and the map only have init functions, since their initial state depends on capacities.

## Region

A region holds up to its capacity of rects:

- Rects that overlap or share an edge merge into their bounds.
- When the region is full, the two rects (the new one included) whose bounds cover the fewest pixels that didn't change
  merge, and the merged rect takes in any rect it now reaches.
- So the rects stay disjoint, and a repair costs at most *capacity* scissored passes.
- Adding a rect costs O(n) while there is room and O(n²) when the region is full.
- `poor_damage_region_clip()` keeps what lies within the surface. Vulkan scissors and present rects must not go outside
  the image.
- Edges are computed in 64 bits, so the Wayland idiom of `INT32_MAX`-sized damage is safe.

How many rects to keep is the main trade-off. With 3 buffers at 640×480 in the harness's scene (12 boxes), the share of
pixels redrawn per frame is:

| Rects | 1 (bounding box) | 2 | 4 | 8 | 16 | 64 |
|---|---|---|---|---|---|---|
| redrawn | 53.0% | 29.1% | 7.8% | 3.3% | 3.3% | 3.3% |

A single rect, which Chromium, WebRender, HWUI and Flutter all use, repaints everything between far-apart changes. A
handful of rects removes most of that. Past the number of separate things that change, more rects buy nothing.

## Trackers

All three answer "what does buffer *i* lack". They differ in what they remember and in how far back they reach.

| Tracker | State | `drawn()` | `lacks()` | Reaches back |
|---|---|---|---|---|
| buffers | a region per buffer | union into every other buffer, O(buffers) | copy one region | forever, any order |
| history | a region per frame | store one region | union of age−1 regions | its frames, then everything |
| map | the last frame that changed each cell | stamp cells | scan the raster | forever, any order |

### buffers

This is the scheme of GTK (Vulkan, Wayland SHM), Chromium (BufferQueue, SkiaOutputDeviceVulkan), Weston 13+, Flutter
Metal and Qt's raster backing stores. It needs the identity of the buffer: a Vulkan image index, a `wl_buffer`, a GBM
bo.

```c
swapchain_damage lacks;
damage_region damage = POOR_DAMAGE_REGION_INIT, repair;

poor_damage_buffers_init(&lacks, width, height);
...
poor_damage_buffers_lacks(&lacks, index, &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw repair */
poor_damage_buffers_drawn(&lacks, index, &damage);
```

`poor_damage_buffers_forget()` marks a single buffer that was recreated.

### history

This is the EGL_EXT_buffer_age scheme of KWin, Mutter, GTK (GL), Weston ≤ 12, wlroots ≤ 0.18, HWUI and WebRender.
- Frames are numbered from 1, and a buffer is known by the number of the frame it holds (0: nothing).
- For an EGL age, `poor_damage_frame_of_age(&history, age)` gives that number.
- Applications that know which buffer they got can store the number that `drawn()` returns. KWin and Mesa compute ages
  that way: the buffer that was swapped gets age 1, and every other buffer that has an age gets one more.

```c
frame_damage history = POOR_DAMAGE_HISTORY_INIT(width, height);
...
poor_damage_history_lacks(&history, poor_damage_frame_of_age(&history, egl_age), &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw repair */
poor_damage_history_drawn(&history, &damage);
```

A buffer older than the history gets a whole redraw. That happens when a compositor holds a buffer for long, or with
Mesa's headless WSI, which hands out images 0,1,0,2,0,1,0,3. With buffers released in random order, the history length
matters:

| Frames | 2 | 4 | 8 | 16 | (buffers tracker) |
|---|---|---|---|---|---|
| 3 buffers: redrawn, frames redrawn whole | 23.8%, 20.2% | 14.5%, 9.9% | 11.2%, 6.2% | 9.1%, 3.6% | 7.7% |
| 4 buffers | 48.9%, 46.2% | 21.8%, 16.6% | 12.7%, 5.9% | 10.9%, 3.7% | 9.6% |

### map

Damage is drawn into a raster the way rendering draws into a texture, except that cells hold the number of the frame
that last changed them rather than colors. "What changed since frame N" is then every cell above N.
- It works for any N, any buffer order and any number of buffers.
- It also serves consumers that are not buffers at all, such as a screencast or VNC client that last saw frame N.
- Keeping only the latest frame per cell is what lets it reach back forever in fixed memory.
- The raster is declared with up to C × R cells. `init` asks for a least cell size, and cells grow by its multiples when
  the raster can't cover the surface otherwise: 64×36 cells of 64 pixels cover 4096×2304, and an 8K surface gets cells
  of 128.
- One cell per pixel would be exact. Larger cells are only a trade of precision for memory and time.
- Rows that changed the same cells merge into bands before they become rects.
- Answers are whole cells.

```c
screen_map map;
uint64_t holds[3] = {};                                  /* the frame each buffer holds */

poor_damage_map_init(&map, width, height, 64, 64);
...
poor_damage_map_lacks(&map, holds[index], &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw repair */
holds[index] = poor_damage_map_drawn(&map, &damage);
```

Cell size trades precision for time (3 buffers, 640×480, raster of up to 128×128):

| Least cell | 1 px | 8 px | 16 px | 32 px | 64 px |
|---|---|---|---|---|---|
| redrawn | 8.2% | 8.7% | 9.8% | 13.1% | 22.7% |
| ÷ exact union | 1.25 | 1.33 | 1.50 | 2.01 | 3.48 |
| µs per frame | 15.1 | 5.9 | 2.5 | 1.3 | 0.9 |

### Sizing for a use case

| Case | Declaration | Bytes |
|---|---|---|
| beam racing, one buffer that is presented and kept | only a region: repair = damage | 72 |
| desktop window, triple buffering | `poor_damage_buffers_declare(t, 3, 4)` | 232 |
| EGL with buffer age | `poor_damage_history_declare(h, 4, 4)` (ages up to 5) | 304 |
| 4K output, consumers asking "since frame N" | `poor_damage_map_declare(m, 64, 36)`, 64 px cells | 18472 |
| text editor stretched in VR, 1500×15000 | `poor_damage_map_declare(m, 24, 240)`, 64 px cells | 46120 |
| SQL result of many columns, 8000×500 | `poor_damage_map_declare(m, 128, 8)`, 64 px cells | 8232 |

Huge surfaces with few, local changes also suit `buffers` with 8–16 rects. It costs a few hundred bytes and repairs
exactly, at the price of merging rects when the damage scatters.

### When buffers are recreated

On resize, or a new swapchain or pool:

- `buffers`: init again, or `forget()` the buffers that changed.
- `history` and `map`: init again, and set every buffer's frame back to 0.
- The first frame damages everything. The presentation engine has nothing yet, and Mutter even redraws the first 3
  frames whole because of junk from drivers.

### Which one

- **The buffer is known** (Vulkan, wl_shm, GBM): `buffers`. It is simplest and exact for any order and any age.
- **Only an EGL age is known**: `history`, as long as the age stays within its frames.
- **Consumers ask "since frame N" for arbitrary N, or there are many buffers**: `map`.
- **Picking the buffer yourself** (wl_shm, EGL on Mesa): take the free buffer that was presented last. In the
  simulation, the freshest-first order (`newest`) of 4 buffers redraws 5.5% of the pixels against 9.2% for FIFO.
  `shm-damage` on KWin shows the same: 5.3% against 8.8%. Mesa's EGL and Qt pick buffers this way.

## Harness

`harness/` holds what the tests and the examples share:

- `scene`: boxes over a 16-pixel checkerboard that bounce, blink, grow and change color. Every 240 frames the board
  changes colors, which damages everything. Each step reports its damage: the old and new bounds of what moved, or the
  strip a bar grew by.
- `engine`: a presentation engine with three orders of handing buffers back:
  - `fifo`: round robin.
  - `newest`: the freshest released buffer, as Mesa's EGL does.
  - `random`: holds of random length that sometimes last 4–27 frames.
- `tracker`: the library's trackers behind one interface, plus `whole` (redraw everything) and `naive` (redraw only the
  frame damage). Their capacities are picked at run time: storage is sized for the largest, and the library gets views
  of it.
- `sim`: draws every frame into the engine's buffers, redrawing only the repair. It then compares the buffer with a
  frame drawn whole. Buffers start as garbage and the surface can be resized every N frames.

`tests/sim-test` runs every tracker over every order and 1–4 buffers with resizes, and also extreme capacities. It
checks:

- no frame is ever wrong,
- every repair covers the exact union of the damage the buffer lacked,
- `naive` is caught with 2 buffers and is right with 1,
- more rects redraw less.

The extreme capacities include 1-rect regions, a 1-frame history, 1×1 and 128×128 rasters, and 7×2 cells.

`damage-sim` prints the whole comparison and takes the capacity options. The default run is 640×480 over 2000 frames.
The table below uses 3 buffers. *redrawn* is the share of pixels that were redrawn. */exact* compares the repair with
the exact union of the damage that the buffer lacked. *µs* is the time the tracker took per frame.

| Tracker | fifo redrawn | newest redrawn | random redrawn | random: frames redrawn whole | /exact (fifo) | µs |
|---|---|---|---|---|---|---|
| whole | 100% | 100% | 100% | 100% | 15.9 | 0.1 |
| naive | wrong | wrong | wrong | — | — | 0.2 |
| buffers | 7.34% | 5.53% | 7.22% | 1.4% | 1.16 | 0.6–0.7 |
| history | 7.34% | 5.53% | 10.74% | 6.1% | 1.16 | 0.5–0.7 |
| map | 9.36% | 7.33% | 8.50% | 1.4% | 1.48 | 2.2–2.4 |

With 1 buffer every tracker redraws 3.55%, exactly the frame damage. With 4 buffers in random order, `history` redraws
12.3% against 9.0% for `buffers`, because old buffers fall back to whole redraws. The repair is about 3–4 times the
pixels that actually differ, and the cause is the rect damage itself (the old and new bounds of a moving box), not the
trackers.

## Examples

Both examples are built when Vulkan, wayland-client, wayland-protocols, wayland-scanner and glslc are found
(`-Dexamples=auto`). Both take the harness's `--tracker`, `--rects`, `--history`, `--grid` and `--cell` options.

### vk-damage

Draws the scene with Vulkan 1.3, using dynamic rendering and synchronization2. Targets:

- `wayland`: an xdg-shell window.
- `headless`: `VK_EXT_headless_surface`, a real WSI with no display.
- `offscreen`: images that the harness engine hands out in any order.

Repairing a swapchain image relies on rules the Vulkan spec guarantees:

- Presentation is read-only, and a re-acquired image holds what was drawn into it. This needs `clipped = VK_FALSE`,
  `oldLayout = PRESENT_SRC_KHR` and `LOAD_OP_LOAD`. New images start `UNDEFINED`, and the trackers treat them as
  holding nothing.
- One scissored pass per repair rect. The render area is the bounds of the repair, and `LOAD_OP_DONT_CARE` is used when
  the repair is whole.
- `VkPresentRegionsKHR` carries the frame damage. Zero rects mean everything changed, so a frame where nothing changed
  sends one empty rect instead.
- One present semaphore per swapchain image, as the Vulkan guide asks.

Useful options:

- `--verify` renders a whole reference frame on the GPU each frame and compares both images, read back.
- `--flash` tints what was redrawn.
- `--resize N` recreates everything every N frames.
- `--present`, `--images`, `--order`, `--device`.

`meson test --suite vulkan` runs offscreen and headless verification with resizes for every tracker. The results:

| Run | buffers | history | map |
|---|---|---|---|
| KWin window, RADV, 4 images, FIFO, 240 frames | 8.82%, 0 wrong | 8.82%, 0 wrong | 11.54%, 0 wrong |
| Mesa headless WSI, 5 images, 300 frames | 12.81%, 10 whole | 18.38%, 35 whole | 15.69%, 10 whole |

These were run with `VK_LAYER_KHRONOS_validation` on RADV and lavapipe, and it reported nothing.

### shm-damage

A `wl_shm` client. It picks among the buffers the compositor released, the freshest by default (`--oldest` for the
other order), and sends the frame damage with `wl_surface.damage_buffer`.
- It draws on frame callbacks, after a configure, or after 100 ms without a frame callback, as Qt does. A window the
  compositor doesn't show gets no callbacks.
- `--copy` repairs the way Qt, Android and Gecko do:
  1. Copy what the buffer lacks from the buffer presented last.
  2. Draw only the frame damage.

  On KWin with 3 buffers, it draws 3.6% of the pixels and copies 3.6%, instead of drawing 5.3%.
- `--verify` compares every frame with one drawn whole.

## Ideas not done

- Resolution for every tracker. Cell size isn't specific to the map. It is one way to store damage, next to rects with
  a budget, and either way works with any bookkeeping:

  | Kept | Rects (rect budget) | Raster (cell size) |
  |---|---|---|
  | per buffer | `buffers` | a bitmap per buffer, 1 bit per cell, where union is OR |
  | per frame | `history` | a bitmap per frame |
  | per place, latest frame | rects carrying the frame that last changed them, where a merge keeps the newer frame | `map` |

  The cheapest step is a cell size on regions that snaps rects as they are added. Snapping saves little memory, but
  makes rects merge more often and keeps scissors aligned.

- An exact banded region with a larger cap, for the damage sent to compositors, which take any number of rects.
  wlroots caps at 20 and Gecko at 8.
- `EGL_KHR_partial_update`: the repair is exactly what `eglSetDamageRegionKHR` wants. An EGL example would show
  `history` with real ages.
- A history whose old frames merge into coarser buckets, to reach further back with fixed memory.
- Refining damage by hashing tiles, as wayvnc does and Mali's transaction elimination does in hardware, for producers
  that can't tell what changed.

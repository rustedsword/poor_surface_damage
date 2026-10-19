# poor_surface_damage

A small C library for damage tracking. It answers two questions every frame:

- What changed since the previous frame? This is what you tell the compositor or presentation engine.
- What does the buffer I'm about to draw into have to redraw? With 2 or 3 buffers in a swapchain, a reused buffer
  has missed the changes of the frames that went into the other buffers, so it needs those as well.

Damage is kept as a few rects, so redrawing it takes only a few draw calls. There are three ways to track what a
buffer is missing:

- buffers: every buffer keeps the damage it has missed. Use this when you know which buffer you got, like a Vulkan
  image index or a wl_buffer.
- history: the damage of the last few frames. Use this with EGL_EXT_buffer_age.
- map: a low resolution image of the frame that last changed each part of the surface. This works for buffers of any
  age, and for anything else that wants to know what changed since frame N.

The library never allocates memory. You declare each type with the capacities you need and put it wherever you like:

```c
poor_damage_region_declare(damage_region, 4);           /* up to 4 rects */
poor_damage_buffers_declare(swapchain_damage, 3, 4);    /* 3 buffers, up to 4 rects each */

swapchain_damage tracker;
damage_region damage = POOR_DAMAGE_REGION_INIT, repair;

poor_damage_buffers_init(&tracker, width, height);
...
poor_damage_region_add(&damage, (struct poor_damage_rect){ x, y, w, h });
poor_damage_buffers_get(&tracker, index, &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw 'repair' into buffer 'index', then present it with 'damage' */
poor_damage_buffers_drawn(&tracker, index, &damage);
```

[docs/design.md](docs/design.md) explains the trackers, how to size them, the test harness that checks them pixel by
pixel, and the examples. [docs/research.md](docs/research.md) describes how compositors, toolkits, browsers and mobile
platforms track damage.

Building needs a C23 compiler, Meson 1.4 and [poor_base](https://github.com/rustedsword/poor_base), either installed
or from the submodule. The examples also need Vulkan, wayland-client, wayland-protocols and glslc.

    $ meson setup build/
    $ ninja -C build/
    $ meson test -C build/                              # add --no-suite vulkan if you have no Vulkan device
    $ build/harness/damage-sim --rects 8                # compares the trackers
    $ build/harness/damage-bench --tracker buffers      # times a tracker, needs kernel.perf_event_paranoid=-1
    $ build/examples/vk-damage --tracker map --flash    # a Vulkan window that tints what it redraws
    $ build/examples/shm-damage --copy --verify         # a wl_shm window that copies what a buffer missed

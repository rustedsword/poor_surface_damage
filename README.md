# poor_surface_damage

Damage tracking without allocations: what changed on a surface in a few rects, and what each buffer of a swapchain
lacks, so that only that is redrawn. Three trackers tell what a buffer lacks:

- buffers: each buffer holds the damage of the frames that were drawn since its own,
- history: the damage of the last frames, for a buffer of a known EGL age,
- map: a raster of the last frame that changed each cell, for a buffer that holds any frame.

Every type is declared with the capacities that the application needs, and lives wherever it puts it:

```c
poor_damage_region_declare(damage_region, 4);           /* up to 4 rects */
poor_damage_buffers_declare(swapchain_damage, 3, 4);    /* 3 buffers of up to 4 rects */

swapchain_damage lacks;
damage_region damage = POOR_DAMAGE_REGION_INIT, repair;

poor_damage_buffers_init(&lacks, width, height);
...
poor_damage_region_add(&damage, (struct poor_damage_rect){ x, y, w, h });
poor_damage_buffers_lacks(&lacks, index, &repair);
poor_damage_region_add_region(&repair, &damage);
/* redraw 'repair' into buffer 'index', present it with 'damage' */
poor_damage_buffers_drawn(&lacks, index, &damage);
```

[docs/design.md](docs/design.md) explains the trackers and how to size them, the harness that checks them pixel by
pixel, and the examples. [docs/research.md](docs/research.md) is how compositors, toolkits, browsers and mobile stacks
track damage.

It needs a C23 compiler, Meson 1.4 and [poor_base](https://github.com/rustedsword/poor_base), installed or from the
submodule. The examples also need Vulkan, wayland-client, wayland-protocols and glslc.

    $ meson setup build/
    $ ninja -C build/
    $ meson test -C build/                              # --no-suite vulkan without a Vulkan device
    $ build/harness/damage-sim --rects 8                # compares the trackers
    $ build/examples/vk-damage --tracker map --flash    # a Vulkan window that tints what it redraws
    $ build/examples/shm-damage --copy --verify         # a wl_shm window that copies what a buffer lacks

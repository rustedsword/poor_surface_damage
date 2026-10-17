# poor_surface_damage

libpoor_surface_damage tracks what changes on a surface, for Wayland clients and compositors, without allocations. A
region holds damage in a few rects, so that redrawing it takes few draw calls: rects that touch merge, and past the
limit the two whose bounds cover the least that didn't change. Each buffer of a swapchain holds the damage of the
frames that were drawn since its own, which a frame that goes to it redraws along with its own damage.

It needs a C23 compiler, Meson 1.4 or newer and [poor_base](https://github.com/rustedsword/poor_base). An installed
poor_base is used if there is one, the submodule otherwise.

    $ meson setup build/
    $ ninja -C build/
    $ meson test -C build/

## Use

```c
struct poor_damage_region buffers[3], damage, redraw;

/* A new buffer lacks everything */
poor_damage_region_whole(&buffers[i], width, height);

/* A frame with 'damage' goes to buffer 'i' */
redraw = buffers[i];
poor_damage_region_add_region(&redraw, &damage);
/* ... draw 'redraw', present with 'damage' ... */
poor_damage_drawn(buffers, i, &damage);
```

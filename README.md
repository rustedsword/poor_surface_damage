# poor_surface_damage

Damage tracking without allocations: what changed on a surface in a few rects, and what each buffer of a swapchain
lacks.

It needs a C23 compiler, Meson 1.4 and [poor_base](https://github.com/rustedsword/poor_base), installed or from the
submodule.

    $ meson setup build/
    $ ninja -C build/
    $ meson test -C build/

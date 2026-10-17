/* SPDX-License-Identifier: MIT */
#ifndef POOR_SURFACE_DAMAGE_H
#define POOR_SURFACE_DAMAGE_H

#include <poor_array.h>
#include <stddef.h>
#include <stdint.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * libpoor_surface_damage tracks what changes on a surface, without allocations. A region holds damage in a few rects,
 * so that redrawing it takes few draw calls, and each buffer of a swapchain holds the damage of the frames that were
 * drawn since its own.
 */

constexpr size_t POOR_DAMAGE_RECTS = 4;

struct poor_damage_rect {
	int32_t x, y, width, height;
};

/* Rects that touch merge, and past POOR_DAMAGE_RECTS the two whose bounds cover the least that didn't change */
struct poor_damage_region {
	size_t count;
	struct poor_damage_rect rects[POOR_DAMAGE_RECTS];
};

#define POOR_DAMAGE_REGION_INIT { .count = 0 }

void
poor_damage_region_init(struct poor_damage_region *region);

/* A rect without area adds nothing */
void
poor_damage_region_add(struct poor_damage_region *region, struct poor_damage_rect rect);

void
poor_damage_region_add_region(struct poor_damage_region *region, const struct poor_damage_region *other);

/* All of 'width' x 'height', which a buffer that holds nothing yet lacks */
void
poor_damage_region_whole(struct poor_damage_region *region, int32_t width, int32_t height);

/*
 * 'buffers' hold what each buffer lacks of the frames that were drawn since its own, a frame that goes to a buffer
 * redraws that and its own damage. Once a frame with 'damage' was drawn into buffer 'index', the other buffers lack
 * 'damage' too and that one lacks nothing.
 */
void
poor_damage_drawn_impl(size_t count, struct poor_damage_region (*buffers)[count], size_t index,
		       const struct poor_damage_region *damage);
#define poor_damage_drawn(buffers, index, damage) \
	poor_damage_drawn_impl(ARRAY_SIZE(buffers), &auto_arr(buffers), index, damage)

#ifdef  __cplusplus
}
#endif

#endif

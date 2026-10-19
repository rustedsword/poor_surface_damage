/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_REGION_H
#define POOR_DAMAGE_REGION_H

#include <poor_array.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct poor_damage_rect {
	int32_t x, y, width, height;
};

/*
 * Poor Damage Region.
 *
 * A set of damaged rects with a fixed capacity.
 *
 * Every rect usually costs a draw call, a scissor or a present rect, so a region keeps only a few of them. When a new
 * rect touches or overlaps a rect already in the region, both are merged into their bounding box. When the region is
 * full, the two rects whose bounding box adds the least undamaged area are merged to make room. A capacity of 1 gives
 * a plain bounding box. A bigger capacity follows the damage more closely, but costs more draw calls, and adding a rect
 * to a full region gets slower with the square of the capacity.
 *
 * Example with up to 4 rects:
 *
 *   poor_damage_region_declare(damage_region, 4);
 *
 *   damage_region damage = POOR_DAMAGE_REGION_INIT;
 *
 *   poor_damage_region_add(&damage, (struct poor_damage_rect){ x, y, width, height });
 *   poor_damage_region_clip(&damage, surface_width, surface_height);
 *
 *   foreach_array_const_ref(arrview_first(damage.count, damage.rects), rect)
 *           ... redraw *rect ...
 *
 * Regions of different capacities can be mixed freely. The macros below may evaluate their region arguments more
 * than once. If the capacity is only known at runtime, call the _impl functions directly with views of your arrays.
 */

/* Declare a region type 'name' that holds up to 'capacity' rects. */
#define poor_damage_region_declare(name, capacity) h_damage_region_declare(name, capacity)

#define POOR_DAMAGE_REGION_INIT { .count = 0 }

/* Make 'region' empty. */
#define poor_damage_region_init(region) ((void)((region)->count = 0))

/* Add a rect to 'region'. Rects with zero or negative width or height are ignored. */
#define poor_damage_region_add(region, ...) poor_damage_region_add_impl(h_damage_region(region), __VA_ARGS__)

/* Add all rects of another region to 'region'. */
#define poor_damage_region_add_region(region, ...) \
	poor_damage_region_add_rects_impl(h_damage_region(region), h_damage_rects((__VA_ARGS__)))

/* Set 'region' to the whole 'width' x 'height' surface, e.g. for a buffer that was never drawn into. */
#define poor_damage_region_whole(region, width, height) \
	poor_damage_region_whole_impl(h_damage_region(region), width, height)

/* Clip 'region' to the 'width' x 'height' surface, dropping rects that end up empty. */
#define poor_damage_region_clip(region, width, height) \
	poor_damage_region_clip_impl(h_damage_region(region), width, height)

void
poor_damage_region_add_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
			    struct poor_damage_rect rect);

void
poor_damage_region_add_rects_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
				  size_t others, const struct poor_damage_rect (*other)[others]);

void
poor_damage_region_whole_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
			      int32_t width, int32_t height);

void
poor_damage_region_clip_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
			     int32_t width, int32_t height);

/* Implementation details, subject to change. */

#define h_damage_region_declare(name, capacity) \
	typedef struct name { \
		size_t count; \
		struct poor_damage_rect rects[capacity]; \
	} name

#define h_damage_region(region) ARRAY_SIZE((region)->rects), &(region)->rects, &(region)->count
#define h_damage_rects(region) (region)->count, arrview_first((region)->count, (region)->rects)

#ifdef __cplusplus
}
#endif

#endif

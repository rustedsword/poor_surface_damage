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
 * Damage in up to 'capacity' rects, so that redrawing it takes that many draw calls at most. Rects that touch merge,
 * and when there is no room for another one, the two whose bounds cover the least that didn't change. One rect is a
 * bounding box; more follow the damage closer, but cost draw calls, and adding a rect to a full region takes time
 * that grows with the square of the capacity.
 *
 *   poor_damage_region_declare(damage_region, 4);
 *
 *   damage_region damage = POOR_DAMAGE_REGION_INIT;
 *
 *   poor_damage_region_add(&damage, (struct poor_damage_rect){ x, y, width, height });
 *   poor_damage_region_clip(&damage, surface_width, surface_height);
 *
 * Regions of any capacity mix. The macros may evaluate the regions given to them more than once. The _impl functions
 * take views of the rects, for capacities known only at run time.
 */
#define poor_damage_region_declare(name, capacity) h_damage_region_declare(name, capacity)

#define POOR_DAMAGE_REGION_INIT { .count = 0 }

#define poor_damage_region_init(region) ((void)((region)->count = 0))

/* A rect without area adds nothing */
#define poor_damage_region_add(region, ...) poor_damage_region_add_impl(h_damage_region(region), __VA_ARGS__)

#define poor_damage_region_add_region(region, ...) \
	poor_damage_region_add_rects_impl(h_damage_region(region), h_damage_rects((__VA_ARGS__)))

/* All of 'width' x 'height', which a buffer that holds nothing yet lacks */
#define poor_damage_region_whole(region, width, height) \
	poor_damage_region_whole_impl(h_damage_region(region), width, height)

/* What lies within 'width' x 'height' */
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

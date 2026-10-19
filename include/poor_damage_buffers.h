/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_BUFFERS_H
#define POOR_DAMAGE_BUFFERS_H

#include "poor_damage_region.h"
#include "poor_damage_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Poor Damage Buffers.
 *
 * Keeps track of the damage that each buffer of a swapchain is missing.
 *
 * When a frame is drawn into one buffer, all other buffers fall behind by the damage of that frame. Before drawing
 * into a buffer again, you have to redraw everything it missed in addition to the damage of the new frame. The damage
 * is tracked separately for each buffer, so buffers can be reused in any order and after any number of frames. Each
 * buffer is identified by an index, e.g. a Vulkan swapchain image index or the slot of a wl_buffer in your pool.
 *
 * Example with 3 buffers that keep up to 4 rects each:
 *
 *   poor_damage_region_declare(damage_region, 4);
 *   poor_damage_buffers_declare(swapchain_damage, 3, 4);
 *
 *   swapchain_damage tracker;
 *   damage_region damage, repair;
 *
 *   poor_damage_buffers_init(&tracker, width, height);
 *
 *   // Then, for each frame:
 *
 *   // fill 'repair' with all damage accumulated while other buffers were rendered
 *   poor_damage_buffers_lacks(&tracker, index, &repair);
 *
 *   // add the damage of this frame to 'repair'
 *   poor_damage_region_add_region(&repair, &damage);
 *
 *   ... redraw 'repair' into buffer 'index', then present it with 'damage' ...
 *
 *   // damage the other buffers and clean buffer 'index'
 *   poor_damage_buffers_drawn(&tracker, index, &damage);
 *
 * Memory usage and the time spent in poor_damage_buffers_drawn() grow with the number of buffers and rects.
 */

/* Declare a tracker type 'name' for 'buffers' buffers that keep up to 'capacity' rects of damage each. */
#define poor_damage_buffers_declare(name, buffers, capacity) h_damage_buffers_declare(name, buffers, capacity)

/*
 * Initialize the tracker for a 'width' x 'height' surface. All buffers start out empty, so the first frame drawn into
 * each of them has to redraw everything.
 */
#define poor_damage_buffers_init(buffers, width, height) \
	poor_damage_buffers_init_impl(&(buffers)->surface, h_damage_buffers(buffers), width, height)

/* Mark buffer 'index' as empty, e.g. after it was recreated. The next frame drawn into it redraws everything. */
#define poor_damage_buffers_forget(buffers, index) \
	poor_damage_buffers_forget_impl(&(buffers)->surface, h_damage_buffers(buffers), index)

/* Set 'region' to the damage that buffer 'index' has missed since it was last drawn into. */
#define poor_damage_buffers_lacks(buffers, index, region) \
	poor_damage_buffers_lacks_impl(h_damage_buffers(buffers), index, ARRAY_SIZE((region)->rects), \
				       &(region)->rects, &(region)->count)

/*
 * Call this after a frame with 'damage' was drawn into buffer 'index'. That buffer is now up to date, and all other
 * buffers are missing 'damage' as well. Returns the number of the frame.
 */
#define poor_damage_buffers_drawn(buffers, index, ...) \
	poor_damage_buffers_drawn_impl(&(buffers)->surface, h_damage_buffers(buffers), index, (__VA_ARGS__)->count, \
				       arrview_first((__VA_ARGS__)->count, (__VA_ARGS__)->rects))

/* Implementation details, subject to change. */

#define h_damage_buffers_declare(name, buffers, capacity) \
	typedef struct name { \
		struct poor_damage_surface surface; \
		size_t counts[buffers]; \
		struct poor_damage_rect rects[buffers][capacity]; \
	} name

#define h_damage_buffers(buffers) \
	ARRAY_SIZE((buffers)->counts), ARRAY_SIZE((buffers)->rects[0]), &(buffers)->counts, &(buffers)->rects

static inline void
poor_damage_buffers_init_impl(struct poor_damage_surface *surface, size_t buffers, size_t capacity,
			      size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
			      int32_t width, int32_t height)
{
	poor_damage_surface_init(surface, width, height);
	foreach_array_index(counts, i)
		poor_damage_rects_whole((*rects)[i], &(*counts)[i], width, height);
}

static inline void
poor_damage_buffers_forget_impl(const struct poor_damage_surface *surface, size_t buffers, size_t capacity,
				size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
				size_t index)
{
	assert(index < buffers);
	poor_damage_rects_whole((*rects)[index], &(*counts)[index], surface->width, surface->height);
}

static inline void
poor_damage_buffers_lacks_impl(size_t buffers, size_t capacity, const size_t (*counts)[buffers],
			       const struct poor_damage_rect (*rects)[buffers][capacity], size_t index,
			       size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
			       size_t *region_count)
{
	assert(index < buffers);
	*region_count = 0;
	poor_damage_rects_add_rects(region, region_count, arrview_first((*counts)[index], (*rects)[index]));
}

static inline uint64_t
poor_damage_buffers_drawn_impl(struct poor_damage_surface *surface, size_t buffers, size_t capacity,
			       size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
			       size_t index, size_t damages, const struct poor_damage_rect (*damage)[damages])
{
	assert(index < buffers);
	foreach_array_index(counts, i)
		if (i != index)
			poor_damage_rects_add_rects((*rects)[i], &(*counts)[i], damage);
	(*counts)[index] = 0;
	return ++surface->frame;
}

#ifdef __cplusplus
}
#endif

#endif

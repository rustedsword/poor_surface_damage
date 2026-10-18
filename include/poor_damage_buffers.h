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
 * What each of 'buffers' buffers lacks of the frames that were drawn since its own, in up to 'capacity' rects. A frame
 * that goes to a buffer redraws that and its own damage. Buffers come back in any order and after any number of
 * frames, as long as each is known by an index, like a Vulkan image index or the slot of a wl_buffer:
 *
 *   poor_damage_buffers_declare(swapchain_damage, 3, 4);
 *
 *   swapchain_damage lacks;
 *
 *   poor_damage_buffers_init(&lacks, width, height);
 *   ...
 *   poor_damage_buffers_lacks(&lacks, index, &repair);
 *   poor_damage_region_add_region(&repair, &damage);
 *   ... redraw 'repair' into buffer 'index' ...
 *   poor_damage_buffers_drawn(&lacks, index, &damage);
 *
 * Memory, and the time that drawn() takes, grow with buffers and rects.
 */
#define poor_damage_buffers_declare(name, buffers, capacity) h_damage_buffers_declare(name, buffers, capacity)

/* Buffers of 'width' x 'height' that hold nothing yet */
#define poor_damage_buffers_init(buffers, width, height) \
	poor_damage_buffers_init_impl(h_damage_buffers(buffers), width, height)

/* Buffer 'index' holds nothing anymore */
#define poor_damage_buffers_forget(buffers, index) poor_damage_buffers_forget_impl(h_damage_buffers(buffers), index)

/* Sets 'region' to what buffer 'index' lacks */
#define poor_damage_buffers_lacks(buffers, index, region) \
	poor_damage_buffers_lacks_impl(h_damage_buffers(buffers), index, h_damage_region(region))

/* A frame with 'damage' was drawn into buffer 'index': the others lack it too, and that one lacks nothing. Returns the
 * number of the frame. */
#define poor_damage_buffers_drawn(buffers, index, ...) \
	poor_damage_buffers_drawn_impl(h_damage_buffers(buffers), index, h_damage_rects((__VA_ARGS__)))

void
poor_damage_buffers_init_impl(struct poor_damage_surface *surface, size_t buffers, size_t capacity,
			      size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
			      int32_t width, int32_t height);

void
poor_damage_buffers_forget_impl(const struct poor_damage_surface *surface, size_t buffers, size_t capacity,
				size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
				size_t index);

void
poor_damage_buffers_lacks_impl(const struct poor_damage_surface *surface, size_t buffers, size_t capacity,
			       const size_t (*counts)[buffers], const struct poor_damage_rect (*rects)[buffers][capacity],
			       size_t index, size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
			       size_t *region_count);

uint64_t
poor_damage_buffers_drawn_impl(struct poor_damage_surface *surface, size_t buffers, size_t capacity,
			       size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
			       size_t index, size_t damages, const struct poor_damage_rect (*damage)[damages]);

/* Implementation details, subject to change. */

#define h_damage_buffers_declare(name, buffers, capacity) \
	typedef struct name { \
		struct poor_damage_surface surface; \
		size_t counts[buffers]; \
		struct poor_damage_rect rects[buffers][capacity]; \
	} name

#define h_damage_buffers(buffers) \
	&(buffers)->surface, ARRAY_SIZE((buffers)->counts), ARRAY_SIZE((buffers)->rects[0]), &(buffers)->counts, \
		&(buffers)->rects

#ifdef __cplusplus
}
#endif

#endif

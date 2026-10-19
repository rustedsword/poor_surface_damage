/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_HISTORY_H
#define POOR_DAMAGE_HISTORY_H

#include "poor_damage_region.h"
#include "poor_damage_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Poor Damage History.
 *
 * Remembers the damage of the last few frames.
 *
 * This is the classic way to use EGL_EXT_buffer_age. The age of a buffer tells how many frames ago it was drawn into,
 * so it is missing the damage of every frame drawn since then. The history keeps the damage of the last 'frames'
 * frames, up to 'capacity' rects each. It doesn't need to know which buffer is which: the age of a buffer, or the
 * frame it holds, is enough. A buffer that is older than the history has to be redrawn completely.
 *
 * Example that remembers 4 frames of up to 8 rects each:
 *
 *   poor_damage_region_declare(damage_region, 8);
 *   poor_damage_history_declare(frame_damage, 4, 8);
 *
 *   frame_damage history = POOR_DAMAGE_HISTORY_INIT(width, height);
 *   damage_region damage, repair;
 *   EGLint age;
 *
 *   // Then, for each frame:
 *
 *   // fill 'repair' with the damage of all frames drawn since the back buffer was last drawn into
 *   eglQuerySurface(display, surface, EGL_BUFFER_AGE_EXT, &age);
 *   poor_damage_history_lacks(&history, poor_damage_frame_of_age(&history, age), &repair);
 *
 *   // add the damage of this frame to 'repair'
 *   poor_damage_region_add_region(&repair, &damage);
 *
 *   ... redraw 'repair', then swap buffers with 'damage' ...
 *
 *   // remember the damage of this frame
 *   poor_damage_history_drawn(&history, &damage);
 *
 * Memory usage grows with the number of frames and rects, and so does the time it takes to work out what an old
 * buffer is missing.
 */

/* Declare a history type 'name' that remembers the last 'frames' frames, up to 'capacity' rects each. */
#define poor_damage_history_declare(name, frames, capacity) h_damage_history_declare(name, frames, capacity)

#define POOR_DAMAGE_HISTORY_INIT(width, height) { .surface = POOR_DAMAGE_SURFACE_INIT(width, height) }

/* Initialize the history for a 'width' x 'height' surface, with no frames drawn yet. */
#define poor_damage_history_init(history, width, height) h_damage_surface_init(&(history)->surface, width, height)

/*
 * Set 'region' to the damage that a buffer holding 'frame' has missed. If 'frame' is 0 or older than the history,
 * 'region' is set to the whole surface.
 */
#define poor_damage_history_lacks(history, frame, region) \
	poor_damage_history_lacks_impl(h_damage_history(history), frame, h_damage_region(region))

/* Call this after a frame with 'damage' was drawn. Returns the number of the frame. */
#define poor_damage_history_drawn(history, ...) \
	poor_damage_history_drawn_impl(h_damage_history(history), h_damage_rects((__VA_ARGS__)))

static inline void
poor_damage_history_lacks_impl(const struct poor_damage_surface *surface, size_t frames, size_t capacity,
			       const size_t (*counts)[frames], const struct poor_damage_rect (*rects)[frames][capacity],
			       uint64_t frame, size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
			       size_t *region_count)
{
	assert(frame <= surface->frame);
	if (!frame || surface->frame - frame > frames) {
		poor_damage_region_whole_impl(region_capacity, region, region_count, surface->width, surface->height);
		return;
	}
	*region_count = 0;
	while (frame++ < surface->frame) {
		size_t slot = (size_t)(frame % frames);

		poor_damage_region_add_rects_impl(region_capacity, region, region_count, (*counts)[slot],
						  arrview_first((*counts)[slot], (*rects)[slot]));
	}
}

static inline uint64_t
poor_damage_history_drawn_impl(struct poor_damage_surface *surface, size_t frames, size_t capacity,
			       size_t (*counts)[frames], struct poor_damage_rect (*rects)[frames][capacity],
			       size_t damages, const struct poor_damage_rect (*damage)[damages])
{
	size_t slot = (size_t)(++surface->frame % frames);

	(*counts)[slot] = 0;
	poor_damage_region_add_rects_impl(capacity, &(*rects)[slot], &(*counts)[slot], damages, damage);
	return surface->frame;
}

/* Implementation details, subject to change. */

#define h_damage_history_declare(name, frames, capacity) \
	typedef struct name { \
		struct poor_damage_surface surface; \
		size_t counts[frames]; \
		struct poor_damage_rect rects[frames][capacity]; \
	} name

#define h_damage_history(history) \
	&(history)->surface, ARRAY_SIZE((history)->counts), ARRAY_SIZE((history)->rects[0]), &(history)->counts, \
		&(history)->rects

#ifdef __cplusplus
}
#endif

#endif

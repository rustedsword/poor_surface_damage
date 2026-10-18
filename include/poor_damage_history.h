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
 * The damage of the last 'frames' frames, in up to 'capacity' rects each, for a buffer that holds one of them. It needs
 * no index of buffers, only the frame that a buffer holds, or its age as EGL_EXT_buffer_age counts it. A buffer that
 * holds an older frame lacks everything:
 *
 *   poor_damage_history_declare(frame_damage, 4, 8);
 *
 *   frame_damage history = POOR_DAMAGE_HISTORY_INIT(width, height);
 *   ...
 *   poor_damage_history_lacks(&history, poor_damage_frame_of_age(&history, age), &repair);
 *   poor_damage_region_add_region(&repair, &damage);
 *   ... redraw 'repair' ...
 *   poor_damage_history_drawn(&history, &damage);
 *
 * Memory grows with frames and rects, and so does the time to tell what an old buffer lacks.
 */
#define poor_damage_history_declare(name, frames, capacity) h_damage_history_declare(name, frames, capacity)

#define POOR_DAMAGE_HISTORY_INIT(width, height) { .surface = POOR_DAMAGE_SURFACE_INIT(width, height) }

/* No frames drawn yet on 'width' x 'height' */
#define poor_damage_history_init(history, width, height) h_damage_surface_init(&(history)->surface, width, height)

/* Sets 'region' to what a buffer that holds 'frame' lacks, everything when the history doesn't reach back to it */
#define poor_damage_history_lacks(history, frame, region) \
	poor_damage_history_lacks_impl(h_damage_history(history), frame, h_damage_region(region))

/* A frame with 'damage' was drawn. Returns its number. */
#define poor_damage_history_drawn(history, ...) \
	poor_damage_history_drawn_impl(h_damage_history(history), h_damage_rects((__VA_ARGS__)))

void
poor_damage_history_lacks_impl(const struct poor_damage_surface *surface, size_t frames, size_t capacity,
			       const size_t (*counts)[frames], const struct poor_damage_rect (*rects)[frames][capacity],
			       uint64_t frame, size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
			       size_t *region_count);

uint64_t
poor_damage_history_drawn_impl(struct poor_damage_surface *surface, size_t frames, size_t capacity,
			       size_t (*counts)[frames], struct poor_damage_rect (*rects)[frames][capacity],
			       size_t damages, const struct poor_damage_rect (*damage)[damages]);

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

/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_SURFACE_H
#define POOR_DAMAGE_SURFACE_H

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The surface that a tracker follows: its size, and the number of the last frame that was drawn on it. Frames are
 * numbered from 1, so a buffer that holds frame 0 holds nothing yet.
 */
struct poor_damage_surface {
	int32_t width, height;
	uint64_t frame;
};

#define POOR_DAMAGE_SURFACE_INIT(w, h) { .width = (w), .height = (h), .frame = 0 }

/* The frame that a buffer of EGL_EXT_buffer_age 'age' holds, 0 when it isn't known: 1 is the last frame of 'tracker' */
#define poor_damage_frame_of_age(tracker, age) h_damage_frame_of_age(&(tracker)->surface, age)

/* Implementation details, subject to change. */

static inline void h_damage_surface_init(struct poor_damage_surface *surface, int32_t width, int32_t height)
{
	assert(width > 0 && height > 0);
	surface->width = width;
	surface->height = height;
	surface->frame = 0;
}

static inline uint64_t h_damage_frame_of_age(const struct poor_damage_surface *surface, uint32_t age)
{
	return age && age <= surface->frame ? surface->frame + 1 - age : 0;
}

#ifdef __cplusplus
}
#endif

#endif

/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_SURFACE_H
#define POOR_DAMAGE_SURFACE_H

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The surface that a tracker follows: its size and the number of the last frame drawn on it.
 *
 * Frames are numbered from 1. Frame 0 means "nothing": a buffer that holds frame 0 was never drawn into and has to be
 * redrawn completely.
 */
struct poor_damage_surface {
	int32_t width, height;
	uint64_t frame;
};

#define POOR_DAMAGE_SURFACE_INIT(w, h) { .width = (w), .height = (h), .frame = 0 }

/*
 * Convert a buffer age, as returned by EGL_EXT_buffer_age, into the number of the frame that the buffer holds. An age
 * of 1 means the buffer holds the last frame drawn by 'tracker'. Returns 0 if the age is 0, which means the contents
 * are unknown, or if it is older than any frame the tracker has seen.
 */
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

/* SPDX-License-Identifier: MIT */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

#include "engine.h"
#include "tracker.h"

/*
 * Draws frames of the scene into the buffers of an engine, redrawing only what the tracker says a buffer is missing,
 * and compares every result with a frame drawn from scratch. Buffers start out with garbage, and the first frame
 * damages everything.
 */

constexpr int32_t SIM_WIDTH = 640;
constexpr int32_t SIM_HEIGHT = 480;

struct sim_options {
	struct tracker_options tracker;
	enum engine_order order;
	size_t buffers;
	int32_t width, height;
	/* Change the size of the surface every 'resize' frames, or never if it is 0 */
	uint32_t seed, frames, resize;
};

struct sim_result {
	/* Frames drawn, frames that came out wrong, and frames that redrew everything */
	uint64_t frames, wrong, wholes;
	/*
	 * Pixels that were redrawn, pixels that actually differed, pixels in the exact union of the damage that buffers
	 * missed, and pixels of the surface
	 */
	uint64_t drawn, needed, exact, pixels;
	/* Rects that were redrawn, and the time spent in the tracker */
	uint64_t rects, nanoseconds;
};

void
sim_run(const struct sim_options *options, struct sim_result *result);

#endif

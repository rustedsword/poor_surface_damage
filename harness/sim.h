/* SPDX-License-Identifier: MIT */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

#include "engine.h"
#include "tracker.h"

/*
 * Draws frames of the scene into the buffers of an engine, redrawing only what a tracker says that a buffer lacks, and
 * checks each of them against a frame that was drawn whole. Buffers hold garbage until they are drawn, and the first
 * frame damages everything.
 */

constexpr int32_t SIM_WIDTH = 640;
constexpr int32_t SIM_HEIGHT = 480;

struct sim_options {
	struct tracker_options tracker;
	enum engine_order order;
	size_t buffers;
	int32_t width, height;
	/* Every 'resize' frames the surface changes its size, unless it's 0 */
	uint32_t seed, frames, resize;
};

struct sim_result {
	/* Frames, the ones that went wrong, and the ones that redrew everything */
	uint64_t frames, wrong, wholes;
	/*
	 * Pixels that were redrawn, the ones that differed before, the ones of the exact union of the damage of the
	 * frames since the buffer's own, and all of them
	 */
	uint64_t drawn, needed, exact, pixels;
	/* Rects that were redrawn, and the time that the tracker took */
	uint64_t rects, nanoseconds;
};

void
sim_run(const struct sim_options *options, struct sim_result *result);

#endif

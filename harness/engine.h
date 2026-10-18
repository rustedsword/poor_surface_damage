/* SPDX-License-Identifier: MIT */
#ifndef ENGINE_H
#define ENGINE_H

#include <poor_array.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A presentation engine that hands buffers back in some order. A buffer stays held for a while once it was presented:
 * one frame when it is on screen until the next one replaces it, none when there is a single buffer, which the engine
 * copies, or a random number of frames.
 */

constexpr size_t ENGINE_BUFFERS = 4;

enum engine_order {
	/* The buffer that was presented the longest ago */
	ENGINE_FIFO,
	/* The free buffer that was presented last, as Mesa's EGL picks the one of the least age */
	ENGINE_NEWEST,
	/* Any free buffer, after random holds that sometimes last for dozens of frames */
	ENGINE_RANDOM,
};

struct engine {
	enum engine_order order;
	size_t count;
	uint64_t frame;
	uint32_t random;
	uint64_t presented[ENGINE_BUFFERS], free[ENGINE_BUFFERS];
};

void
engine_init(struct engine *engine, enum engine_order order, size_t count, uint32_t seed);

/* A free buffer, or the one that will be freed first */
size_t
engine_acquire(struct engine *engine);

void
engine_present(struct engine *engine, size_t index);

const char *
engine_order_name(enum engine_order order);

#endif

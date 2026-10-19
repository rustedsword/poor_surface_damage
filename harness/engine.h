/* SPDX-License-Identifier: MIT */
#ifndef ENGINE_H
#define ENGINE_H

#include <poor_array.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A fake presentation engine that hands buffers back in a chosen order. A presented buffer stays busy for a while: for
 * one frame, while it is on screen until the next one replaces it, for no time at all when there is only one buffer,
 * since the engine then copies it, or for a random number of frames.
 */

constexpr size_t ENGINE_BUFFERS = 4;

enum engine_order {
	/* Round robin: the buffer that was presented the longest time ago */
	ENGINE_FIFO,
	/* The free buffer that was presented most recently, like Mesa's EGL, which picks the buffer of the lowest age */
	ENGINE_NEWEST,
	/* Any free buffer, after random delays that sometimes last for dozens of frames */
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

/* Pick a free buffer, or the one that gets free first */
size_t
engine_acquire(struct engine *engine);

void
engine_present(struct engine *engine, size_t index);

const char *
engine_order_name(enum engine_order order);

#endif

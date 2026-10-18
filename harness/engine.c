/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stddef.h>

#include "engine.h"

static uint32_t next(struct engine *engine)
{
	engine->random ^= engine->random << 13;
	engine->random ^= engine->random >> 17;
	engine->random ^= engine->random << 5;
	return engine->random;
}

void engine_init(struct engine *engine, enum engine_order order, size_t count, uint32_t seed)
{
	assert(count && count <= ENGINE_BUFFERS && seed);
	engine->order = order;
	engine->count = count;
	engine->frame = 0;
	engine->random = seed;
	fill_array(arrview_first(count, engine->presented), 0);
	fill_array(arrview_first(count, engine->free), 0);
}

static bool is_free(const struct engine *engine, size_t index)
{
	return engine->free[index] <= engine->frame + 1;
}

static bool better(const struct engine *engine, size_t candidate, size_t best)
{
	if (is_free(engine, candidate) != is_free(engine, best))
		return is_free(engine, candidate);
	if (!is_free(engine, candidate))
		return engine->free[candidate] < engine->free[best];
	switch (engine->order) {
	case ENGINE_FIFO:
		return engine->presented[candidate] < engine->presented[best];
	case ENGINE_NEWEST:
		return engine->presented[candidate] > engine->presented[best];
	case ENGINE_RANDOM:
		break;
	}
	return false;
}

size_t engine_acquire(struct engine *engine)
{
	size_t best = 0, frees = 0, pick;

	foreach_array_index(arrview_first(engine->count, engine->free), i) {
		best = better(engine, i, best) ? i : best;
		frees += is_free(engine, i);
	}
	if (engine->order != ENGINE_RANDOM || frees < 2)
		return best;
	pick = next(engine) % frees;
	foreach_array_index(arrview_first(engine->count, engine->free), i)
		if (is_free(engine, i) && !pick--)
			return i;
	unreachable();
}

void engine_present(struct engine *engine, size_t index)
{
	uint64_t hold = engine->count > 1;

	assert(index < engine->count);
	if (engine->order == ENGINE_RANDOM)
		hold = next(engine) % 16 ? next(engine) % 4 : 4 + next(engine) % 24;
	engine->presented[index] = ++engine->frame;
	engine->free[index] = engine->frame + 1 + hold;
}

const char *engine_order_name(enum engine_order order)
{
	static const char *const names[] = { [ENGINE_FIFO] = "fifo", [ENGINE_NEWEST] = "newest", [ENGINE_RANDOM] = "random" };

	return names[order];
}

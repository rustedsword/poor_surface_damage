/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>
#include <string.h>
#include <time.h>

#include "scene.h"
#include "sim.h"

static uint32_t storage[ENGINE_BUFFERS + 1][SIM_WIDTH * SIM_HEIGHT];
static bool stale[ENGINE_BUFFERS][SIM_WIDTH * SIM_HEIGHT];

#define make_view(name, array, sim)									\
	make_arrview_first(name##_all, (size_t)(sim)->width * (size_t)(sim)->height, array);		\
	make_arrview_dim(name, (size_t)(sim)->width, name##_all)

struct sim {
	const struct sim_options *options;
	int32_t width, height;
	uint32_t random;
	struct scene scene;
	struct engine engine;
	struct tracker tracker;
};

static uint32_t next(struct sim *sim)
{
	sim->random ^= sim->random << 13;
	sim->random ^= sim->random >> 17;
	sim->random ^= sim->random << 5;
	return sim->random;
}

static uint64_t now(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec;
}

static uint64_t area(const region *covered)
{
	uint64_t pixels = 0;

	foreach_array_const_ref(arrview_first(covered->count, covered->rects), rect)
		pixels += (uint64_t)rect->width * (uint64_t)rect->height;
	return pixels;
}

/* Replace all buffers with new ones of the given size, filled with garbage */
static void recreate(struct sim *sim, int32_t width, int32_t height)
{
	sim->width = width;
	sim->height = height;
	foreach_array_ref(arrview_first(sim->options->buffers, storage), buffer)
		foreach_array_ref(arrview_first((size_t)width * (size_t)height, *buffer), pixel)
			*pixel = next(sim);
	foreach_array_ref(arrview_first(sim->options->buffers, stale), pixels)
		fill_array(arrview_first((size_t)width * (size_t)height, *pixels), true);
	engine_init(&sim->engine, sim->options->order, sim->options->buffers, next(sim) | 1);
	tracker_init(&sim->tracker, &sim->options->tracker, sim->options->buffers, width, height);
}

/* Count the pixels in the exact union of all the damage that buffer 'index' missed, including 'damage' */
static uint64_t exact(struct sim *sim, size_t index, const region *damage)
{
	uint64_t pixels = 0;

	foreach_array_ref(arrview_first(sim->options->buffers, stale), buffer) {
		make_view(mask, *buffer, sim);

		foreach_array_const_ref(arrview_first(damage->count, damage->rects), rect)
			foreach_array_ref(arrview((size_t)rect->y, (size_t)rect->height, mask), row)
				fill_array(arrview((size_t)rect->x, (size_t)rect->width, *row), true);
	}
	make_view(mask, stale[index], sim);
	foreach_array_ref(mask_all, pixel) {
		pixels += *pixel;
		*pixel = false;
	}
	return pixels;
}

static void frame(struct sim *sim, struct sim_result *result)
{
	region damage = POOR_DAMAGE_REGION_INIT, repair;
	size_t index;
	uint64_t start, middle;

	if (!sim->scene.frame)
		poor_damage_region_whole(&damage, sim->width, sim->height);
	if (sim->options->resize && sim->scene.frame && sim->scene.frame % sim->options->resize == 0) {
		bool smaller = sim->width == sim->options->width;

		recreate(sim, smaller ? sim->options->width * 3 / 4 + 1 : sim->options->width,
			 smaller ? sim->options->height * 2 / 3 + 1 : sim->options->height);
		scene_resize(&sim->scene, sim->width, sim->height, &damage);
	}
	scene_step(&sim->scene, &damage);
	poor_damage_region_clip(&damage, sim->width, sim->height);
	tracker_limit(&sim->tracker, &damage);
	index = engine_acquire(&sim->engine);

	make_view(reference, storage[ENGINE_BUFFERS], sim);
	make_view(buffer, storage[index], sim);
	scene_draw(&sim->scene, reference, ((struct poor_damage_rect){ 0, 0, sim->width, sim->height }));
	foreach_array_index(buffer_all, i)
		result->needed += (*buffer_all)[i] != (*reference_all)[i];
	result->exact += exact(sim, index, &damage);

	start = now();
	tracker_repair(&sim->tracker, index, &damage, &repair);
	middle = now();
	foreach_array_const_ref(arrview_first(repair.count, repair.rects), rect)
		scene_draw(&sim->scene, buffer, *rect);
	result->wrong += !!memcmp(buffer_all, reference_all, ARRAY_SIZE_BYTES(buffer_all));
	result->nanoseconds += middle - start;
	start = now();
	tracker_drawn(&sim->tracker, index, &damage);
	result->nanoseconds += now() - start;
	engine_present(&sim->engine, index);
	tracker_presented(&sim->tracker, index);

	result->frames++;
	result->drawn += area(&repair);
	result->pixels += (uint64_t)sim->width * (uint64_t)sim->height;
	result->rects += repair.count;
	result->wholes += area(&repair) == (uint64_t)sim->width * (uint64_t)sim->height;
}

void sim_run(const struct sim_options *options, struct sim_result *result)
{
	struct sim sim;

	assert(options->buffers && options->buffers <= ENGINE_BUFFERS && options->seed);
	assert(options->width > 0 && options->width <= SIM_WIDTH && options->height > 0 && options->height <= SIM_HEIGHT);
	sim.options = options;
	sim.random = options->seed;
	*result = (struct sim_result){};
	recreate(&sim, options->width, options->height);
	scene_init(&sim.scene, options->width, options->height, options->seed);
	for (uint32_t i = 0; i < options->frames; i++)
		frame(&sim, result);
}

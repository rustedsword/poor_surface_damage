/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "sim.h"
#include "test-runner.h"

static void run(const struct tracker_options *tracker, enum engine_order order, size_t buffers, struct sim_result *result)
{
	const struct sim_options options = {
		.tracker = *tracker, .order = order, .buffers = buffers, .width = 131, .height = 97, .seed = 7,
		.frames = 300, .resize = 61,
	};

	sim_run(&options, result);
}

static void run_kind(enum tracker_kind kind, enum engine_order order, size_t buffers, struct sim_result *result)
{
	struct tracker_options tracker = TRACKER_OPTIONS_INIT;

	tracker.kind = kind;
	tracker.cell_width = tracker.cell_height = 4;
	run(&tracker, order, buffers, result);
}

TEST(trackers_redraw_what_buffers_lack)
{
	struct sim_result result;

	for (enum tracker_kind kind = TRACKER_BUFFERS; kind <= TRACKER_MAP; kind++) {
		for (enum engine_order order = ENGINE_FIFO; order <= ENGINE_RANDOM; order++) {
			for (size_t buffers = 1; buffers <= ENGINE_BUFFERS; buffers++) {
				run_kind(kind, order, buffers, &result);
				assert(result.frames == 300 && !result.wrong);
				assert(result.drawn >= result.exact && result.exact >= result.needed);
				assert(result.drawn < result.pixels / 2);
			}
		}
	}
}

TEST(any_capacities_redraw_what_buffers_lack)
{
	static const struct tracker_options trackers[] = {
		{ TRACKER_BUFFERS, .rects = 1, .frames = 1, .cells = 1, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_BUFFERS, .rects = 64, .frames = 1, .cells = 1, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_HISTORY, .rects = 1, .frames = 1, .cells = 1, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_HISTORY, .rects = 16, .frames = 2, .cells = 1, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_HISTORY, .rects = 3, .frames = 64, .cells = 1, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_MAP, .rects = 2, .frames = 1, .cells = 1, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_MAP, .rects = 8, .frames = 1, .cells = 150, .cell_width = 7, .cell_height = 2 },
		{ TRACKER_MAP, .rects = 32, .frames = 1, .cells = 16384, .cell_width = 1, .cell_height = 1 },
		{ TRACKER_MAP, .rects = 4, .frames = 1, .cells = 3, .cell_width = 64, .cell_height = 1 },
	};
	struct sim_result result;

	foreach_array_const_ref(trackers, tracker) {
		run(tracker, ENGINE_RANDOM, 3, &result);
		assert(!result.wrong && result.drawn >= result.exact && result.exact >= result.needed);
	}
}

TEST(redrawing_everything_is_right)
{
	struct sim_result result;

	run_kind(TRACKER_WHOLE, ENGINE_RANDOM, 3, &result);
	assert(!result.wrong && result.drawn == result.pixels && result.wholes == result.frames);
}

TEST(redrawing_only_the_damage_needs_a_single_buffer)
{
	struct sim_result result;

	for (enum engine_order order = ENGINE_FIFO; order <= ENGINE_RANDOM; order++) {
		run_kind(TRACKER_NAIVE, order, 1, &result);
		assert(!result.wrong);
		run_kind(TRACKER_NAIVE, order, 2, &result);
		assert(result.wrong > result.frames / 2);
	}
}

TEST(old_buffers_are_redrawn_whole_only_past_the_history)
{
	struct sim_result history, map, buffers;

	run_kind(TRACKER_HISTORY, ENGINE_RANDOM, 4, &history);
	run_kind(TRACKER_MAP, ENGINE_RANDOM, 4, &map);
	run_kind(TRACKER_BUFFERS, ENGINE_RANDOM, 4, &buffers);
	assert(history.wholes > map.wholes && history.wholes > buffers.wholes);
}

TEST(more_rects_redraw_less)
{
	struct tracker_options tracker = TRACKER_OPTIONS_INIT;
	struct sim_result box, few, many;

	tracker.rects = 1;
	run(&tracker, ENGINE_FIFO, 3, &box);
	tracker.rects = 4;
	run(&tracker, ENGINE_FIFO, 3, &few);
	tracker.rects = 32;
	run(&tracker, ENGINE_FIFO, 3, &many);
	assert(box.drawn > few.drawn && few.drawn > many.drawn && many.rects > few.rects);
}

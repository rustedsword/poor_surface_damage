/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

poor_damage_region_declare(region, 4);
poor_damage_history_declare(history8, 8, 4);
poor_damage_history_declare(history2, 2, 4);

#define is(region, ...) is_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)
#define drawn(history, ...) poor_damage_history_drawn(history, &(region){ .count = 1, .rects = { { __VA_ARGS__ } } })

static bool is_impl(size_t count, const struct poor_damage_rect (*rects)[count], int32_t x, int32_t y, int32_t width,
		    int32_t height)
{
	return count == 1 && (*rects)[0].x == x && (*rects)[0].y == y && (*rects)[0].width == width &&
	       (*rects)[0].height == height;
}

TEST(buffers_that_hold_nothing_lack_everything)
{
	history8 history = POOR_DAMAGE_HISTORY_INIT(640, 480);
	region lacks;

	poor_damage_history_lacks(&history, 0, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
	assert(drawn(&history, 0, 0, 10, 10) == 1);
	poor_damage_history_lacks(&history, 0, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
}

TEST(buffers_lack_the_frames_drawn_since_theirs)
{
	history8 history;
	region lacks;

	poor_damage_history_init(&history, 640, 480);
	drawn(&history, 0, 0, 10, 10);
	drawn(&history, 100, 100, 10, 10);
	assert(drawn(&history, 100, 110, 10, 10) == 3);
	poor_damage_history_lacks(&history, 3, &lacks);
	assert(!lacks.count);
	poor_damage_history_lacks(&history, 2, &lacks);
	assert(is(&lacks, 100, 110, 10, 10));
	poor_damage_history_lacks(&history, 1, &lacks);
	assert(is(&lacks, 100, 100, 10, 20));
}

TEST(frames_older_than_the_history_are_unknown)
{
	history8 history = POOR_DAMAGE_HISTORY_INIT(640, 480);
	region lacks;

	for (int32_t i = 0; i <= 8; i++)
		drawn(&history, 20 * i, 0, 10, 10);
	poor_damage_history_lacks(&history, 1, &lacks);
	assert(lacks.count && !is(&lacks, 0, 0, 640, 480));
	drawn(&history, 300, 300, 10, 10);
	poor_damage_history_lacks(&history, 1, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
	poor_damage_history_lacks(&history, 2, &lacks);
	assert(!is(&lacks, 0, 0, 640, 480));
}

TEST(a_short_history_forgets_sooner)
{
	history2 history = POOR_DAMAGE_HISTORY_INIT(640, 480);
	region lacks;

	drawn(&history, 0, 0, 10, 10);
	drawn(&history, 20, 0, 10, 10);
	drawn(&history, 40, 0, 10, 10);
	poor_damage_history_lacks(&history, 1, &lacks);
	assert(lacks.count == 2);
	drawn(&history, 60, 0, 10, 10);
	poor_damage_history_lacks(&history, 1, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
}

TEST(ages_count_back_from_the_last_frame)
{
	history8 history = POOR_DAMAGE_HISTORY_INIT(640, 480);

	assert(!poor_damage_frame_of_age(&history, 1));
	for (int i = 0; i < 5; i++)
		drawn(&history, 0, 0, 10, 10);
	assert(poor_damage_frame_of_age(&history, 1) == 5 && poor_damage_frame_of_age(&history, 2) == 4);
	assert(poor_damage_frame_of_age(&history, 5) == 1 && !poor_damage_frame_of_age(&history, 6));
	assert(!poor_damage_frame_of_age(&history, 0));
}

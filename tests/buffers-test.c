/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

poor_damage_region_declare(region, 4);
poor_damage_buffers_declare(triple, 3, 4);
poor_damage_buffers_declare(boxes, 8, 1);

#define is(region, ...) is_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)

static bool is_impl(size_t count, const struct poor_damage_rect (*rects)[count], int32_t x, int32_t y, int32_t width,
		    int32_t height)
{
	return count == 1 && (*rects)[0].x == x && (*rects)[0].y == y && (*rects)[0].width == width &&
	       (*rects)[0].height == height;
}

static region damage_of(int32_t x, int32_t y, int32_t width, int32_t height)
{
	region damage = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&damage, (struct poor_damage_rect){ x, y, width, height });
	return damage;
}

TEST(buffers_lack_what_was_drawn_since_their_frame)
{
	region first = damage_of(0, 0, 10, 10), second = damage_of(100, 100, 10, 10), third = damage_of(0, 50, 10, 10);
	region stale;
	triple buffers;

	poor_damage_buffers_init(&buffers, 640, 480);
	assert(poor_damage_buffers_drawn(&buffers, 0, &first) == 1);
	poor_damage_buffers_get(&buffers, 0, &stale);
	assert(!stale.count);
	poor_damage_buffers_get(&buffers, 1, &stale);
	assert(is(&stale, 0, 0, 640, 480));
	poor_damage_buffers_drawn(&buffers, 1, &second);
	poor_damage_buffers_get(&buffers, 0, &stale);
	assert(is(&stale, 100, 100, 10, 10));
	poor_damage_buffers_drawn(&buffers, 0, &third);
	poor_damage_buffers_get(&buffers, 1, &stale);
	assert(is(&stale, 0, 50, 10, 10));
	poor_damage_buffers_drawn(&buffers, 2, &second);
	poor_damage_buffers_get(&buffers, 1, &stale);
	assert(stale.count == 2);
}

TEST(a_forgotten_buffer_lacks_everything)
{
	region damage = damage_of(0, 0, 10, 10), stale;
	triple buffers;

	poor_damage_buffers_init(&buffers, 640, 480);
	poor_damage_buffers_drawn(&buffers, 1, &damage);
	poor_damage_buffers_forget(&buffers, 1);
	poor_damage_buffers_get(&buffers, 1, &stale);
	assert(is(&stale, 0, 0, 640, 480));
}

TEST(buffers_of_one_rect_lack_bounding_boxes)
{
	region left = damage_of(0, 0, 10, 10), right = damage_of(100, 0, 10, 10), stale;
	boxes buffers;

	poor_damage_buffers_init(&buffers, 640, 480);
	foreach_array_index(buffers.counts, i)
		poor_damage_buffers_drawn(&buffers, i, &left);
	poor_damage_buffers_drawn(&buffers, 7, &right);
	poor_damage_buffers_get(&buffers, 0, &stale);
	assert(is(&stale, 0, 0, 110, 10));
	poor_damage_buffers_get(&buffers, 7, &stale);
	assert(!stale.count);
}

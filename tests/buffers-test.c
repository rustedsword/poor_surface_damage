/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

static bool is(const struct poor_damage_region *region, int32_t x, int32_t y, int32_t width, int32_t height)
{
	return region->count == 1 && region->rects[0].x == x && region->rects[0].y == y && region->rects[0].width == width &&
	       region->rects[0].height == height;
}

static struct poor_damage_region damage_of(int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct poor_damage_region region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ x, y, width, height });
	return region;
}

TEST(buffers_lack_what_was_drawn_since_their_frame)
{
	struct poor_damage_region buffers[3], first = damage_of(0, 0, 10, 10), second = damage_of(100, 100, 10, 10);
	struct poor_damage_region third = damage_of(0, 50, 10, 10);

	foreach_array_ref(buffers, buffer)
		poor_damage_region_whole(buffer, 640, 480);
	poor_damage_drawn(buffers, 0, &first);
	assert(!buffers[0].count && is(&buffers[1], 0, 0, 640, 480) && is(&buffers[2], 0, 0, 640, 480));
	poor_damage_drawn(buffers, 1, &second);
	assert(is(&buffers[0], 100, 100, 10, 10) && !buffers[1].count && is(&buffers[2], 0, 0, 640, 480));
	poor_damage_drawn(buffers, 0, &third);
	assert(!buffers[0].count && buffers[1].count == 1 && is(&buffers[1], 0, 50, 10, 10));
	poor_damage_drawn(buffers, 2, &second);
	assert(buffers[0].count == 1 && buffers[1].count == 2 && !buffers[2].count);
}

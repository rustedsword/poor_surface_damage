/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

static bool has(const struct poor_damage_region *region, int32_t x, int32_t y, int32_t width, int32_t height)
{
	foreach_array_const_ref(arrview_first(region->count, region->rects), rect)
		if (rect->x == x && rect->y == y && rect->width == width && rect->height == height)
			return true;
	return false;
}

static bool covers(const struct poor_damage_region *region, const struct poor_damage_rect *inner)
{
	foreach_array_const_ref(arrview_first(region->count, region->rects), rect)
		if (inner->x >= rect->x && inner->x + inner->width <= rect->x + rect->width && inner->y >= rect->y &&
		    inner->y + inner->height <= rect->y + rect->height)
			return true;
	return false;
}

TEST(rects_without_area_add_nothing)
{
	struct poor_damage_region region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 0, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 10, -1 });
	assert(!region.count);
}

TEST(touching_rects_merge)
{
	struct poor_damage_region region;

	poor_damage_region_init(&region);
	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 0, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 0, 10, 10 });
	assert(region.count == 1 && has(&region, 0, 0, 20, 10));
	poor_damage_region_add(&region, (struct poor_damage_rect){ 5, 10, 30, 4 });
	assert(region.count == 1 && has(&region, 0, 0, 35, 14));
	poor_damage_region_add(&region, (struct poor_damage_rect){ 1, 1, 2, 2 });
	assert(region.count == 1 && has(&region, 0, 0, 35, 14));
}

TEST(corners_and_gaps_keep_rects_apart)
{
	struct poor_damage_region region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 0, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 22, 0, 10, 10 });
	assert(region.count == 3 && has(&region, 0, 0, 10, 10) && has(&region, 10, 10, 10, 10) && has(&region, 22, 0, 10, 10));
}

TEST(past_the_limit_merge_where_it_wastes_least)
{
	struct poor_damage_region region = POOR_DAMAGE_REGION_INIT;

	for (int32_t i = 0; i < 5; i++)
		poor_damage_region_add(&region, (struct poor_damage_rect){ 10 * i, 50 * i, 10, 10 });
	assert(region.count == POOR_DAMAGE_RECTS && has(&region, 0, 0, 20, 60) && has(&region, 20, 100, 10, 10));
	assert(has(&region, 30, 150, 10, 10) && has(&region, 40, 200, 10, 10));
}

TEST(many_rects_stay_covered)
{
	struct poor_damage_region region = POOR_DAMAGE_REGION_INIT;
	struct poor_damage_rect rects[40];

	foreach_array_index(rects, i) {
		rects[i] = (struct poor_damage_rect){ i * 37 % 280, 24 * i, 20, 16 };
		poor_damage_region_add(&region, rects[i]);
	}
	assert(region.count <= POOR_DAMAGE_RECTS);
	foreach_array_const_ref(rects, rect)
		assert(covers(&region, rect));
}

TEST(regions_add_up)
{
	struct poor_damage_region region = POOR_DAMAGE_REGION_INIT, other = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 0, 10, 10 });
	poor_damage_region_add(&other, (struct poor_damage_rect){ 0, 10, 10, 10 });
	poor_damage_region_add(&other, (struct poor_damage_rect){ 50, 50, 10, 10 });
	poor_damage_region_add_region(&region, &other);
	assert(region.count == 2 && has(&region, 0, 0, 10, 20) && has(&region, 50, 50, 10, 10));
	poor_damage_region_whole(&region, 640, 480);
	assert(region.count == 1 && has(&region, 0, 0, 640, 480));
}

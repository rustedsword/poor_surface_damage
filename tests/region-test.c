/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

poor_damage_region_declare(region1, 1);
poor_damage_region_declare(region4, 4);
poor_damage_region_declare(region16, 16);

#define has(region, ...) has_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)
#define covers(region, ...) covers_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)

static bool has_impl(size_t count, const struct poor_damage_rect (*rects)[count], int32_t x, int32_t y, int32_t width,
		     int32_t height)
{
	foreach_array_const_ref(rects, rect)
		if (rect->x == x && rect->y == y && rect->width == width && rect->height == height)
			return true;
	return false;
}

static bool covers_impl(size_t count, const struct poor_damage_rect (*rects)[count], const struct poor_damage_rect *inner)
{
	foreach_array_const_ref(rects, rect)
		if (inner->x >= rect->x && inner->x + inner->width <= rect->x + rect->width && inner->y >= rect->y &&
		    inner->y + inner->height <= rect->y + rect->height)
			return true;
	return false;
}

TEST(rects_without_area_add_nothing)
{
	region4 region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 0, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 10, -1 });
	assert(!region.count);
}

TEST(touching_rects_merge)
{
	region4 region;

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
	region4 region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 0, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 22, 0, 10, 10 });
	assert(region.count == 3 && has(&region, 0, 0, 10, 10) && has(&region, 10, 10, 10, 10) && has(&region, 22, 0, 10, 10));
}

TEST(past_the_limit_merge_where_it_wastes_least)
{
	region4 region = POOR_DAMAGE_REGION_INIT;

	for (int32_t i = 0; i < 5; i++)
		poor_damage_region_add(&region, (struct poor_damage_rect){ 10 * i, 50 * i, 10, 10 });
	assert(region.count == 4 && has(&region, 0, 0, 20, 60) && has(&region, 20, 100, 10, 10));
	assert(has(&region, 30, 150, 10, 10) && has(&region, 40, 200, 10, 10));
}

TEST(merged_rects_take_in_the_rects_they_reach)
{
	region4 region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 0, 100, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 20, 100, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 95, 12, 50, 6 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 500, 500, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 900, 900, 10, 10 });
	assert(region.count == 3 && has(&region, 0, 0, 145, 30) && has(&region, 900, 900, 10, 10));
}

TEST(rects_covered_by_a_merge_add_nothing)
{
	static const struct poor_damage_rect rects[] = {
		{ 30, 90, 30, 20 }, { 70, 10, 10, 10 }, { 60, 40, 40, 10 }, { 30, 40, 10, 40 }, { 60, 60, 10, 30 },
	};
	region4 region = POOR_DAMAGE_REGION_INIT;

	foreach_array_const_ref(rects, rect)
		poor_damage_region_add(&region, *rect);
	assert(region.count == 2 && has(&region, 70, 10, 10, 10) && has(&region, 30, 40, 70, 70));
}

TEST(many_rects_stay_covered)
{
	region4 region = POOR_DAMAGE_REGION_INIT;
	struct poor_damage_rect rects[40];

	foreach_array_index(rects, i) {
		rects[i] = (struct poor_damage_rect){ (int32_t)i * 37 % 280, 24 * (int32_t)i, 20, 16 };
		poor_damage_region_add(&region, rects[i]);
	}
	assert(region.count <= 4);
	foreach_array_const_ref(rects, rect)
		assert(covers(&region, rect));
}

TEST(a_single_rect_is_the_bounding_box)
{
	region1 region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 10, 10, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 100, 50, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 200, 1, 1 });
	assert(region.count == 1 && has(&region, 0, 10, 110, 191));
}

TEST(more_rects_follow_the_damage_closer)
{
	region4 few = POOR_DAMAGE_REGION_INIT;
	region16 many = POOR_DAMAGE_REGION_INIT;

	for (int32_t i = 0; i < 10; i++) {
		poor_damage_region_add(&few, (struct poor_damage_rect){ 30 * i, 0, 10, 10 });
		poor_damage_region_add(&many, (struct poor_damage_rect){ 30 * i, 0, 10, 10 });
	}
	assert(few.count == 4 && many.count == 10 && has(&many, 270, 0, 10, 10));
}

TEST(regions_of_any_capacity_mix)
{
	region16 many = POOR_DAMAGE_REGION_INIT;
	region1 one = POOR_DAMAGE_REGION_INIT;
	region4 few = POOR_DAMAGE_REGION_INIT;

	for (int32_t i = 0; i < 10; i++)
		poor_damage_region_add(&many, (struct poor_damage_rect){ 30 * i, 20 * i, 10, 10 });
	poor_damage_region_add_region(&few, &many);
	poor_damage_region_add_region(&one, &few);
	assert(few.count == 4 && one.count == 1 && has(&one, 0, 0, 280, 190));
	foreach_array_const_ref(arrview_first(many.count, many.rects), rect)
		assert(covers(&few, rect));
}

TEST(capacities_known_at_run_time_take_views)
{
	struct poor_damage_rect storage[64];
	size_t capacity = 3, count = 0;

	for (int32_t i = 0; i < 10; i++)
		poor_damage_rects_add(arrview_first(capacity, storage), &count, (struct poor_damage_rect){ 30 * i, 0, 10, 10 });
	assert(count == 3);
}

TEST(regions_add_up)
{
	region4 region = POOR_DAMAGE_REGION_INIT, other = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 0, 10, 10 });
	poor_damage_region_add(&other, (struct poor_damage_rect){ 0, 10, 10, 10 });
	poor_damage_region_add(&other, (struct poor_damage_rect){ 50, 50, 10, 10 });
	poor_damage_region_add_region(&region, &other);
	assert(region.count == 2 && has(&region, 0, 0, 10, 20) && has(&region, 50, 50, 10, 10));
	poor_damage_region_whole(&region, 640, 480);
	assert(region.count == 1 && has(&region, 0, 0, 640, 480));
}

TEST(clipping_keeps_what_lies_within)
{
	region4 region = POOR_DAMAGE_REGION_INIT;

	poor_damage_region_add(&region, (struct poor_damage_rect){ -5, -5, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 30, 10, 20, 5 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 100, 0, 10, 10 });
	poor_damage_region_add(&region, (struct poor_damage_rect){ 0, 20, INT32_MAX, 10 });
	poor_damage_region_clip(&region, 40, 25);
	assert(region.count == 3 && has(&region, 0, 0, 5, 5) && has(&region, 30, 10, 10, 5) && has(&region, 0, 20, 40, 5));
}

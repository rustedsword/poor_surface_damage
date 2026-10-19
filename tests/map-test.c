/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

poor_damage_region_declare(region, 4);
poor_damage_map_declare(square, 32, 32);
poor_damage_map_declare(editor, 24, 240);
poor_damage_map_declare(wide, 128, 8);

#define is(region, ...) is_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)
#define covers(region, ...) covers_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)
#define drawn(map, ...) poor_damage_map_drawn(map, &(region){ .count = 1, .rects = { { __VA_ARGS__ } } })

static bool is_impl(size_t count, const struct poor_damage_rect (*rects)[count], int32_t x, int32_t y, int32_t width,
		    int32_t height)
{
	return count == 1 && (*rects)[0].x == x && (*rects)[0].y == y && (*rects)[0].width == width &&
	       (*rects)[0].height == height;
}

static bool covers_impl(size_t count, const struct poor_damage_rect (*rects)[count], const struct poor_damage_rect *inner)
{
	foreach_array_const_ref(rects, rect)
		if (inner->x >= rect->x && inner->x + inner->width <= rect->x + rect->width && inner->y >= rect->y &&
		    inner->y + inner->height <= rect->y + rect->height)
			return true;
	return false;
}

TEST(cells_cover_the_surface)
{
	square map;

	poor_damage_map_init(&map, 650, 100, 1, 1);
	assert(map.grid.cell_width == 21 && map.grid.columns == 31 && map.grid.cell_height == 4 && map.grid.rows == 25);
	poor_damage_map_init(&map, 1, 1, 1, 1);
	assert(map.grid.cell_width == 1 && map.grid.columns == 1 && map.grid.cell_height == 1 && map.grid.rows == 1);
	poor_damage_map_init(&map, 640, 480, 64, 64);
	assert(map.grid.cell_width == 64 && map.grid.columns == 10 && map.grid.cell_height == 64 && map.grid.rows == 8);
}

TEST(cells_grow_by_their_multiples_when_the_grid_runs_out)
{
	editor map;

	poor_damage_map_init(&map, 1500, 15000, 64, 64);
	assert(map.grid.cell_width == 64 && map.grid.columns == 24 && map.grid.cell_height == 64 && map.grid.rows == 235);
	poor_damage_map_init(&map, 3000, 40000, 64, 64);
	assert(map.grid.cell_width == 128 && map.grid.columns == 24 && map.grid.cell_height == 192);
	assert(map.grid.rows == 209);
}

TEST(grids_follow_the_shape_of_the_surface)
{
	wide map;
	region lacks;

	poor_damage_map_init(&map, 8000, 500, 64, 64);
	assert(map.grid.cell_width == 64 && map.grid.columns == 125 && map.grid.cell_height == 64 && map.grid.rows == 8);
	drawn(&map, 0, 0, 8000, 500);
	drawn(&map, 7990, 10, 5, 5);
	poor_damage_map_lacks(&map, 1, &lacks);
	assert(is(&lacks, 7936, 0, 64, 64));
}

TEST(buffers_that_hold_nothing_lack_everything)
{
	square map;
	region lacks;

	poor_damage_map_init(&map, 640, 480, 1, 1);
	poor_damage_map_lacks(&map, 0, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
	assert(drawn(&map, 0, 0, 10, 10) == 1);
	poor_damage_map_lacks(&map, 0, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
}

TEST(buffers_lack_whole_cells_that_changed_since_their_frame)
{
	square map;
	region lacks;

	poor_damage_map_init(&map, 640, 480, 1, 1);
	drawn(&map, 25, 20, 10, 10);
	poor_damage_map_lacks(&map, 1, &lacks);
	assert(!lacks.count);
	drawn(&map, 25, 20, 10, 10);
	drawn(&map, 300, 300, 1, 1);
	poor_damage_map_lacks(&map, 2, &lacks);
	assert(is(&lacks, 300, 300, 20, 15));
	poor_damage_map_lacks(&map, 1, &lacks);
	assert(lacks.count == 2 && covers(&lacks, &(struct poor_damage_rect){ 20, 15, 20, 15 }));
	assert(covers(&lacks, &(struct poor_damage_rect){ 300, 300, 20, 15 }));
	for (int i = 0; i < 1000; i++)
		drawn(&map, 0, 0, 0, 0);
	poor_damage_map_lacks(&map, 1, &lacks);
	assert(lacks.count == 2);
}

TEST(cells_past_the_edges_are_clipped)
{
	square map;
	region lacks;

	poor_damage_map_init(&map, 650, 100, 1, 1);
	drawn(&map, 640, 90, 100, 100);
	drawn(&map, -50, -50, 10, 10);
	poor_damage_map_lacks(&map, 1, &lacks);
	assert(!lacks.count);
	poor_damage_map_lacks(&map, 0, &lacks);
	assert(is(&lacks, 0, 0, 650, 100));
	drawn(&map, 640, 90, 100, 100);
	poor_damage_map_lacks(&map, 2, &lacks);
	assert(is(&lacks, 630, 88, 20, 12));
}

TEST(many_changed_cells_stay_covered)
{
	square map;
	region lacks;
	struct poor_damage_rect rects[40];

	poor_damage_map_init(&map, 640, 480, 1, 1);
	drawn(&map, 0, 0, 640, 480);
	foreach_array_index(rects, i) {
		rects[i] = (struct poor_damage_rect){ (int32_t)i * 37 % 600, 11 * (int32_t)i, 5, 3 };
		drawn(&map, rects[i].x, rects[i].y, rects[i].width, rects[i].height);
	}
	poor_damage_map_lacks(&map, 1, &lacks);
	assert(lacks.count <= 4);
	foreach_array_const_ref(rects, rect)
		assert(covers(&lacks, rect));
}

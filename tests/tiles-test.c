/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

poor_damage_region_declare(region, 4);
poor_damage_tiles_declare(square, 32, 32);
poor_damage_tiles_declare(editor, 24, 240);
poor_damage_tiles_declare(wide, 128, 8);

#define is(region, ...) is_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)
#define covers(region, ...) covers_impl((region)->count, arrview_first((region)->count, (region)->rects), __VA_ARGS__)
#define drawn(tiles, ...) poor_damage_tiles_drawn(tiles, &(region){ .count = 1, .rects = { { __VA_ARGS__ } } })

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

TEST(tiles_cover_the_surface)
{
	square tiles;

	poor_damage_tiles_init(&tiles, 650, 100, 1, 1);
	assert(tiles.grid.tile_width == 21 && tiles.grid.columns == 31 && tiles.grid.tile_height == 4 && tiles.grid.rows == 25);
	poor_damage_tiles_init(&tiles, 1, 1, 1, 1);
	assert(tiles.grid.tile_width == 1 && tiles.grid.columns == 1 && tiles.grid.tile_height == 1 && tiles.grid.rows == 1);
	poor_damage_tiles_init(&tiles, 640, 480, 64, 64);
	assert(tiles.grid.tile_width == 64 && tiles.grid.columns == 10 && tiles.grid.tile_height == 64 && tiles.grid.rows == 8);
}

TEST(tiles_grow_by_their_multiples_when_the_grid_runs_out)
{
	editor tiles;

	poor_damage_tiles_init(&tiles, 1500, 15000, 64, 64);
	assert(tiles.grid.tile_width == 64 && tiles.grid.columns == 24 && tiles.grid.tile_height == 64 && tiles.grid.rows == 235);
	poor_damage_tiles_init(&tiles, 3000, 40000, 64, 64);
	assert(tiles.grid.tile_width == 128 && tiles.grid.columns == 24 && tiles.grid.tile_height == 192);
	assert(tiles.grid.rows == 209);
}

TEST(grids_follow_the_shape_of_the_surface)
{
	wide tiles;
	region lacks;

	poor_damage_tiles_init(&tiles, 8000, 500, 64, 64);
	assert(tiles.grid.tile_width == 64 && tiles.grid.columns == 125 && tiles.grid.tile_height == 64 && tiles.grid.rows == 8);
	drawn(&tiles, 0, 0, 8000, 500);
	drawn(&tiles, 7990, 10, 5, 5);
	poor_damage_tiles_lacks(&tiles, 1, &lacks);
	assert(is(&lacks, 7936, 0, 64, 64));
}

TEST(buffers_that_hold_nothing_lack_everything)
{
	square tiles;
	region lacks;

	poor_damage_tiles_init(&tiles, 640, 480, 1, 1);
	poor_damage_tiles_lacks(&tiles, 0, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
	assert(drawn(&tiles, 0, 0, 10, 10) == 1);
	poor_damage_tiles_lacks(&tiles, 0, &lacks);
	assert(is(&lacks, 0, 0, 640, 480));
}

TEST(buffers_lack_whole_tiles_that_changed_since_their_frame)
{
	square tiles;
	region lacks;

	poor_damage_tiles_init(&tiles, 640, 480, 1, 1);
	drawn(&tiles, 25, 20, 10, 10);
	poor_damage_tiles_lacks(&tiles, 1, &lacks);
	assert(!lacks.count);
	drawn(&tiles, 25, 20, 10, 10);
	drawn(&tiles, 300, 300, 1, 1);
	poor_damage_tiles_lacks(&tiles, 2, &lacks);
	assert(is(&lacks, 300, 300, 20, 15));
	poor_damage_tiles_lacks(&tiles, 1, &lacks);
	assert(lacks.count == 2 && covers(&lacks, &(struct poor_damage_rect){ 20, 15, 20, 15 }));
	assert(covers(&lacks, &(struct poor_damage_rect){ 300, 300, 20, 15 }));
	for (int i = 0; i < 1000; i++)
		drawn(&tiles, 0, 0, 0, 0);
	poor_damage_tiles_lacks(&tiles, 1, &lacks);
	assert(lacks.count == 2);
}

TEST(tiles_past_the_edges_are_clipped)
{
	square tiles;
	region lacks;

	poor_damage_tiles_init(&tiles, 650, 100, 1, 1);
	drawn(&tiles, 640, 90, 100, 100);
	drawn(&tiles, -50, -50, 10, 10);
	poor_damage_tiles_lacks(&tiles, 1, &lacks);
	assert(!lacks.count);
	poor_damage_tiles_lacks(&tiles, 0, &lacks);
	assert(is(&lacks, 0, 0, 650, 100));
	drawn(&tiles, 640, 90, 100, 100);
	poor_damage_tiles_lacks(&tiles, 2, &lacks);
	assert(is(&lacks, 630, 88, 20, 12));
}

TEST(many_changed_tiles_stay_covered)
{
	square tiles;
	region lacks;
	struct poor_damage_rect rects[40];

	poor_damage_tiles_init(&tiles, 640, 480, 1, 1);
	drawn(&tiles, 0, 0, 640, 480);
	foreach_array_index(rects, i) {
		rects[i] = (struct poor_damage_rect){ (int32_t)i * 37 % 600, 11 * (int32_t)i, 5, 3 };
		drawn(&tiles, rects[i].x, rects[i].y, rects[i].width, rects[i].height);
	}
	poor_damage_tiles_lacks(&tiles, 1, &lacks);
	assert(lacks.count <= 4);
	foreach_array_const_ref(rects, rect)
		assert(covers(&lacks, rect));
}

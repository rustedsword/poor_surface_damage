/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <assert.h>

#include "poor_surface_damage.h"
#include "test-runner.h"

poor_damage_region_declare(region, 4);

poor_damage_map_declare(square);
poor_damage_map_declare(screen);
poor_damage_map_declare(editor);
poor_damage_map_declare(wide);
poor_damage_map_declare(table);
poor_damage_map_declare(shared);
poor_damage_map_declare(single);
poor_damage_map_declare(few);

struct squares {
	square map;
	uint64_t cells[1024];
};

struct screens {
	screen map;
	uint64_t cells[2304];
};

struct editors {
	uint64_t cells[5760];
	editor map;
};

struct wides {
	wide map;
	uint64_t cells[1024];
};

struct tables {
	uint64_t cells[8][16];
	table map;
};

struct scratch {
	int32_t width, height;
	union {
		uint64_t cells[16];
		unsigned char bytes[128];
	};
	shared map;
};

poor_damage_map_attach_buffer(square, struct squares, map, cells);
poor_damage_map_attach_buffer(screen, struct screens, map, cells);
poor_damage_map_attach_buffer(editor, struct editors, map, cells);
poor_damage_map_attach_buffer(wide, struct wides, map, cells);
poor_damage_map_attach_buffer(table, struct tables, map, cells);
struct singles {
	single map;
	uint64_t cells[1];
};

poor_damage_map_attach_buffer(shared, struct scratch, map, cells);
struct fews {
	few map;
	uint64_t cells[32];
};

poor_damage_map_attach_buffer(single, struct singles, map, cells);
poor_damage_map_attach_buffer(few, struct fews, map, cells);

#define has_grid(map, ...) has_grid_impl(&(map)->grid, __VA_ARGS__)

static bool has_grid_impl(const struct poor_damage_grid *grid, int32_t cell_width, int32_t cell_height, size_t columns,
			  size_t rows)
{
	return grid->cell_width == cell_width && grid->cell_height == cell_height && grid->columns == columns &&
	       grid->rows == rows;
}

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
	struct squares s;

	poor_damage_map_init(&s.map, 650, 100, 21, 4);
	assert(has_grid(&s.map, 21, 4, 31, 25));
	poor_damage_map_init(&s.map, 1, 1, 1, 1);
	assert(has_grid(&s.map, 1, 1, 1, 1));
	poor_damage_map_init(&s.map, 640, 480, 64, 64);
	assert(has_grid(&s.map, 64, 64, 10, 8));
	poor_damage_map_init(&s.map, 640, 480, 1, 1);
	assert(has_grid(&s.map, 18, 18, 36, 27));
}

TEST(one_budget_fits_any_shape)
{
	struct screens s;

	poor_damage_map_init(&s.map, 1920, 1080, 64, 64);
	assert(has_grid(&s.map, 64, 64, 30, 17));
	poor_damage_map_init(&s.map, 8000, 500, 64, 64);
	assert(has_grid(&s.map, 64, 64, 125, 8));
	poor_damage_map_init(&s.map, 1500, 15000, 64, 64);
	assert(has_grid(&s.map, 128, 128, 12, 118));
}

TEST(cells_grow_by_their_multiples_when_the_budget_runs_out)
{
	struct editors s;

	poor_damage_map_init(&s.map, 1500, 15000, 64, 64);
	assert(has_grid(&s.map, 64, 64, 24, 235));
	poor_damage_map_init(&s.map, 3000, 40000, 64, 64);
	assert(has_grid(&s.map, 192, 192, 16, 209));
}

TEST(cells_stay_within_the_surface)
{
	struct singles s;

	poor_damage_map_init(&s.map, 65536, 65536, 65536, 1);
	assert(has_grid(&s.map, 65536, 65536, 1, 1));
	poor_damage_map_init(&s.map, 1'000'000'000, 1, 1, 1);
	assert(has_grid(&s.map, 1'000'000'000, 1, 1, 1));
}

TEST(huge_cells_answer_within_the_surface)
{
	struct fews s;
	region stale;

	poor_damage_map_init(&s.map, 1'500'000'000, 1'000'000'000, 20, 1);
	assert(has_grid(&s.map, 1'250'000'000, 62'500'000, 2, 16));
	drawn(&s.map, 0, 0, 0, 0);
	drawn(&s.map, 0, 0, INT32_MAX, INT32_MAX);
	poor_damage_map_since(&s.map, 1, &stale);
	assert(is(&stale, 0, 0, 1'500'000'000, 1'000'000'000));
	drawn(&s.map, 1'499'999'999, 999'999'999, 1, 1);
	poor_damage_map_since(&s.map, 2, &stale);
	assert(is(&stale, 1'250'000'000, 937'500'000, 250'000'000, 62'500'000));
}

static int64_t over(int64_t size, int64_t cell)
{
	return (size + cell - 1) / cell;
}

TEST(cells_are_the_least_multiple_that_fits_the_budget)
{
	static const int32_t sizes[] = { 1, 7, 64, 99, 300, 1001 };
	static const int32_t cell_sizes[] = { 1, 3, 16 };
	struct poor_damage_surface surface;
	struct poor_damage_grid grid;
	uint64_t storage[64];

	foreach_array_index(storage, i)
		foreach_array_const_ref(sizes, width)
			foreach_array_const_ref(sizes, height)
				foreach_array_const_ref(cell_sizes, cell) {
					int64_t scale = 1, cells = (int64_t)i + 1;

					while (over(*width, scale * *cell) * over(*height, scale * 2 * *cell) > cells)
						scale++;
					poor_damage_map_init_impl(&surface, &grid, i + 1, arrview_first(i + 1, storage), *width,
								  *height, *cell, 2 * *cell);
					assert(grid.cell_width == (scale * *cell < *width ? scale * *cell : *width));
					assert(grid.cell_height == (scale * 2 * *cell < *height ? scale * 2 * *cell : *height));
					assert(grid.columns * grid.rows <= i + 1);
				}
}

TEST(grids_follow_the_shape_of_the_surface)
{
	struct wides s;
	region stale;

	poor_damage_map_init(&s.map, 8000, 500, 64, 64);
	assert(has_grid(&s.map, 64, 64, 125, 8));
	drawn(&s.map, 0, 0, 8000, 500);
	drawn(&s.map, 7990, 10, 5, 5);
	poor_damage_map_since(&s.map, 1, &stale);
	assert(is(&stale, 7936, 0, 64, 64));
}

TEST(buffers_of_arrays_are_budgets_of_all_their_cells)
{
	struct tables s;

	poor_damage_map_init(&s.map, 640, 480, 64, 64);
	assert(has_grid(&s.map, 64, 64, 10, 8));
	poor_damage_map_init(&s.map, 1280, 960, 64, 64);
	assert(has_grid(&s.map, 128, 128, 10, 8));
}

TEST(buffers_that_hold_nothing_lack_everything)
{
	struct squares s;
	region stale;

	poor_damage_map_init(&s.map, 640, 480, 1, 1);
	poor_damage_map_since(&s.map, 0, &stale);
	assert(is(&stale, 0, 0, 640, 480));
	assert(drawn(&s.map, 0, 0, 10, 10) == 1);
	poor_damage_map_since(&s.map, 0, &stale);
	assert(is(&stale, 0, 0, 640, 480));
}

TEST(buffers_lack_whole_cells_that_changed_since_their_frame)
{
	struct squares s;
	region stale;

	poor_damage_map_init(&s.map, 640, 480, 20, 15);
	drawn(&s.map, 25, 20, 10, 10);
	poor_damage_map_since(&s.map, 1, &stale);
	assert(!stale.count);
	drawn(&s.map, 25, 20, 10, 10);
	drawn(&s.map, 300, 300, 1, 1);
	poor_damage_map_since(&s.map, 2, &stale);
	assert(is(&stale, 300, 300, 20, 15));
	poor_damage_map_since(&s.map, 1, &stale);
	assert(stale.count == 2 && covers(&stale, &(struct poor_damage_rect){ 20, 15, 20, 15 }));
	assert(covers(&stale, &(struct poor_damage_rect){ 300, 300, 20, 15 }));
	for (int i = 0; i < 1000; i++)
		drawn(&s.map, 0, 0, 0, 0);
	poor_damage_map_since(&s.map, 1, &stale);
	assert(stale.count == 2);
}

TEST(cells_past_the_edges_are_clipped)
{
	struct squares s;
	region stale;

	poor_damage_map_init(&s.map, 650, 100, 21, 4);
	drawn(&s.map, 640, 90, 100, 100);
	drawn(&s.map, -50, -50, 10, 10);
	poor_damage_map_since(&s.map, 1, &stale);
	assert(!stale.count);
	poor_damage_map_since(&s.map, 0, &stale);
	assert(is(&stale, 0, 0, 650, 100));
	drawn(&s.map, 640, 90, 100, 100);
	poor_damage_map_since(&s.map, 2, &stale);
	assert(is(&stale, 630, 88, 20, 12));
}

TEST(many_changed_cells_stay_covered)
{
	struct squares s;
	region stale;
	struct poor_damage_rect rects[40];

	poor_damage_map_init(&s.map, 640, 480, 1, 1);
	drawn(&s.map, 0, 0, 640, 480);
	foreach_array_index(rects, i) {
		rects[i] = (struct poor_damage_rect){ (int32_t)i * 37 % 600, 11 * (int32_t)i, 5, 3 };
		drawn(&s.map, rects[i].x, rects[i].y, rects[i].width, rects[i].height);
	}
	poor_damage_map_since(&s.map, 1, &stale);
	assert(stale.count <= 4);
	foreach_array_const_ref(rects, rect)
		assert(covers(&stale, rect));
}

TEST(buffers_share_memory_with_anything_else)
{
	struct scratch scratch = { .width = 8, .height = 8 };
	region stale;

	fill_array(scratch.bytes, 0xff);
	poor_damage_map_init(&scratch.map, scratch.width, scratch.height, 2, 2);
	assert(!scratch.cells[15] && has_grid(&scratch.map, 2, 2, 4, 4));
	drawn(&scratch.map, 0, 0, 0, 0);
	drawn(&scratch.map, 6, 6, 2, 2);
	poor_damage_map_since(&scratch.map, poor_damage_frame_of_age(&scratch.map, 2), &stale);
	assert(is(&stale, 6, 6, 2, 2) && scratch.cells[15] == 2);
	poor_damage_map_since(&scratch.map, 0, &stale);
	assert(is(&stale, 0, 0, 8, 8));
}

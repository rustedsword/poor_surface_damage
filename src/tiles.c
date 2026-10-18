/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* The least multiple of 'tile' with which 'tiles' tiles cover 'size' */
static int32_t fit(int32_t size, size_t tiles, int32_t tile)
{
	int64_t least = (size + (int64_t)tiles - 1) / (int64_t)tiles;

	return (int32_t)((least + tile - 1) / tile * tile);
}

EXPORT void poor_damage_tiles_init_impl(struct poor_damage_surface *surface, struct poor_damage_grid *grid, size_t rows,
					size_t columns, uint64_t (*changed)[rows][columns], int32_t width, int32_t height,
					int32_t tile_width, int32_t tile_height)
{
	assert(rows && columns && tile_width > 0 && tile_height > 0);
	h_damage_surface_init(surface, width, height);
	grid->tile_width = fit(width, columns, tile_width);
	grid->tile_height = fit(height, rows, tile_height);
	grid->columns = (size_t)((width + grid->tile_width - 1) / grid->tile_width);
	grid->rows = (size_t)((height + grid->tile_height - 1) / grid->tile_height);
	foreach_array_ref(arrview_first(grid->rows, *changed), row)
		fill_array(arrview_first(grid->columns, *row), 0);
}

#define same_changes(a, b, frame) same_changes_impl(ARRAY_SIZE(a), &auto_arr(a), &auto_arr(b), frame)

/* Whether two rows of tiles changed the same tiles after 'frame' */
static bool same_changes_impl(size_t columns, const uint64_t (*a)[columns], const uint64_t (*b)[columns], uint64_t frame)
{
	foreach_array_index(a, column)
		if (((*a)[column] > frame) != ((*b)[column] > frame))
			return false;
	return true;
}

#define add_band(grid, changed, frame, top, bottom, capacity, region, count) \
	add_band_impl(grid, ARRAY_SIZE(changed), &auto_arr(changed), frame, top, bottom, capacity, region, count)

/* The tiles of rows from 'top' to 'bottom' that changed after 'frame' as the first of them did */
static void add_band_impl(const struct poor_damage_grid *grid, size_t columns, const uint64_t (*changed)[columns],
			  uint64_t frame, size_t top, size_t bottom, size_t capacity,
			  struct poor_damage_rect (*region)[capacity], size_t *count)
{
	for (size_t column = 0, start; column < columns; column++) {
		if ((*changed)[column] <= frame)
			continue;
		for (start = column; column < columns && (*changed)[column] > frame; column++)
			;
		poor_damage_region_add_impl(capacity, region, count,
					    (struct poor_damage_rect){ (int32_t)start * grid->tile_width,
								       (int32_t)top * grid->tile_height,
								       (int32_t)(column - start) * grid->tile_width,
								       (int32_t)(bottom - top) * grid->tile_height });
	}
}

EXPORT void poor_damage_tiles_lacks_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid,
					 size_t rows, size_t columns, const uint64_t (*changed)[rows][columns],
					 uint64_t frame, size_t region_capacity,
					 struct poor_damage_rect (*region)[region_capacity], size_t *region_count)
{
	size_t top = 0;

	assert(frame <= surface->frame);
	if (!frame) {
		poor_damage_region_whole_impl(region_capacity, region, region_count, surface->width, surface->height);
		return;
	}
	*region_count = 0;
	foreach_array_index(arrview_first(grid->rows, *changed), row) {
		if (same_changes(arrview_first(grid->columns, (*changed)[row]), arrview_first(grid->columns, (*changed)[top]),
				 frame))
			continue;
		add_band(grid, arrview_first(grid->columns, (*changed)[top]), frame, top, row, region_capacity, region,
			 region_count);
		top = row;
	}
	add_band(grid, arrview_first(grid->columns, (*changed)[top]), frame, top, grid->rows, region_capacity, region,
		 region_count);
	poor_damage_region_clip_impl(region_capacity, region, region_count, surface->width, surface->height);
}

EXPORT uint64_t poor_damage_tiles_drawn_impl(struct poor_damage_surface *surface, const struct poor_damage_grid *grid,
					     size_t rows, size_t columns, uint64_t (*changed)[rows][columns],
					     size_t damages, const struct poor_damage_rect (*damage)[damages])
{
	surface->frame++;
	foreach_array_const_ref(damage, rect) {
		int64_t x = greater(rect->x, 0), y = greater(rect->y, 0);
		int64_t right = lesser((int64_t)rect->x + rect->width, surface->width);
		int64_t bottom = lesser((int64_t)rect->y + rect->height, surface->height);
		size_t column = (size_t)(x / grid->tile_width), row = (size_t)(y / grid->tile_height);

		if (right <= x || bottom <= y)
			continue;
		foreach_array_ref(arrview(row, (size_t)((bottom + grid->tile_height - 1) / grid->tile_height) - row, *changed),
				  tiles)
			fill_array(arrview(column, (size_t)((right + grid->tile_width - 1) / grid->tile_width) - column, *tiles),
				   surface->frame);
	}
	return surface->frame;
}

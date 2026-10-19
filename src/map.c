/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* View the cells in use as 'rows' arrays of 'columns' cells */
#define make_raster(name, grid, changed)								\
	make_arrview_first(name##_cells, (grid)->rows * (grid)->columns, *(changed));			\
	make_arrview_dim(name, (grid)->columns, name##_cells)

static int64_t cells_over(int64_t size, int64_t cell)
{
	return (size + cell - 1) / cell;
}

EXPORT void poor_damage_map_init_impl(struct poor_damage_surface *surface, struct poor_damage_grid *grid, size_t cells,
				      uint64_t (*changed)[cells], int32_t width, int32_t height, int32_t cell_width,
				      int32_t cell_height)
{
	int64_t least = 1, most;

	assert(cells && cell_width > 0 && cell_height > 0);
	poor_damage_surface_init(surface, width, height);
	most = greater(cells_over(width, cell_width), cells_over(height, cell_height));
	while (least < most) {
		int64_t scale = least + (most - least) / 2;

		if (cells_over(width, scale * cell_width) * cells_over(height, scale * cell_height) <= (int64_t)cells)
			most = scale;
		else
			least = scale + 1;
	}
	grid->cell_width = (int32_t)lesser(least * cell_width, width);
	grid->cell_height = (int32_t)lesser(least * cell_height, height);
	grid->columns = (size_t)cells_over(width, grid->cell_width);
	grid->rows = (size_t)cells_over(height, grid->cell_height);
	fill_array(arrview_first(grid->rows * grid->columns, *changed), 0);
}

#define same_changes(a, b, frame) same_changes_impl(ARRAY_SIZE(a), &auto_arr(a), &auto_arr(b), frame)

/* Whether the same cells of two rows changed after 'frame' */
static bool same_changes_impl(size_t columns, const uint64_t (*a)[columns], const uint64_t (*b)[columns], uint64_t frame)
{
	foreach_array_index(a, column)
		if (((*a)[column] > frame) != ((*b)[column] > frame))
			return false;
	return true;
}

#define add_band(surface, grid, changed, frame, top, bottom, capacity, region, count) \
	add_band_impl(surface, grid, ARRAY_SIZE(changed), &auto_arr(changed), frame, top, bottom, capacity, region, count)

/* Add the cells that changed after 'frame' in rows 'top' to 'bottom', which all changed alike, clipped to the surface */
static void add_band_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t columns,
			  const uint64_t (*changed)[columns], uint64_t frame, size_t top, size_t bottom, size_t capacity,
			  struct poor_damage_rect (*region)[capacity], size_t *count)
{
	int64_t y = (int64_t)top * grid->cell_height;
	int64_t height = lesser((int64_t)bottom * grid->cell_height, surface->height) - y;

	for (size_t column = 0, start; column < columns; column++) {
		int64_t x, width;

		if ((*changed)[column] <= frame)
			continue;
		for (start = column; column < columns && (*changed)[column] > frame; column++)
			;
		x = (int64_t)start * grid->cell_width;
		width = lesser((int64_t)column * grid->cell_width, surface->width) - x;
		poor_damage_rects_add(region, count,
				      (struct poor_damage_rect){ (int32_t)x, (int32_t)y, (int32_t)width, (int32_t)height });
	}
}

EXPORT void poor_damage_map_lacks_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid,
				       size_t cells, const uint64_t (*changed)[cells], uint64_t frame,
				       size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
				       size_t *region_count)
{
	size_t top = 0;
	make_raster(raster, grid, changed);

	assert(frame <= surface->frame);
	if (!frame) {
		poor_damage_rects_whole(region, region_count, surface->width, surface->height);
		return;
	}
	*region_count = 0;
	foreach_array_index(raster, row) {
		if (same_changes((*raster)[row], (*raster)[top], frame))
			continue;
		add_band(surface, grid, (*raster)[top], frame, top, row, region_capacity, region, region_count);
		top = row;
	}
	add_band(surface, grid, (*raster)[top], frame, top, grid->rows, region_capacity, region, region_count);
}

EXPORT uint64_t poor_damage_map_drawn_impl(struct poor_damage_surface *surface, const struct poor_damage_grid *grid,
					   size_t cells, uint64_t (*changed)[cells], size_t damages,
					   const struct poor_damage_rect (*damage)[damages])
{
	make_raster(raster, grid, changed);

	surface->frame++;
	foreach_array_const_ref(damage, rect) {
		int64_t x = greater(rect->x, 0), y = greater(rect->y, 0);
		int64_t right = lesser((int64_t)rect->x + rect->width, surface->width);
		int64_t bottom = lesser((int64_t)rect->y + rect->height, surface->height);
		size_t column = (size_t)(x / grid->cell_width), row = (size_t)(y / grid->cell_height);

		if (right <= x || bottom <= y)
			continue;
		foreach_array_ref(arrview(row, (size_t)cells_over(bottom, grid->cell_height) - row, *raster), row_cells)
			fill_array(arrview(column, (size_t)cells_over(right, grid->cell_width) - column, *row_cells),
				   surface->frame);
	}
	return surface->frame;
}

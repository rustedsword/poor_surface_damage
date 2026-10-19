/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_MAP_H
#define POOR_DAMAGE_MAP_H

#include "poor_damage_region.h"
#include "poor_damage_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Poor Damage Map.
 *
 * Damage drawn into a raster of up to 'columns' x 'rows' cells over the surface, the way rendering draws into a
 * texture, except that cells hold the number of the frame that last changed them rather than colors. A buffer that
 * holds any frame lacks the cells of later frames. Cells are of the size that init asks for, or of its least multiple
 * with which the raster covers the surface: 64 x 36 cells of 64 pixels cover 4096 x 2304, and larger surfaces get cells
 * of 128 pixels. Smaller cells follow the damage closer, but take more memory and more time to tell what a buffer lacks:
 *
 *   poor_damage_map_declare(editor_map, 24, 240);
 *
 *   editor_map map;
 *   uint64_t holds[3] = {};
 *
 *   poor_damage_map_init(&map, 1500, 15000, 64, 64);
 *   ...
 *   poor_damage_map_lacks(&map, holds[index], &repair);
 *   poor_damage_region_add_region(&repair, &damage);
 *   ... redraw 'repair' into buffer 'index' ...
 *   holds[index] = poor_damage_map_drawn(&map, &damage);
 *
 * Memory, and the time to tell what a buffer lacks, grow with the cells, not with buffers or frames.
 */
struct poor_damage_grid {
	int32_t cell_width, cell_height;
	size_t columns, rows;
};

#define poor_damage_map_declare(name, columns, rows) h_damage_map_declare(name, columns, rows)

/* No frames drawn yet on 'width' x 'height', in cells of 'cell_width' x 'cell_height' or of their multiples */
#define poor_damage_map_init(map, width, height, cell_width, cell_height) \
	poor_damage_map_init_impl(h_damage_map(map), width, height, cell_width, cell_height)

/* Sets 'region' to what a buffer that holds 'frame' lacks */
#define poor_damage_map_lacks(map, frame, region) \
	poor_damage_map_lacks_impl(h_damage_map(map), frame, h_damage_region(region))

/* A frame with 'damage' was drawn. Returns its number. */
#define poor_damage_map_drawn(map, ...) poor_damage_map_drawn_impl(h_damage_map(map), h_damage_rects((__VA_ARGS__)))

void
poor_damage_map_init_impl(struct poor_damage_surface *surface, struct poor_damage_grid *grid, size_t rows,
			  size_t columns, uint64_t (*changed)[rows][columns], int32_t width, int32_t height,
			  int32_t cell_width, int32_t cell_height);

void
poor_damage_map_lacks_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t rows,
			   size_t columns, const uint64_t (*changed)[rows][columns], uint64_t frame,
			   size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
			   size_t *region_count);

uint64_t
poor_damage_map_drawn_impl(struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t rows,
			   size_t columns, uint64_t (*changed)[rows][columns], size_t damages,
			   const struct poor_damage_rect (*damage)[damages]);

/* Implementation details, subject to change. */

#define h_damage_map_declare(name, columns, rows) \
	typedef struct name { \
		struct poor_damage_surface surface; \
		struct poor_damage_grid grid; \
		uint64_t changed[rows][columns]; \
	} name

#define h_damage_map(map) \
	&(map)->surface, &(map)->grid, ARRAY_SIZE((map)->changed), ARRAY_SIZE((map)->changed[0]), &(map)->changed

#ifdef __cplusplus
}
#endif

#endif

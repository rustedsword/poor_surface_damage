/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_TILES_H
#define POOR_DAMAGE_TILES_H

#include "poor_damage_region.h"
#include "poor_damage_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Poor Damage Tiles.
 *
 * The last frame that changed each tile of a grid of up to 'columns' x 'rows' tiles over the surface, for a buffer
 * that holds any frame. Tiles are of the size that init asks for, or of its least multiple with which the grid covers
 * the surface: 64 x 36 tiles of 64 pixels cover 4096 x 2304, and larger surfaces get tiles of 128 pixels. A buffer
 * lacks whole tiles:
 *
 *   poor_damage_tiles_declare(editor_tiles, 24, 240);
 *
 *   editor_tiles tiles;
 *   uint64_t holds[3] = {};
 *
 *   poor_damage_tiles_init(&tiles, 1500, 15000, 64, 64);
 *   ...
 *   poor_damage_tiles_lacks(&tiles, holds[index], &repair);
 *   poor_damage_region_add_region(&repair, &damage);
 *   ... redraw 'repair' into buffer 'index' ...
 *   holds[index] = poor_damage_tiles_drawn(&tiles, &damage);
 *
 * Memory, and the time to tell what a buffer lacks, grow with the tiles of the grid, not with buffers or frames.
 */
struct poor_damage_grid {
	int32_t tile_width, tile_height;
	size_t columns, rows;
};

#define poor_damage_tiles_declare(name, columns, rows) h_damage_tiles_declare(name, columns, rows)

/* No frames drawn yet on 'width' x 'height', in tiles of 'tile_width' x 'tile_height' or of their multiples */
#define poor_damage_tiles_init(tiles, width, height, tile_width, tile_height) \
	poor_damage_tiles_init_impl(h_damage_tiles(tiles), width, height, tile_width, tile_height)

/* Sets 'region' to what a buffer that holds 'frame' lacks */
#define poor_damage_tiles_lacks(tiles, frame, region) \
	poor_damage_tiles_lacks_impl(h_damage_tiles(tiles), frame, h_damage_region(region))

/* A frame with 'damage' was drawn. Returns its number. */
#define poor_damage_tiles_drawn(tiles, ...) \
	poor_damage_tiles_drawn_impl(h_damage_tiles(tiles), h_damage_rects((__VA_ARGS__)))

void
poor_damage_tiles_init_impl(struct poor_damage_surface *surface, struct poor_damage_grid *grid, size_t rows,
			    size_t columns, uint64_t (*changed)[rows][columns], int32_t width, int32_t height,
			    int32_t tile_width, int32_t tile_height);

void
poor_damage_tiles_lacks_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid,
			     size_t rows, size_t columns, const uint64_t (*changed)[rows][columns], uint64_t frame,
			     size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
			     size_t *region_count);

uint64_t
poor_damage_tiles_drawn_impl(struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t rows,
			     size_t columns, uint64_t (*changed)[rows][columns], size_t damages,
			     const struct poor_damage_rect (*damage)[damages]);

/* Implementation details, subject to change. */

#define h_damage_tiles_declare(name, columns, rows) \
	typedef struct name { \
		struct poor_damage_surface surface; \
		struct poor_damage_grid grid; \
		uint64_t changed[rows][columns]; \
	} name

#define h_damage_tiles(tiles) \
	&(tiles)->surface, &(tiles)->grid, ARRAY_SIZE((tiles)->changed), ARRAY_SIZE((tiles)->changed[0]), \
		&(tiles)->changed

#ifdef __cplusplus
}
#endif

#endif

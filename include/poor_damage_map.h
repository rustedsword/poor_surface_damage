/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_MAP_H
#define POOR_DAMAGE_MAP_H

#include <stddef.h>

#include "poor_damage_region.h"
#include "poor_damage_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Poor Damage Map.
 *
 * Damage drawn into a raster of cells over the surface, the way rendering draws into a texture, except that cells hold
 * the number of the frame that last changed them rather than colors. A buffer that holds any frame lacks the cells of
 * later frames.
 *
 * The raster is the application's buffer: an array of uint64_t, one per cell, in one of its structs, where it can share
 * memory with anything else. The map lives in the same struct and finds its buffer there, the way a list finds its
 * items from their links in poor_list.h, so its type is declared before that struct, and the buffer attached after it:
 *
 *   poor_damage_map_declare(screen_map);
 *
 *   struct screen {
 *           screen_map map;
 *           uint64_t damage[2304];
 *   };
 *
 *   poor_damage_map_attach_buffer(screen_map, struct screen, map, damage);
 *
 *   struct screen screen;
 *   uint64_t holds[3] = {};
 *
 *   poor_damage_map_init(&screen.map, width, height, 64, 64);
 *   ...
 *   poor_damage_map_lacks(&screen.map, holds[index], &repair);
 *   poor_damage_region_add_region(&repair, &damage);
 *   ... redraw 'repair' into buffer 'index' ...
 *   holds[index] = poor_damage_map_drawn(&screen.map, &damage);
 *
 * The buffer is only a budget of cells, and init shapes it after the surface. Cells are of the size that init asks
 * for, or of its least multiple with which they fit the budget: 2304 cells of 64 pixels cover 1920 x 1080 in 30 x 17,
 * and 8000 x 500 in 125 x 8, while 1500 x 15000 gets 12 x 118 cells of 128 pixels. Cells are never larger than the
 * surface. A buffer of arrays is a budget of all their cells. Smaller cells follow the damage closer, but take more
 * memory and more time to tell what a buffer lacks, which grow with the cells, not with buffers or frames.
 */
struct poor_damage_grid {
	int32_t cell_width, cell_height;
	size_t columns, rows;
};

/* Declares map type 'name' before the struct where maps of it live */
#define poor_damage_map_declare(name) h_damage_map_declare(name, h_damage_map_cat(h_damage_map_meta_, name))

/* Attaches 'member' of 'type' as the buffer of maps of type 'name' at 'path' in the same 'type' */
#define poor_damage_map_attach_buffer(name, type, path, member) \
	h_damage_map_attach_buffer(name, h_damage_map_cat(h_damage_map_meta_, name), type, path, member)

/* No frames drawn yet on 'width' x 'height', in cells of 'cell_width' x 'cell_height' or of a multiple of both */
#define poor_damage_map_init(map, width, height, cell_width, cell_height) \
	poor_damage_map_init_impl(h_damage_map(map), width, height, cell_width, cell_height)

/* Sets 'region' to what a buffer that holds 'frame' lacks */
#define poor_damage_map_lacks(map, frame, region) \
	poor_damage_map_lacks_impl(h_damage_map(map), frame, h_damage_region(region))

/* A frame with 'damage' was drawn. Returns its number. */
#define poor_damage_map_drawn(map, ...) poor_damage_map_drawn_impl(h_damage_map(map), h_damage_rects((__VA_ARGS__)))

void
poor_damage_map_init_impl(struct poor_damage_surface *surface, struct poor_damage_grid *grid, size_t cells,
			  uint64_t (*changed)[cells], int32_t width, int32_t height, int32_t cell_width,
			  int32_t cell_height);

void
poor_damage_map_lacks_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t cells,
			   const uint64_t (*changed)[cells], uint64_t frame, size_t region_capacity,
			   struct poor_damage_rect (*region)[region_capacity], size_t *region_count);

uint64_t
poor_damage_map_drawn_impl(struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t cells,
			   uint64_t (*changed)[cells], size_t damages, const struct poor_damage_rect (*damage)[damages]);

/* Implementation details, subject to change. */

#define h_damage_map_cat(a, ...) h_damage_map_primitive_cat(a, __VA_ARGS__)
#define h_damage_map_primitive_cat(a, ...) a ## __VA_ARGS__

/* The meta struct is never made: its members only tell where maps and their buffers are in their struct, and the cells
 * of buffers */
#define h_damage_map_declare(name, meta) \
	struct meta; \
	typedef union name { \
		struct { \
			struct poor_damage_surface surface; \
			struct poor_damage_grid grid; \
		}; \
		struct meta *h_meta; \
	} name

#define h_damage_map_attach_buffer(name, meta, type, path, member) \
	struct meta { \
		unsigned char h_path[offsetof(type, path) + 1]; \
		unsigned char h_member[offsetof(type, member) + 1]; \
		uint64_t (*h_buffer)[sizeof(((type *)nullptr)->member) / sizeof(uint64_t)]; \
		static_assert(_Generic(&((type *)nullptr)->path, name *: 1, default: 0), \
			      "map (" #path ") must be an unqualified " #name); \
		static_assert(ARRAY_SIZE(((type *)nullptr)->member) && \
				      _Generic((typeof(((type *)nullptr)->member[0]) *)nullptr, uint64_t *: 1, \
					       uint64_t (*)[h_damage_map_row(((type *)nullptr)->member)]: 1, default: 0), \
			      "buffer (" #member ") must be an array of unqualified uint64_t, or of arrays of them"); \
	}

#define h_damage_map_row(buffer) ((sizeof((buffer)[0]) + sizeof(uint64_t) - 1) / sizeof(uint64_t))

#define h_damage_map_buffer(map) \
	((typeof((map)->h_meta->h_buffer))((unsigned char *)(map) - (sizeof((map)->h_meta->h_path) - 1) + \
					     (sizeof((map)->h_meta->h_member) - 1)))

#define h_damage_map(map) &(map)->surface, &(map)->grid, ARRAY_SIZE(*h_damage_map_buffer(map)), h_damage_map_buffer(map)

#ifdef __cplusplus
}
#endif

#endif

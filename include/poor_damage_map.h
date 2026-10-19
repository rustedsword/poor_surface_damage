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
 * Draws damage into a low resolution image of the surface.
 *
 * Think of it as rendering into a texture, except that each cell stores the number of the last frame that changed it
 * instead of a color. To find out what a buffer is missing, look for the cells that changed after the frame it holds.
 * Since only the latest frame is kept per cell, this works for buffers of any age, for any number of buffers, and even
 * for things that aren't buffers at all, like a screen recorder that last saw frame N.
 *
 * The cells live in your own array of uint64_t, which has to be a member of the same struct as the map. The map finds
 * the array by its offset in that struct, the same way poor_list.h finds items from their links. So declare the map
 * type before the struct, and attach the array after it:
 *
 *   poor_damage_region_declare(damage_region, 4);
 *   poor_damage_map_declare(screen_map);
 *
 *   struct screen {
 *           screen_map map;
 *           uint64_t cells[2304];
 *   };
 *
 *   poor_damage_map_attach_buffer(screen_map, struct screen, map, cells);
 *
 *   struct screen screen;
 *   damage_region damage, repair;
 *   uint64_t holds[3] = {};  // the frame that each buffer holds, 0 for none
 *
 *   poor_damage_map_init(&screen.map, width, height, 64, 64);
 *
 *   // Then, for each frame:
 *
 *   // fill 'repair' with the cells that changed since buffer 'index' was last drawn into
 *   poor_damage_map_since(&screen.map, holds[index], &repair);
 *
 *   // add the damage of this frame to 'repair'
 *   poor_damage_region_add_region(&repair, &damage);
 *
 *   ... redraw 'repair' into buffer 'index', then present it with 'damage' ...
 *
 *   // draw the damage of this frame into the map, and remember which frame buffer 'index' holds now
 *   holds[index] = poor_damage_map_drawn(&screen.map, &damage);
 *
 * The array is just a budget of cells, and init picks the layout that fits the surface. Cells get the size passed to
 * init, or are scaled up by the same factor in both directions until they fit into the budget. For example, 2304 cells
 * of 64 pixels cover 1920 x 1080 as 30 x 17 cells, and 8000 x 500 as 125 x 8 cells, while 1500 x 15000 needs bigger
 * cells: 12 x 118 cells of 128 pixels. Cells never get bigger than the surface. A two-dimensional array works too, and
 * simply counts as all of its cells.
 *
 * Smaller cells follow the damage more closely, but take more memory and more time to search. Both depend only on the
 * number of cells, not on the number of buffers or frames.
 */

/* The layout of the cells, chosen by poor_damage_map_init(). */
struct poor_damage_grid {
	int32_t cell_width, cell_height;
	size_t columns, rows;
};

/* Declare a map type 'name'. Do this before the struct that holds the map and its cells. */
#define poor_damage_map_declare(name) h_damage_map_declare(name, h_damage_map_cat(h_damage_map_meta_, name))

/*
 * Tell maps of type 'name' where their cells are: the map is the member 'path' of 'type', and its cells are the member
 * 'member' of the same struct, an array of uint64_t.
 */
#define poor_damage_map_attach_buffer(name, type, path, member) \
	h_damage_map_attach_buffer(name, h_damage_map_cat(h_damage_map_meta_, name), type, path, member)

/*
 * Initialize the map for a 'width' x 'height' surface, with cells of 'cell_width' x 'cell_height' pixels, or bigger if
 * they don't fit into the budget otherwise. No frames are drawn yet.
 */
#define poor_damage_map_init(map, width, height, cell_width, cell_height) \
	poor_damage_map_init_impl(h_damage_map(map), width, height, cell_width, cell_height)

/*
 * Set 'region' to the cells that changed after 'frame', which is what a buffer holding 'frame' has missed. If 'frame'
 * is 0, 'region' is set to the whole surface.
 */
#define poor_damage_map_since(map, frame, region) \
	poor_damage_map_since_impl(h_damage_map(map), frame, ARRAY_SIZE((region)->rects), &(region)->rects, \
				   &(region)->count)

/* Call this after a frame with 'damage' was drawn. Returns the number of the frame. */
#define poor_damage_map_drawn(map, ...) \
	poor_damage_map_drawn_impl(h_damage_map(map), (__VA_ARGS__)->count, \
				   arrview_first((__VA_ARGS__)->count, (__VA_ARGS__)->rects))

/* Implementation details, subject to change. */

#define h_damage_map_cat(a, ...) h_damage_map_primitive_cat(a, __VA_ARGS__)
#define h_damage_map_primitive_cat(a, ...) a ## __VA_ARGS__

/*
 * The meta struct is never instantiated. Its members only record, in their types, where the map and its cells are in
 * the struct, and how many cells there are.
 */
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

void
poor_damage_map_init_impl(struct poor_damage_surface *surface, struct poor_damage_grid *grid, size_t cells,
			  uint64_t (*changed)[cells], int32_t width, int32_t height, int32_t cell_width,
			  int32_t cell_height);

void
poor_damage_map_since_impl(const struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t cells,
			   const uint64_t (*changed)[cells], uint64_t frame, size_t region_capacity,
			   struct poor_damage_rect (*region)[region_capacity], size_t *region_count);

uint64_t
poor_damage_map_drawn_impl(struct poor_damage_surface *surface, const struct poor_damage_grid *grid, size_t cells,
			   uint64_t (*changed)[cells], size_t damages, const struct poor_damage_rect (*damage)[damages]);

#ifdef __cplusplus
}
#endif

#endif

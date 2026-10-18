/* SPDX-License-Identifier: MIT */
#ifndef SCENE_H
#define SCENE_H

#include <poor_array.h>
#include <stdint.h>

#include "region.h"

/*
 * Boxes over a checkerboard of 16 pixel cells: they bounce, blink, grow and change colors, and now and then the board
 * changes colors. A step to the next frame tells what changed. Boxes that come later are drawn over earlier ones.
 */

constexpr size_t SCENE_BOXES = 12;
constexpr uint64_t SCENE_BOARD_PERIOD = 240;

enum scene_motion { SCENE_BOUNCE, SCENE_BLINK, SCENE_GROW, SCENE_CYCLE };

struct scene_box {
	enum scene_motion motion;
	struct poor_damage_rect rect;
	int32_t dx, dy, limit, period;
	uint32_t color;
	bool shown;
};

struct scene {
	int32_t width, height;
	uint64_t frame;
	uint32_t random;
	uint32_t board[2];
	struct scene_box boxes[SCENE_BOXES];
};

void
scene_init(struct scene *scene, int32_t width, int32_t height, uint32_t seed);

/* Boxes keep within the new size, and everything changes */
void
scene_resize(struct scene *scene, int32_t width, int32_t height, region *damage);

void
scene_step(struct scene *scene, region *damage);

/* Draws what lies within 'clip' into 'pixels' of the scene's size */
void
scene_draw_impl(const struct scene *scene, size_t height, size_t width, uint32_t (*pixels)[height][width],
		struct poor_damage_rect clip);
#define scene_draw(scene, pixels, clip) \
	scene_draw_impl(scene, ARRAY_SIZE(pixels), ARRAY_SIZE(auto_arr(pixels)[0]), &auto_arr(pixels), clip)

#endif

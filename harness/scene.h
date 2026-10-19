/* SPDX-License-Identifier: MIT */
#ifndef SCENE_H
#define SCENE_H

#include <poor_array.h>
#include <stdint.h>

#include "region.h"

/*
 * A test scene: boxes over a checkerboard of 16 pixel squares. Some boxes bounce around, some blink, some grow like
 * progress bars, and some change colors. Every 240 frames the whole checkerboard changes colors. scene_step() moves to
 * the next frame and adds what changed to the damage. Boxes are drawn in order, later ones on top.
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

/* Resize the scene: the boxes are placed again within the new size, and the whole surface is damaged */
void
scene_resize(struct scene *scene, int32_t width, int32_t height, region *damage);

void
scene_step(struct scene *scene, region *damage);

/* Draw the part of the scene inside 'clip' into 'pixels', which has the size of the scene */
void
scene_draw_impl(const struct scene *scene, size_t height, size_t width, uint32_t (*pixels)[height][width],
		struct poor_damage_rect clip);
#define scene_draw(scene, pixels, clip) \
	scene_draw_impl(scene, ARRAY_SIZE(pixels), ARRAY_SIZE(auto_arr(pixels)[0]), &auto_arr(pixels), clip)

#endif

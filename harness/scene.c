/* SPDX-License-Identifier: MIT */
#include <assert.h>

#include "scene.h"

static uint32_t next(struct scene *scene)
{
	scene->random ^= scene->random << 13;
	scene->random ^= scene->random >> 17;
	scene->random ^= scene->random << 5;
	return scene->random;
}

static int32_t between(struct scene *scene, int32_t low, int32_t high)
{
	return low + (int32_t)(next(scene) % (uint32_t)(high - low + 1));
}

static int32_t lesser(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static int32_t greater(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static struct poor_damage_rect intersect(struct poor_damage_rect a, struct poor_damage_rect b)
{
	int32_t x = greater(a.x, b.x), y = greater(a.y, b.y);
	int32_t right = lesser(a.x + a.width, b.x + b.width), bottom = lesser(a.y + a.height, b.y + b.height);

	return (struct poor_damage_rect){ x, y, greater(right - x, 0), greater(bottom - y, 0) };
}

static uint32_t bright(struct scene *scene)
{
	return 0xff404040 | (next(scene) & 0xffffff);
}

static uint32_t dark(struct scene *scene)
{
	return 0xff000000 | (next(scene) & 0x3f3f3f);
}

static void place(struct scene *scene, struct scene_box *box)
{
	int32_t size = between(scene, lesser(scene->width, scene->height) / 40 + 2,
			       lesser(scene->width, scene->height) / 10 + 4);

	box->rect.width = lesser(size, scene->width);
	box->rect.height = lesser(box->motion == SCENE_GROW ? size / 3 + 1 : size, scene->height);
	box->rect.x = between(scene, 0, scene->width - box->rect.width);
	box->rect.y = between(scene, 0, scene->height - box->rect.height);
	if (box->motion == SCENE_GROW) {
		box->limit = lesser(4 * size, scene->width - box->rect.x);
		box->rect.width = 0;
	}
}

void scene_init(struct scene *scene, int32_t width, int32_t height, uint32_t seed)
{
	assert(width > 0 && height > 0 && seed);
	scene->width = width;
	scene->height = height;
	scene->frame = 0;
	scene->random = seed;
	scene->board[0] = dark(scene);
	scene->board[1] = dark(scene);
	foreach_array_ref(scene->boxes, box) {
		box->motion = (enum scene_motion)(array_ref_index(scene->boxes, box) % 4);
		box->dx = between(scene, 1, 3) * (next(scene) & 1 ? 1 : -1);
		box->dy = between(scene, 1, 3) * (next(scene) & 1 ? 1 : -1);
		box->period = between(scene, 8, 40);
		box->color = bright(scene);
		box->shown = true;
		place(scene, box);
	}
}

void scene_resize(struct scene *scene, int32_t width, int32_t height, region *damage)
{
	assert(width > 0 && height > 0);
	scene->width = width;
	scene->height = height;
	foreach_array_ref(scene->boxes, box)
		place(scene, box);
	poor_damage_region_whole(damage, width, height);
}

static void bounce(struct scene *scene, struct scene_box *box, region *damage)
{
	poor_damage_region_add(damage, box->rect);
	if (box->rect.x + box->dx < 0 || box->rect.x + box->dx + box->rect.width > scene->width)
		box->dx = -box->dx;
	if (box->rect.y + box->dy < 0 || box->rect.y + box->dy + box->rect.height > scene->height)
		box->dy = -box->dy;
	box->rect.x = greater(lesser(box->rect.x + box->dx, scene->width - box->rect.width), 0);
	box->rect.y = greater(lesser(box->rect.y + box->dy, scene->height - box->rect.height), 0);
	poor_damage_region_add(damage, box->rect);
}

static void grow(struct scene_box *box, region *damage)
{
	int32_t speed = box->dx < 0 ? -box->dx : box->dx;

	if (box->rect.width + speed > box->limit) {
		poor_damage_region_add(damage, box->rect);
		box->rect.width = 0;
		return;
	}
	poor_damage_region_add(damage, (struct poor_damage_rect){ box->rect.x + box->rect.width, box->rect.y, speed,
								 box->rect.height });
	box->rect.width += speed;
}

void scene_step(struct scene *scene, region *damage)
{
	if (++scene->frame % SCENE_BOARD_PERIOD == 0) {
		scene->board[0] = dark(scene);
		scene->board[1] = dark(scene);
		poor_damage_region_whole(damage, scene->width, scene->height);
	}
	foreach_array_ref(scene->boxes, box) {
		bool tick = scene->frame % (uint64_t)box->period == 0;

		switch (box->motion) {
		case SCENE_BOUNCE:
			bounce(scene, box, damage);
			break;
		case SCENE_BLINK:
			box->shown ^= tick;
			break;
		case SCENE_GROW:
			grow(box, damage);
			break;
		case SCENE_CYCLE:
			box->color = tick ? bright(scene) : box->color;
			break;
		}
		if (tick && (box->motion == SCENE_BLINK || box->motion == SCENE_CYCLE))
			poor_damage_region_add(damage, box->rect);
	}
}

void scene_draw_impl(const struct scene *scene, size_t height, size_t width, uint32_t (*pixels)[height][width],
		     struct poor_damage_rect clip)
{
	size_t y;

	assert(height == (size_t)scene->height && width == (size_t)scene->width);
	clip = intersect(clip, (struct poor_damage_rect){ 0, 0, scene->width, scene->height });
	y = (size_t)clip.y;
	foreach_array_ref(arrview((size_t)clip.y, (size_t)clip.height, *pixels), row) {
		foreach_array_ref(arrview((size_t)clip.x, (size_t)clip.width, *row), pixel)
			*pixel = scene->board[(((size_t)array_ref_index(*row, pixel) >> 4) ^ (y >> 4)) & 1];
		y++;
	}
	foreach_array_const_ref(scene->boxes, box) {
		struct poor_damage_rect part = intersect(box->rect, clip);

		if (!box->shown)
			continue;
		foreach_array_ref(arrview((size_t)part.y, (size_t)part.height, *pixels), row)
			fill_array(arrview((size_t)part.x, (size_t)part.width, *row), box->color);
	}
}

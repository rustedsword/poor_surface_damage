/* SPDX-License-Identifier: MIT */
#include <assert.h>

#include "poor_surface_damage.h"

#define EXPORT __attribute__((visibility("default")))

static bool overlap(int32_t a, int32_t a_size, int32_t b, int32_t b_size)
{
	return a < b + b_size && b < a + a_size;
}

static bool touch(int32_t a, int32_t a_size, int32_t b, int32_t b_size)
{
	return a <= b + b_size && b <= a + a_size;
}

static bool joins(const struct poor_damage_rect *a, const struct poor_damage_rect *b)
{
	return (overlap(a->x, a->width, b->x, b->width) && touch(a->y, a->height, b->y, b->height)) ||
	       (touch(a->x, a->width, b->x, b->width) && overlap(a->y, a->height, b->y, b->height));
}

static struct poor_damage_rect bounds(const struct poor_damage_rect *a, const struct poor_damage_rect *b)
{
	int32_t x = a->x < b->x ? a->x : b->x, y = a->y < b->y ? a->y : b->y;
	int32_t right = a->x + a->width > b->x + b->width ? a->x + a->width : b->x + b->width;
	int32_t bottom = a->y + a->height > b->y + b->height ? a->y + a->height : b->y + b->height;

	return (struct poor_damage_rect){ x, y, right - x, bottom - y };
}

static int64_t area(const struct poor_damage_rect *rect)
{
	return (int64_t)rect->width * rect->height;
}

#define merge(rects, count) merge_impl(ARRAY_SIZE(rects), &auto_arr(rects), count)

/* Rects that join merge first, then the ones whose bounds cover the least that didn't change */
static bool merge_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count)
{
	int64_t least = INT64_MAX;
	size_t first = 0, second = 0;

	for (size_t i = 0; i < *count; i++) {
		for (size_t j = i + 1; j < *count; j++) {
			struct poor_damage_rect both = bounds(&(*rects)[i], &(*rects)[j]);
			int64_t waste = joins(&(*rects)[i], &(*rects)[j]) ? -1 : area(&both) - area(&(*rects)[i]) - area(&(*rects)[j]);

			if (waste < least) {
				least = waste;
				first = i;
				second = j;
			}
		}
	}
	if (least >= 0 && *count <= POOR_DAMAGE_RECTS)
		return false;
	(*rects)[first] = bounds(&(*rects)[first], &(*rects)[second]);
	(*rects)[second] = (*rects)[--*count];
	return true;
}

EXPORT void poor_damage_region_init(struct poor_damage_region *region)
{
	region->count = 0;
}

EXPORT void poor_damage_region_add(struct poor_damage_region *region, struct poor_damage_rect rect)
{
	struct poor_damage_rect rects[POOR_DAMAGE_RECTS + 1];
	size_t count = region->count;

	if (rect.width <= 0 || rect.height <= 0)
		return;
	copy_array(arrview_first(count, rects), arrview_first(count, region->rects));
	rects[count++] = rect;
	while (merge(rects, &count))
		;
	copy_array(arrview_first(count, region->rects), arrview_first(count, rects));
	region->count = count;
}

EXPORT void poor_damage_region_add_region(struct poor_damage_region *region, const struct poor_damage_region *other)
{
	foreach_array_const_ref(arrview_first(other->count, other->rects), rect)
		poor_damage_region_add(region, *rect);
}

EXPORT void poor_damage_region_whole(struct poor_damage_region *region, int32_t width, int32_t height)
{
	assert(width > 0 && height > 0);
	region->rects[0] = (struct poor_damage_rect){ 0, 0, width, height };
	region->count = 1;
}

EXPORT void poor_damage_drawn_impl(size_t count, struct poor_damage_region (*buffers)[count], size_t index,
				   const struct poor_damage_region *damage)
{
	assert(index < count);
	foreach_array_index(buffers, i)
		if (i != index)
			poor_damage_region_add_region(&(*buffers)[i], damage);
	(*buffers)[index].count = 0;
}

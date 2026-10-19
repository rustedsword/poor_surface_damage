/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool overlap(int64_t a, int64_t a_size, int64_t b, int64_t b_size)
{
	return a < b + b_size && b < a + a_size;
}

static bool touch(int64_t a, int64_t a_size, int64_t b, int64_t b_size)
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
	int64_t x = lesser(a->x, b->x), y = lesser(a->y, b->y);
	int64_t right = greater((int64_t)a->x + a->width, (int64_t)b->x + b->width);
	int64_t bottom = greater((int64_t)a->y + a->height, (int64_t)b->y + b->height);

	return (struct poor_damage_rect){ (int32_t)x, (int32_t)y, (int32_t)lesser(right - x, INT32_MAX),
					  (int32_t)lesser(bottom - y, INT32_MAX) };
}

static int64_t area(const struct poor_damage_rect *rect)
{
	return (int64_t)rect->width * rect->height;
}

/* How much undamaged area the bounding box of two separate rects adds */
static int64_t waste(const struct poor_damage_rect *a, const struct poor_damage_rect *b)
{
	struct poor_damage_rect both = bounds(a, b);

	return area(&both) - area(a) - area(b);
}

/* Merge every rect that touches or overlaps 'rect' into it. The rects of a region never touch each other, so only
 * 'rect' has to be checked. */
static void absorb(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
		   struct poor_damage_rect *rect)
{
	for (size_t i = 0; i < *count;) {
		if (!joins(&(*rects)[i], rect)) {
			i++;
			continue;
		}
		*rect = bounds(&(*rects)[i], rect);
		(*rects)[i] = (*rects)[--*count];
		i = 0;
	}
}

/* The region is full: merge the two rects, 'rect' included, whose bounding box adds the least undamaged area */
static void make_room(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
		      struct poor_damage_rect *rect)
{
	int64_t least = INT64_MAX;
	size_t first = 0, second = *count;
	struct poor_damage_rect both;

	for (size_t i = 0; i < *count; i++) {
		for (size_t j = i + 1; j <= *count; j++) {
			int64_t cost = waste(&(*rects)[i], j < *count ? &(*rects)[j] : rect);

			if (cost < least) {
				least = cost;
				first = i;
				second = j;
			}
		}
	}
	if (second == *count) {
		*rect = bounds(&(*rects)[first], rect);
		(*rects)[first] = (*rects)[--*count];
		return;
	}
	both = bounds(&(*rects)[first], &(*rects)[second]);
	(*rects)[second] = (*rects)[--*count];
	(*rects)[first] = (*rects)[--*count];
	poor_damage_region_add_impl(capacity, rects, count, both);
}

EXPORT void poor_damage_region_add_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
					struct poor_damage_rect rect)
{
	assert(capacity && *count <= capacity);
	if (rect.width <= 0 || rect.height <= 0)
		return;
	for (absorb(capacity, rects, count, &rect); *count == capacity; absorb(capacity, rects, count, &rect))
		make_room(capacity, rects, count, &rect);
	(*rects)[(*count)++] = rect;
}

EXPORT void poor_damage_region_add_rects_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
					      size_t others, const struct poor_damage_rect (*other)[others])
{
	foreach_array_const_ref(other, rect)
		poor_damage_region_add_impl(capacity, rects, count, *rect);
}

EXPORT void poor_damage_region_whole_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
					  int32_t width, int32_t height)
{
	assert(capacity && width > 0 && height > 0);
	(*rects)[0] = (struct poor_damage_rect){ 0, 0, width, height };
	*count = 1;
}

EXPORT void poor_damage_region_clip_impl(size_t capacity, struct poor_damage_rect (*rects)[capacity], size_t *count,
					 int32_t width, int32_t height)
{
	size_t kept = 0;

	foreach_array_const_ref(arrview_first(*count, *rects), rect) {
		int64_t x = greater(rect->x, 0), y = greater(rect->y, 0);
		int64_t right = lesser((int64_t)rect->x + rect->width, width);
		int64_t bottom = lesser((int64_t)rect->y + rect->height, height);

		if (right > x && bottom > y)
			(*rects)[kept++] = (struct poor_damage_rect){ (int32_t)x, (int32_t)y, (int32_t)(right - x),
								      (int32_t)(bottom - y) };
	}
	*count = kept;
}

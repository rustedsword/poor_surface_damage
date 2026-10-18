/* SPDX-License-Identifier: MIT */
#include "internal.h"

EXPORT void poor_damage_buffers_init_impl(struct poor_damage_surface *surface, size_t buffers, size_t capacity,
					  size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
					  int32_t width, int32_t height)
{
	h_damage_surface_init(surface, width, height);
	foreach_array_index(counts, i)
		poor_damage_buffers_forget_impl(surface, buffers, capacity, counts, rects, i);
}

EXPORT void poor_damage_buffers_forget_impl(const struct poor_damage_surface *surface, size_t buffers, size_t capacity,
					    size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
					    size_t index)
{
	assert(index < buffers);
	poor_damage_region_whole_impl(capacity, &(*rects)[index], &(*counts)[index], surface->width, surface->height);
}

EXPORT void poor_damage_buffers_lacks_impl(const struct poor_damage_surface *surface, size_t buffers, size_t capacity,
					   const size_t (*counts)[buffers],
					   const struct poor_damage_rect (*rects)[buffers][capacity], size_t index,
					   size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
					   size_t *region_count)
{
	assert(index < buffers);
	*region_count = 0;
	poor_damage_region_add_rects_impl(region_capacity, region, region_count, (*counts)[index],
					  arrview_first((*counts)[index], (*rects)[index]));
}

EXPORT uint64_t poor_damage_buffers_drawn_impl(struct poor_damage_surface *surface, size_t buffers, size_t capacity,
					       size_t (*counts)[buffers], struct poor_damage_rect (*rects)[buffers][capacity],
					       size_t index, size_t damages, const struct poor_damage_rect (*damage)[damages])
{
	assert(index < buffers);
	foreach_array_index(counts, i)
		if (i != index)
			poor_damage_region_add_rects_impl(capacity, &(*rects)[i], &(*counts)[i], damages, damage);
	(*counts)[index] = 0;
	return ++surface->frame;
}

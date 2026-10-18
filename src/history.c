/* SPDX-License-Identifier: MIT */
#include "internal.h"

EXPORT void poor_damage_history_lacks_impl(const struct poor_damage_surface *surface, size_t frames, size_t capacity,
					   const size_t (*counts)[frames],
					   const struct poor_damage_rect (*rects)[frames][capacity], uint64_t frame,
					   size_t region_capacity, struct poor_damage_rect (*region)[region_capacity],
					   size_t *region_count)
{
	assert(frame <= surface->frame);
	if (!frame || surface->frame - frame > frames) {
		poor_damage_region_whole_impl(region_capacity, region, region_count, surface->width, surface->height);
		return;
	}
	*region_count = 0;
	while (frame++ < surface->frame) {
		size_t slot = (size_t)(frame % frames);

		poor_damage_region_add_rects_impl(region_capacity, region, region_count, (*counts)[slot],
						  arrview_first((*counts)[slot], (*rects)[slot]));
	}
}

EXPORT uint64_t poor_damage_history_drawn_impl(struct poor_damage_surface *surface, size_t frames, size_t capacity,
					       size_t (*counts)[frames], struct poor_damage_rect (*rects)[frames][capacity],
					       size_t damages, const struct poor_damage_rect (*damage)[damages])
{
	size_t slot = (size_t)(++surface->frame % frames);

	(*counts)[slot] = 0;
	poor_damage_region_add_rects_impl(capacity, &(*rects)[slot], &(*counts)[slot], damages, damage);
	return surface->frame;
}

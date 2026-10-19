/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <poor_stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracker.h"

static const char *const names[] = {
	[TRACKER_WHOLE] = "whole", [TRACKER_NAIVE] = "naive", [TRACKER_BUFFERS] = "buffers",
	[TRACKER_HISTORY] = "history", [TRACKER_MAP] = "map",
};

/* View the stored regions as 'regions' arrays of the configured number of rects */
#define make_kept(name, tracker, regions)									\
	make_arrview_first(name##_counts, regions, (tracker)->counts);						\
	make_arrview_first(name##_all, (regions) * (tracker)->options.rects, (tracker)->rects);			\
	make_arrview_dim(name, (tracker)->options.rects, name##_all)

#define changed_of(tracker) arrview_first((tracker)->options.cells, (tracker)->changed)

#define rects_of(region) (region)->count, arrview_first((region)->count, (region)->rects)

void tracker_init(struct tracker *tracker, const struct tracker_options *options, size_t buffers, int32_t width,
		  int32_t height)
{
	assert(buffers && buffers <= TRACKER_MAX_BUFFERS && options->rects && options->rects <= REGION_RECTS);
	assert(options->frames && options->frames <= TRACKER_MAX_FRAMES);
	assert(options->cells && options->cells <= TRACKER_MAX_CELLS);
	tracker->options = *options;
	tracker->buffers = buffers;
	tracker->surface = (struct poor_damage_surface)POOR_DAMAGE_SURFACE_INIT(width, height);
	fill_array(arrview_first(buffers, tracker->ages), 0);
	fill_array(arrview_first(buffers, tracker->holds), 0);
	if (options->kind == TRACKER_BUFFERS) {
		make_kept(kept, tracker, buffers);

		poor_damage_buffers_init_impl(&tracker->surface, buffers, options->rects, kept_counts, kept, width, height);
	}
	if (options->kind == TRACKER_MAP)
		poor_damage_map_init_impl(&tracker->surface, &tracker->grid, options->cells, changed_of(tracker), width,
					  height, options->cell_width, options->cell_height);
}

void tracker_limit(const struct tracker *tracker, region *damage)
{
	region all = *damage;
	make_arrview_first(limited, tracker->options.rects, damage->rects);

	damage->count = 0;
	poor_damage_rects_add_rects(limited, &damage->count, arrview_first(all.count, all.rects));
}

void tracker_repair(const struct tracker *tracker, size_t index, const region *damage, region *repair)
{
	size_t rects = tracker->options.rects;
	make_arrview_first(out, rects, repair->rects);

	assert(index < tracker->buffers);
	switch (tracker->options.kind) {
	case TRACKER_WHOLE:
		poor_damage_rects_whole(out, &repair->count, tracker->surface.width, tracker->surface.height);
		return;
	case TRACKER_NAIVE:
		repair->count = 0;
		break;
	case TRACKER_BUFFERS: {
		make_kept(kept, tracker, tracker->buffers);

		poor_damage_buffers_get_impl(tracker->buffers, rects, kept_counts, kept, index, rects, out,
					     &repair->count);
		break;
	}
	case TRACKER_HISTORY: {
		make_kept(kept, tracker, tracker->options.frames);

		poor_damage_history_since_impl(&tracker->surface, tracker->options.frames, rects, kept_counts, kept,
					       poor_damage_frame_of_age(tracker, tracker->ages[index]), rects, out,
					       &repair->count);
		break;
	}
	case TRACKER_MAP:
		poor_damage_map_since_impl(&tracker->surface, &tracker->grid, tracker->options.cells, changed_of(tracker),
					   tracker->holds[index], rects, out, &repair->count);
		break;
	}
	poor_damage_rects_add_rects(out, &repair->count, arrview_first(damage->count, damage->rects));
}

void tracker_drawn(struct tracker *tracker, size_t index, const region *damage)
{
	assert(index < tracker->buffers);
	switch (tracker->options.kind) {
	case TRACKER_WHOLE:
	case TRACKER_NAIVE:
		break;
	case TRACKER_BUFFERS: {
		make_kept(kept, tracker, tracker->buffers);

		poor_damage_buffers_drawn_impl(&tracker->surface, tracker->buffers, tracker->options.rects, kept_counts, kept,
					       index, rects_of(damage));
		break;
	}
	case TRACKER_HISTORY: {
		make_kept(kept, tracker, tracker->options.frames);

		poor_damage_history_drawn_impl(&tracker->surface, tracker->options.frames, tracker->options.rects,
					       kept_counts, kept, rects_of(damage));
		break;
	}
	case TRACKER_MAP:
		tracker->holds[index] = poor_damage_map_drawn_impl(&tracker->surface, &tracker->grid, tracker->options.cells,
								   changed_of(tracker), rects_of(damage));
		break;
	}
}

void tracker_presented(struct tracker *tracker, size_t index)
{
	assert(index < tracker->buffers);
	foreach_array_ref(arrview_first(tracker->buffers, tracker->ages), age)
		*age += *age > 0;
	tracker->ages[index] = 1;
}

bool tracker_option(struct tracker_options *options, int option, const char *value)
{
	switch (option) {
	case TRACKER_OPTION_KIND:
		return tracker_parse(value, &options->kind);
	case TRACKER_OPTION_RECTS:
		options->rects = strtoul(value, nullptr, 10);
		return options->rects && options->rects <= REGION_RECTS;
	case TRACKER_OPTION_HISTORY:
		options->frames = strtoul(value, nullptr, 10);
		return options->frames && options->frames <= TRACKER_MAX_FRAMES;
	case TRACKER_OPTION_CELLS:
		options->cells = strtoul(value, nullptr, 10);
		return options->cells && options->cells <= TRACKER_MAX_CELLS;
	case TRACKER_OPTION_CELL_SIZE:
		return sscanf(value, "%dx%d", &options->cell_width, &options->cell_height) == 2 && options->cell_width > 0 &&
		       options->cell_height > 0;
	}
	return false;
}

const char *tracker_name(enum tracker_kind kind)
{
	return names[kind];
}

bool tracker_parse(const char *name, enum tracker_kind *kind)
{
	foreach_array_index(names, i) {
		if (!strcmp(names[i], name)) {
			*kind = (enum tracker_kind)i;
			return true;
		}
	}
	return false;
}

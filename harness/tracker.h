/* SPDX-License-Identifier: MIT */
#ifndef TRACKER_H
#define TRACKER_H

#include <stddef.h>
#include <stdint.h>

#include "region.h"

/*
 * The trackers of the library behind one interface, along with two that are wasteful or wrong, to compare them with.
 * Their capacities are chosen at run time: storage is sized for the largest ones, and the library gets views of it.
 */

constexpr size_t TRACKER_MAX_BUFFERS = 8;
constexpr size_t TRACKER_MAX_FRAMES = 64;
constexpr size_t TRACKER_MAX_CELLS = 128 * 128;

enum tracker_kind {
	/* Redraws everything */
	TRACKER_WHOLE,
	/* Redraws only the damage of the frame, which is right while there is one buffer */
	TRACKER_NAIVE,
	TRACKER_BUFFERS,
	/* With ages of buffers as EGL_EXT_buffer_age counts them */
	TRACKER_HISTORY,
	TRACKER_MAP,
};

struct tracker_options {
	enum tracker_kind kind;
	/* Rects of each region, frames of the history, cells of the map, and their least size */
	size_t rects, frames, cells;
	int32_t cell_width, cell_height;
};

#define TRACKER_OPTIONS_INIT \
	{ .kind = TRACKER_BUFFERS, .rects = 4, .frames = 8, .cells = 4096, .cell_width = 16, .cell_height = 16 }

struct tracker {
	struct tracker_options options;
	size_t buffers;
	struct poor_damage_surface surface;
	struct poor_damage_grid grid;
	size_t counts[TRACKER_MAX_FRAMES];
	struct poor_damage_rect rects[TRACKER_MAX_FRAMES * REGION_RECTS];
	uint64_t changed[TRACKER_MAX_CELLS];
	uint32_t ages[TRACKER_MAX_BUFFERS];
	uint64_t holds[TRACKER_MAX_BUFFERS];
};

/* Options of the command line that choose a tracker and its capacities, for getopt_long() */
enum tracker_option {
	TRACKER_OPTION_KIND = 0x100,
	TRACKER_OPTION_RECTS,
	TRACKER_OPTION_HISTORY,
	TRACKER_OPTION_CELLS,
	TRACKER_OPTION_CELL_SIZE,
};

#define TRACKER_LONG_OPTIONS \
	{ "tracker", required_argument, nullptr, TRACKER_OPTION_KIND }, \
	{ "rects", required_argument, nullptr, TRACKER_OPTION_RECTS }, \
	{ "history", required_argument, nullptr, TRACKER_OPTION_HISTORY }, \
	{ "cells", required_argument, nullptr, TRACKER_OPTION_CELLS }, \
	{ "cell-size", required_argument, nullptr, TRACKER_OPTION_CELL_SIZE }

#define TRACKER_USAGE \
	"  --tracker T   whole, naive, buffers, history or map, buffers by default\n" \
	"  --rects N     rects of each region, 4 by default, up to 64\n" \
	"  --history N   frames of the history, 8 by default, up to 64\n" \
	"  --cells N     cells of the map, 4096 by default, up to 16384\n" \
	"  --cell-size WxH  the least size of cells, 16x16 by default\n"

/* Takes the value of an option of TRACKER_LONG_OPTIONS, returns false when it's wrong */
bool
tracker_option(struct tracker_options *options, int option, const char *value);

/* 'buffers' buffers of 'width' x 'height' that hold nothing yet */
void
tracker_init(struct tracker *tracker, const struct tracker_options *options, size_t buffers, int32_t width,
	     int32_t height);

/* Limits 'damage' to the rects of the tracker, as an application of that capacity would have it */
void
tracker_limit(const struct tracker *tracker, region *damage);

/* What to redraw into buffer 'index' for a frame with 'damage' */
void
tracker_repair(const struct tracker *tracker, size_t index, const region *damage, region *repair);

void
tracker_drawn(struct tracker *tracker, size_t index, const region *damage);

/* Buffer 'index' went to the presentation engine, which ages the others */
void
tracker_presented(struct tracker *tracker, size_t index);

const char *
tracker_name(enum tracker_kind kind);

/* Returns false for a name of no tracker */
bool
tracker_parse(const char *name, enum tracker_kind *kind);

#endif

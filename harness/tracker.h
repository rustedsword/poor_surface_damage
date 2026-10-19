/* SPDX-License-Identifier: MIT */
#ifndef TRACKER_H
#define TRACKER_H

#include <stddef.h>
#include <stdint.h>

#include "region.h"

/*
 * All trackers of the library behind one interface, plus two simple ones to compare them against. Capacities are
 * chosen at runtime: the storage is sized for the largest ones, and the library gets views of it.
 */

constexpr size_t TRACKER_MAX_BUFFERS = 8;
constexpr size_t TRACKER_MAX_FRAMES = 64;
constexpr size_t TRACKER_MAX_CELLS = 128 * 128;

enum tracker_kind {
	/* Redraw everything, every frame */
	TRACKER_WHOLE,
	/* Redraw only the damage of the current frame, which is only correct with a single buffer */
	TRACKER_NAIVE,
	TRACKER_BUFFERS,
	/* Uses buffer ages, counted the way EGL_EXT_buffer_age counts them */
	TRACKER_HISTORY,
	TRACKER_MAP,
};

struct tracker_options {
	enum tracker_kind kind;
	/* Rects per region, frames in the history, cells in the map, and the smallest cell size */
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

/* Command line options for choosing a tracker and its capacities, to use with getopt_long() */
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
	"  --tracker T   whole, naive, buffers, history or map (default: buffers)\n" \
	TRACKER_CAPACITY_USAGE

#define TRACKER_CAPACITY_USAGE \
	"  --rects N     maximum rects per region, up to 64 (default: 4)\n" \
	"  --history N   frames kept by the history tracker, up to 64 (default: 8)\n" \
	"  --cells N     cells of the map, up to 16384 (default: 4096)\n" \
	"  --cell-size WxH  smallest cell size of the map (default: 16x16)\n"

/* Apply the value of one of TRACKER_LONG_OPTIONS. Returns false if the value is invalid. */
bool
tracker_option(struct tracker_options *options, int option, const char *value);

/* Initialize the tracker for 'buffers' empty buffers of 'width' x 'height' */
void
tracker_init(struct tracker *tracker, const struct tracker_options *options, size_t buffers, int32_t width,
	     int32_t height);

/* Reduce 'damage' to the number of rects the tracker allows, as an application with that capacity would */
void
tracker_limit(const struct tracker *tracker, region *damage);

/* Set 'repair' to what has to be redrawn into buffer 'index' for a frame with 'damage' */
void
tracker_repair(const struct tracker *tracker, size_t index, const region *damage, region *repair);

void
tracker_drawn(struct tracker *tracker, size_t index, const region *damage);

/* Call this after buffer 'index' was presented, to age the other buffers */
void
tracker_presented(struct tracker *tracker, size_t index);

const char *
tracker_name(enum tracker_kind kind);

/* Find the tracker called 'name'. Returns false if there is none. */
bool
tracker_parse(const char *name, enum tracker_kind *kind);

#endif

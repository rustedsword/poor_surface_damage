/* SPDX-License-Identifier: MIT */
#ifndef POOR_SURFACE_DAMAGE_H
#define POOR_SURFACE_DAMAGE_H

/*
 * libpoor_surface_damage keeps track of damage: which parts of a surface changed, and which parts each buffer still
 * has to redraw. It never allocates memory: you declare each type with the capacities you need, and put it wherever
 * you like. The macros may evaluate their tracker, region and array arguments more than once.
 *
 *  - poor_damage_region.h: a set of damaged rects with a fixed capacity
 *  - poor_damage_buffers.h: the damage that each buffer of a swapchain is missing
 *  - poor_damage_history.h: the damage of the last few frames, for use with EGL_EXT_buffer_age
 *  - poor_damage_map.h: a low resolution image of the frames that changed each cell, for buffers of any age
 */

#include "poor_damage_region.h"
#include "poor_damage_surface.h"
#include "poor_damage_buffers.h"
#include "poor_damage_history.h"
#include "poor_damage_map.h"

#endif

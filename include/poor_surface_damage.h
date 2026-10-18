/* SPDX-License-Identifier: MIT */
#ifndef POOR_SURFACE_DAMAGE_H
#define POOR_SURFACE_DAMAGE_H

/*
 * libpoor_surface_damage tracks what changes on a surface without allocations: each type is declared with the
 * capacities that the application needs, and lives wherever the application puts it.
 *
 *  - poor_damage_region.h: damage in a few rects,
 *  - poor_damage_buffers.h: what each buffer of a swapchain lacks,
 *  - poor_damage_history.h: the damage of the last frames, for a buffer that holds one of them,
 *  - poor_damage_tiles.h: the last frame that changed each tile of a grid, for a buffer that holds any frame.
 */

#include "poor_damage_region.h"
#include "poor_damage_surface.h"
#include "poor_damage_buffers.h"
#include "poor_damage_history.h"
#include "poor_damage_tiles.h"

#endif

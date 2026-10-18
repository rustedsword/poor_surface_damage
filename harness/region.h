/* SPDX-License-Identifier: MIT */
#ifndef REGION_H
#define REGION_H

#include "poor_surface_damage.h"

/* Regions of the harness hold up to REGION_RECTS rects, and trackers limit them to fewer at run time */
constexpr size_t REGION_RECTS = 64;

poor_damage_region_declare(region, REGION_RECTS);

#endif

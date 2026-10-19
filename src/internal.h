/* SPDX-License-Identifier: MIT */
#ifndef POOR_DAMAGE_INTERNAL_H
#define POOR_DAMAGE_INTERNAL_H

#include "poor_surface_damage.h"

#define EXPORT [[gnu::visibility("default")]]

static inline int64_t lesser(int64_t a, int64_t b)
{
	return a < b ? a : b;
}

static inline int64_t greater(int64_t a, int64_t b)
{
	return a > b ? a : b;
}

#endif

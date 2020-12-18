#ifndef MAP_MATH_H
#define MAP_MATH_H
#include <stdbool.h>
#include "misc.h"

/*TODO: merge with clamp/clampd*/
static inline double map_math_clip(double n, double minValue, double maxValue)
{
    return MIN(MAX(n, minValue), maxValue);
}

static inline uint32_t map_math_size(uintf8_t level)
{
    return ((uint32_t)256) << level;
}

bool map_math_geo_to_pixel(double latitude, double longitude, uintf8_t level, uint32_t *px, uint32_t *py);
void map_math_pixel_to_geo(uint32_t px, uint32_t py, uintf8_t level, double *latitude, double *longitude);
#endif /* MAP_MATH_H */

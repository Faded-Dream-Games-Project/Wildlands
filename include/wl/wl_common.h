/* wl_common.h — shared basics for the whole engine. No dependencies. */
#ifndef WL_COMMON_H
#define WL_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define WL_GAME_NAME    "Wildlands"
#define WL_VERSION_MAJOR 0
#define WL_VERSION_MINOR 1
#define WL_VERSION_PATCH 0
#define WL_VERSION_STRING "0.1.0"

#define WL_WORLD_FORMAT_VERSION 1 /* bump on incompatible chunk/level format change */
#define WL_CONFIG_FORMAT_VERSION 1

#define WL_PATH_MAX 4096
#define WL_NAME_MAX 128 /* world names, mod ids, etc. */

#define WL_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#endif /* WL_COMMON_H */

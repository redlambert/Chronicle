#pragma once

/* Dark Cloud Expanded's town map changes (github.com/Tororoi/DarkCloud-Expanded, IsoPatch/), made
 * natively. Expanded bakes them into the disc image; here they are made from the town's own files
 * each time one loads, right after DataReadLoadFile has rebuilt the retail bytes, so no game data
 * goes into the repo. Each patch checks the vanilla bytes it expects first and is skipped, with a
 * log line, when they don't match.
 *
 * Brownboo (gedit/s04, Expanded's TownSceneBakes, BrownbooCollision and TownCollisionBakes):
 * - the upper crater walls and the houses drawn single-sided, so the edit view sees the town and a
 *   camera inside a house sees through it
 * - the stray corner triangles of the crater rings and floors removed
 * - the camera collision s04g01_v rebuilt: the terrain hull plus obj56 with the iwa01 tunnel rock
 *   swapped for its smooth hull
 * - the three pond rocks added to the player collision s04g01_a
 *
 * Yellow Drops (gedit/s13, Expanded's TownSceneBakes and YellowDropsWestBank):
 * - the water raised from 0 to 4.25: the WATER_SURFACE plane and the suimenn sheet (s1302)
 * - the west bank bulged west by up to 70, rebuilt in s1301: the grid10 and grid11 meshes, the
 *   crown wall and floor (s1301_a), the camera wall (s1301_c), the factory's camera ring
 *   (doumu_c) pulled in, and camera hulls for the two torii-gate pillars
 *
 * Queens (gedit/e03, Expanded's SnakeStatueCollision and QueensCollision):
 * - the snake statue (e03h06) climbable: its player collision is its visual mesh with Expanded's
 *   surgery, its camera hull an open cylinder round the head
 * - the ground parts' player collision and the camera collision rebuilt from the town's structure
 *   meshes plus Expanded's authored walls; the loading-zone triggers kept
 * - the waterfalls' Z-write on */

#include "common.h"

#include <cstddef>

namespace dc::game {
struct EDIT_MAP_INFO;
}

namespace dc::game::expanded {

/**
 * LoadEditMapData has read a town's mapinfo: the parsed values Expanded changes are changed.
 */
void TownMapInfoLoaded(const char *map_name, EDIT_MAP_INFO *info);

/**
 * EditInit has read a town's scene.scn into `scene` (`*size` bytes, `capacity` bytes of room).
 * Patches it in place for the towns Expanded changes and updates `*size`.
 */
void TownSceneLoaded(const char *map_name, u_int *scene, int *size, std::size_t capacity);

/**
 * Test hook (--where): pressing R3 while walking a town writes your position and the floor under
 * you to logs/where.txt.
 */
void TownWhereEnable(bool on);

/**
 * Once a frame from EditLoop, for the --where helper.
 */
void TownWhereStep();

} // namespace dc::game::expanded

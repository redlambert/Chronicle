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
 * - the three pond rocks added to the player collision s04g01_a */

#include "common.h"

#include <cstddef>

namespace dc::game::expanded {

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

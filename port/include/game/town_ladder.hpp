#pragma once

/* Dark Cloud Expanded's ladders for the town allies (github.com/Tororoi/DarkCloud-Expanded,
 * Town/TownLadder.cs), ported as native code. Toan's climb is rigged for his model alone, so an ally
 * at a ladder takes it their own way: Xiao leaps, Goro hops up rung by rung and jumps down, Ruby
 * floats, Osmond dives down and flies up on his helicopter backpack, and Ungaga refuses at both
 * ends. Each move is a list of steps solved when Cross is pressed at the ladder (walk to the ladder,
 * turn, arcs, glides, clips) and played one step a frame from EdMoveChara, which hands the character
 * back when the list ends. Expanded runs the same moves as town scripts it writes for each press. */

#include "camerafollow.hpp"
#include "character.hpp"
#include "edit.hpp"

namespace dc::game::expanded {

/**
 * Cross at a ladder while an ally walks the town: works out the ally's move from the ladder's two ends
 * and starts it. False when the ally has no moveset loaded, so the game's own climb should run.
 */
bool TownLadderStart(CCharacter *chara, int ally, const ED_EVENT_PARAM *param, CCameraFollow *camera);

/**
 * One frame of a ladder move: places the character, plays its clips and follows it with the camera.
 * False when no move is running, and EdMoveChara goes on as usual.
 */
bool TownLadderStep(CCharacter *chara, CCameraFollow *camera);

/**
 * Drops a move in progress (a new map, an event).
 */
void TownLadderCancel();

} // namespace dc::game::expanded

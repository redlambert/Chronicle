#pragma once

/* Dark Cloud Expanded's dungeon and enemy changes (github.com/Tororoi/DarkCloud-Expanded), ported
 * as native code: the minibosses' combat scaling, loot tables and boosted weapon drops, Ungaga's
 * doors for the whole party, and the harder enemy AI option. The game files call in through the
 * small hooks below, each marked "Dark Cloud Expanded" where it sits. */

#include "common.h"
#include "dataalloc_fwd.hpp"

namespace dc::game {

class CCharacter;
class CMonstorUnit;

namespace expanded {

/**
 * Rolls the floor's minibosses once its monsters are placed, or brings back the ones still alive
 * when the party returns from the back floor or goes back to it.
 */
void MinibossesPlaced(CMonstorUnit *unit);

/**
 * Whether the monster in this slot of the floor is a miniboss.
 */
bool IsMiniboss(int monster_no);

/**
 * Scale for a body (damage-taking) sphere the monster's script gives it.
 */
float MinibossBodyRadius(int monster_no, float radius);

/**
 * Scale for an attack sphere the monster's script gives it.
 */
float MinibossAttackRadius(int monster_no, float radius);

/**
 * Damage of a melee hit the monster's script sets up.
 */
int MinibossMelee(int monster_no, int damage);

/**
 * Damage of a projectile the monster's script fires with its own damage.
 */
int MinibossShot(int monster_no, int damage);

/**
 * Distance to the player as the monster's script reads it: a miniboss commits to its attack
 * from further away.
 */
float MinibossDistance(int monster_no, float distance);

/**
 * Speed of a motion the monster's script plays: a miniboss's longer strides walk slower.
 */
float MinibossMotionSpeed(int monster_no, int motion_id, float speed);

/**
 * Each dungeon step: keeps the miniboss list current and applies boosted weapon drops.
 */
void MinibossStep();

/**
 * Makes Ungaga's doors in the dungeon's event script open for the whole party.
 */
void PatchUngagaDoors(int dungeon, char *script, int size);

/**
 * Dark Cloud Enhanced: Ungaga's item throw plays twice as fast, once a party member's dungeon
 * model is loaded.
 */
void UngagaThrowSpeed(int chara, CCharacter &model);

/**
 * Harder enemy AI: once a monster's script is loaded into its kind's slot, raises the revive
 * chance of the native revivers and splices a revive roll into the death of every other monster
 * that has a real get-up motion. The splice's code goes right after the script in the allocator.
 */
void HarderAiScript(const char *model_code, char *script, int size, CDataAlloc2<1> *alloc);

} // namespace expanded

} // namespace dc::game

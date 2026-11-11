#pragma once

/* Dark Cloud Expanded's weapon effects (github.com/Tororoi/DarkCloud-Expanded), ported as native
 * code. Each effect lives here; the game files call in through the small hooks below, each
 * marked "Dark Cloud Expanded" where it sits. */

#include <string_view>

#include "common.h"
#include "itemdata.hpp"

namespace dc::game {

class CCollisionData;
class ClsMes;
struct MONSTOR;

namespace expanded {

/**
 * The weapon in Toan's hand while he is the active character on a dungeon floor, else -1.
 */
int ToanWeapon();

/**
 * Whether the item is in a carried weapon slot or in storage.
 */
bool Owned(int item_no);

// Which weapons carry which effect, their build-up lines included.

/** Shamshir's "Swift Strikes": the five combo swings play a third faster. */
bool SwiftStrikes(int item_no);
/** Small Sword's "Quick Draw": the opening swing skips its wind-up. */
bool QuickDraw(int item_no);
/** Tsukikage's "Moonlit Focus": charge attacks build twice as fast. */
bool MoonlitFocus(int item_no);
/** Dark Cloud's "Guard Crush": Toan's hits pass every guard. */
bool GuardCrush(int item_no);
/** Dusack's "No Fool's Gold": a waking mimic can be hit. */
bool NoFoolsGold(int item_no);
/** Baselard's "Heavy Hand" throw, which the Claymore also has. */
bool HeavyHand(int item_no);
/** Cross Hinder's "Sanctifier": double damage and ABS against undead. */
bool Sanctifier(int item_no);
/** Sun Sword's "Solar Harvest": a kill may drop a Sun attachment. */
bool SolarHarvest(int item_no);
/** The bone key's "Gravedigger" and the Cross Hinder: reviving undead stay down. */
bool KeepsUndeadDown(int item_no);
/** Sax's "Fine Fare": 0 for none, 1 Sax, 2 Dusack, 3 the 7 Branch Sword and its line. */
int FineFareTier(int item_no);
/** Cross Hinder, Big Bang and Sword of Zeus: lock-on reaches twice as far. */
bool LongLockOn(int item_no);

// Hooks called from the game.

/**
 * ToanKey_Play, before it reads the motion: Quick Draw's skip and Swift Strikes' swing speed.
 * Updates time when the motion was moved.
 */
void ToanSwingStep(float &time);

/**
 * Toan's attack for a swing: the held sword's own, or more where an effect raises it.
 */
int ToanAttack(int attack);

/**
 * How much farther than retail the active character's lock-on reaches.
 */
float LockOnRate();

/**
 * Extra charge Toan's held attack gathers each step, on top of retail's 1/60.
 */
float ExtraChargePerStep();

/**
 * Toan's knockback for the hit just set: retail's figures, or the held sword's own throw.
 */
void ToanKickBack(float *origin, float speed, float decay);

/**
 * The hit Toan's swing sets: the radius, after moving the position out along a grown blade.
 */
float ToanHitSphere(float *hit_pos, float radius);

/**
 * Every walking step on a dungeon floor: curses, resistances, the Kitchen Knife's blessing,
 * blade sizes, the Wise Owl Sword's hints and the other ongoing effects.
 */
void DungeonStep();

/**
 * Each step HealingWater runs, with whether the party stands in a healing spring.
 */
void SpringStep(bool in_spring);

/**
 * Whether a weapon effect stops the active character from taking this ailment.
 */
bool AilmentBlocked(int ailment);

/**
 * Defense the held weapon adds to the active character's guard against a hit.
 */
int DefenseBonus();

/**
 * Toan's hit damage against a monster, after retail's formula: the undead bonus.
 */
float ToanDamage(float damage, const MONSTOR &monster);

/**
 * Whether the active character's guard blocks even heavy hits (7th Heaven's "Divine Guard").
 */
bool PerfectGuard();

/**
 * Whether Toan's hit lands through this monster's guard window.
 */
bool PassesGuard(int hit_owner);

/**
 * Whether Toan's hits land on this monster while its script holds it invincible.
 */
bool PassesInvincibility(const MONSTOR &monster);

/**
 * The ABS a kill gives the weapon, after retail's own; also any kill drop the weapon adds.
 */
int KillAbs(int exp, const MONSTOR &monster, const WEAPON_HAVE *weapon);

/**
 * A weapon's description in the menus, rewritten to say what its effects do: the vanilla
 * entry's name line, then the new text. Null where the window, the line or the weapon has no
 * new text, so the bank's own entry is read.
 */
short *MenuDescription(const ClsMes *mes, const short *buff, int line);

/**
 * Circle Amplifier: how many times over every magic circle works. 1, or 2 while a Crysknife
 * or a Magical Hammer is owned, or 3 while both are.
 */
int CircleMultiplier();

/**
 * Circle Amplifier: the powders a "to max" circle drops into the bag, one fewer than the
 * multiplier, while there is room.
 */
void CircleReward(int item_no);

/**
 * Sevenfold Rite: the level a 7 Branch Sword must reach before it can be Status Broken.
 */
constexpr int kSevenfoldLevel = 7;

/**
 * The share of a weapon's stats its Status Break sphere keeps: 0.6, or 0.77 for a 7 Branch Sword.
 */
float StatusBreakFactor(int item_no);

/**
 * The most ABS a weapon may hold: its max, or twice that while Overtraining holds.
 */
int AbsCap(int max_exp);

/**
 * The ABS a weapon starts its new level with: 0, or what it held past its max while
 * Overtraining holds. Called before the level goes up.
 */
int LevelUpCarry(WEAPON_HAVE *weapon);

/**
 * A kill credited to the active character with weapon: the drops weapon effects add.
 */
void OnKill(const MONSTOR &monster, const WEAPON_HAVE *weapon, float *position);

/**
 * The item a chest hands over once Toan opens it: Fine Fare's upgrades.
 */
int ChestItem(int item_no);

/**
 * The value a monster script's random roll gives, or -1 to keep retail's.
 */
int ScriptRandom(int bound);

/**
 * An attachment just placed in a weapon's hole: the Buster Sword's boost.
 */
void OnAttach(WEAPON_HAVE *weapon, int hole);

/**
 * An attachment about to leave a weapon's hole: undoes what OnAttach gave it.
 */
void OnDetach(WEAPON_HAVE *weapon, int hole);

/**
 * Shows text in the dungeon's message window for frames steps.
 */
void ShowDungeonMessage(std::string_view text, int frames);

/**
 * A new floor begins: clears what each effect remembers of the last.
 */
void OnFloorStart();

} // namespace expanded

} // namespace dc::game

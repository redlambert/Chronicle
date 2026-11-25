#pragma once

// Dark Cloud Enhanced's quests on the dungeon side (quest/enhanced_quests.cpp): the town half of each
// is a quest of the port's quest framework, kept in quests.json; these are the hooks the dungeon,
// the fishing and the item code call so the quests see what happens there.

#include <string>
#include <vector>

namespace dc::game {
struct MONSTOR;
}

namespace dc::quest {

/**
 * A monster has just been defeated (its drops being decided): counts it for the monster hunts.
 */
void EnhancedMonsterDown(const dc::game::MONSTOR &monster);

/**
 * A fish has been landed in town (0 Norune, 1 Matataki, 2 Queens, 3 Muska Lacka): kind as the
 * fishing tables number it, size in tenths of a centimetre as the catch message shows it.
 */
void EnhancedFishCaught(int town, int kind, int size);

/**
 * A new dungeon floor: forgets the last floor's challenge.
 */
void EnhancedFloorStart();

/**
 * One dungeon step: Samba's and the Mayor's floor challenges.
 */
void EnhancedDungeonStep();

/**
 * The item a fetch quest hides in the first small chest of dungeon's back floor, or -1 for none.
 * Rolled once per back floor, as Enhanced rolls it.
 */
int EnhancedBackFloorItem(int dungeon);

/**
 * The most life a Fruit of Eden raises a character to: limit, or 250 once the Mayor's bonus quests
 * are open.
 */
int EnhancedEdenLimit(int limit);

/**
 * Whether the healing fountains are off: while Samba's challenge runs on Moon Sea floor 7's back
 * floor, as Enhanced turns them off there.
 */
bool EnhancedFountainsOff();

/**
 * Whether the Mayor's bonus quests are open (Pickle has seen the full collection). Enhanced's
 * Fairy King sells the Crystal Eyeball from then on.
 */
bool EnhancedMayorOpen();

/**
 * The most water (ten to a drop) a Gourd may be used below: 90, or 110 once the Mayor's bonus
 * quests are open, so the gauge reaches 12 drops.
 */
float EnhancedGourdLimit();

/**
 * Whether Sam's special reward has been given: the fish of the Queens Sea can be seen while fishing.
 */
bool EnhancedQueensFishSeen();

/**
 * One quest under way, as the Journal's quest tracker lists it.
 */
struct TrackedQuest {
    std::string giver; /**< Who gave it, and their town. */
    std::string task;  /**< What is left to do. */
};

/**
 * Dark Cloud Expanded's quest tracker: every quest under way, or done and waiting for its reward.
 */
std::vector<TrackedQuest> EnhancedTrackedQuests();

} // namespace dc::quest

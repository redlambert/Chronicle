#pragma once

/* Dark Cloud Enhanced's town features (github.com/Gundorada-Workshop/DarkCloud-Enhanced), ported
 * as native code: walking a town as any ally, with the villagers' lines for each ally, and the
 * logging that the next Enhanced features need. The game files call in through the small hooks
 * below, each marked "Dark Cloud Enhanced" where it sits. */

#include <string>

#include "common.h"

namespace dc::game::expanded {

/**
 * One villager's lines for one ally: the first time they talk, then the next, by turns.
 */
struct AllyTalk {
    int         map_no;     /**< The town, as MapNo numbers it. */
    int         ally;       /**< 1 Xiao, 2 Goro, 3 Ruby, 4 Ungaga, 5 Osmond. */
    const char *model_code; /**< The two digits of the villager's town model ("01" for p01a); empty for any. */
    int         villager;   /**< The villager's number, where the town goes by it (Brownboo, Yellow Drops); else -1. */
    const char *first;
    const char *second;
};

extern const AllyTalk kAllyTalk[];
extern const int      kAllyTalkCount;

/**
 * The character walking the town: 0 for Toan, else the ally's party number.
 */
int TownAlly();

/**
 * The town party menu picked a character: they take over once the player walks again.
 */
void TownAllySelect(int chara);

/**
 * EditInit has loaded Toan for a new map: whoever walked the last one is gone.
 */
void TownAllyMapLoaded();

/**
 * Once a frame from EditLoop: puts the picked character in, while the player walks.
 */
void TownAllyStep();

/**
 * An event (a cutscene, not a talk) has just started: Toan comes back for it, as Enhanced has him.
 */
void TownAllyEventStarted();

/**
 * Whether the map's event points are switched off for the ally walking it, as Enhanced has them.
 */
bool TownAllySkipsEventPoints();

/**
 * How far above the feet the first-person camera sits: lower for Xiao.
 */
float TownEyeHeight();

/**
 * EdLoadMainChara has read a character file: Enhanced's motion fixes for the allies' town files
 * go into its config before it is read, and the config is logged.
 */
void TownCharaPackLoaded(const char *path, u_int *pack);

/**
 * The ally's line for the villager the player talks to, or nullptr when Toan walks or the
 * villager has none. Each call turns the villager to its other line for next time.
 */
const char *TownAllyTalk(int map_no, int villager, const std::string &model);

// Logging for the Enhanced features still to come (Ungaga's throw, the Fog Gate). Everything goes
// to the logs folder beside the saves.

/**
 * The dungeon's party file for the character has been read: its config files are saved.
 */
void LogDungeonChara(int chara, u_int *pack);

/**
 * The dungeon's system script file has been read: it is saved.
 */
void LogDungeonScript(int dungeon, const void *data, int size);

/**
 * Once a dungeon frame: logs each system script the dungeon runs, and for how many frames.
 */
void LogDungeonTask();

/**
 * A town event has started.
 */
void LogTownEvent(int event_no, bool system);

/**
 * Once a town frame: logs when the event that started ends, and its length.
 */
void LogTownEventStep();

} // namespace dc::game::expanded

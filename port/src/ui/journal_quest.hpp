#pragma once

#include <string>
#include <utility>
#include <vector>

/* Dark Cloud Expanded's quest tracker (Quests/QuestTracker.cs), which lived in the mod's window, as a
 * page of the Journal: every quest under way, read again each time the page opens. */

namespace dc::ui::journal_quest {

/**
 * Reads the quests under way and starts the list at its top, when the page opens from the Journal.
 */
void Open();

/**
 * Handles gamepad input while the quest tracker is open. Returns true when the player backs out to
 * the Journal menu.
 */
bool HandleKey();

/**
 * Draws the quest tracker page with the UI framework.
 */
void Draw();

/**
 * Appends the quest tracker's localization keys and their default English strings to out.
 */
void AppendStrings(std::vector<std::pair<std::string, std::string>> &out);

} // namespace dc::ui::journal_quest

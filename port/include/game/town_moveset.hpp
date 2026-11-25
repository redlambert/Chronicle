#pragma once

/* Dark Cloud Expanded's town movesets for the allies (github.com/Tororoi/DarkCloud-Expanded,
 * IsoPatch/TownModelBakes.cs and Formats/MotSplice.cs), rebuilt natively. Expanded assembles each
 * ally's town model when it patches the disc image: it copies motion clips from other models of the
 * same character into the model's own motion files, joint by joint, then holds, offsets, reverses
 * or freezes some of the copied windows and rewrites the model's KEY table to the town's slots. Here
 * the same assembly runs on the pack in memory the first time the ally's model is loaded, from the
 * player's own game data.
 *
 * The town's motion slots: 0 idle, 1 run, 2 walk, 3 push door, 4 pull door, 5 item get, 6 item get
 * loop, 7 damage (the ladder refusal), 8 fall, 9 land, 10 double door, then the ladder slots from 11.
 *
 * Nothing here touches the game: packs come in and go out as bytes, and each source model is read
 * through the reader the caller passes. */

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dc::game::moveset {

using Bytes = std::vector<std::uint8_t>;

/**
 * Reads a whole file of the game's data ("dun/mainchara/c10b.chr"); false when there is none.
 */
using Reader = std::function<bool(std::string_view path, Bytes &out)>;

/**
 * The model Expanded's moveset for an ally is built on (1 Xiao to 5 Osmond), or nullptr for Toan.
 */
const char *BaseModel(int ally);

/**
 * Builds the ally's town model from its base model's bytes: every copied clip, every window
 * change, the shadow and helicopter parts Expanded adds, and the rewritten KEY table. Returns false,
 * with the reason in log, when anything it needs is missing or not laid out as expected; out is
 * then left empty and the base model should be used as it is.
 */
bool Build(int ally, const Bytes &base, const Reader &read, Bytes &out, std::vector<std::string> &log);

} // namespace dc::game::moveset

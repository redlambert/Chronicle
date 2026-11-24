#pragma once

/* Dark Cloud Expanded's fishing changes (github.com/Tororoi/DarkCloud-Expanded, Fishing/Fishing.cs),
 * ported as native code: the Mardan swords rework, Arise Mardan's bigger fish, fish steering at
 * Matataki Waterfall and East Harbor, and the fishing records feeding Arise Mardan's Max Magic. The
 * game files call in through the small hooks below, each marked "Dark Cloud Expanded" where it sits. */

#include "common.h"

namespace dc::game {

class CFish;

namespace expanded {

/**
 * A fishing spot's fish are about to load (FishingLoadFish): looks for a Mardan sword in the bag or
 * storage and remembers the spot (0 Norune, 1 Matataki's pond, 2 its waterfall, 3 East Harbor, 4 the
 * Oasis).
 */
void FishingSessionStart(int spot);

/**
 * The kind a fish loads as: with Mardan Twei or Arise Mardan, a fish that is not a Garayan has a
 * second chance, as rare as the spot's own, of turning into one.
 */
int FishKind(int kind);

/**
 * A fish has loaded: the Mardan swords raise the Fishing Points of all but the Garayans, and Arise
 * Mardan scales its size up towards twice the kind's largest.
 */
void FishLoaded(CFish *fish);

/**
 * Each fishing step, with the angler's position: turns fish toward the angler, every 10 seconds at
 * Matataki Waterfall and East Harbor, and the Garayans and Umadakaras more often with a Mardan
 * sword, by how much they like the bait on the hook.
 */
void FishingSteer(const float *angler);

/**
 * Keeps one best catch per species in the fishing records, largest first, and gives Arise Mardan
 * its Max Magic for them. Called after each catch and when a save is loaded or a game starts.
 */
void FishRecordsUpdate();

} // namespace expanded

} // namespace dc::game

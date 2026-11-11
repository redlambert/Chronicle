#pragma once

/* Dark Cloud Expanded's fixes to the game's English text (github.com/Tororoi/DarkCloud-Expanded,
 * IsoPatch/MesTextFixes.cs), applied as each message is read instead of baked into the disc. */

namespace dc::game::expanded {

/**
 * The message text starting at text with Expanded's spelling and spacing fixes, or text itself
 * where nothing changes or the message cannot be rewritten safely. Valid until the bank it came
 * from is reloaded.
 */
short *FixText(short *text);

} // namespace dc::game::expanded

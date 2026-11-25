#pragma once

/* Dark Cloud Expanded's weapon visuals (github.com/Tororoi/DarkCloud-Expanded), ported as native
 * code: the game's own effect models played where a weapon effect wants them, the scene's light
 * washed out or dimmed, and tints laid over a weapon, the player or an enemy. The weapon effects in
 * expanded_weapons.cpp call these; the game files call the few hooks at the end. */

namespace dc::game::expanded::fx {

/**
 * The game's effect models the weapon effects borrow, each loaded from the game data the first
 * time it is played and kept for the session.
 */
enum class Prop {
    Explosion, // dun/effect/explosion.chr: the bomb's blast (Big Bang)
    Lightning, // gedit/s99/chara/lightning.chr: the Divine Beast Cave cutscene's bolt (Sword of Zeus)
    Smoke,     // gedit/e03/chara/e228ex.chr: Queens' smoke puff (the summons)
    Stars,     // gedit/s04/chara/e114ex.chr: the dizzy stars (Confuse)
    Beam,      // dun/monstor/c17_beem_s.chr: the Dark Genie's small beam (Babel's Spear)
    Sparkle,   // gedit/s78/chara/e508_ex.chr: a whirl of sparkles (Hercules' Wrath)
    Count,
};

/**
 * Plays clip (the pack's KEY line) of a prop at a point, scaled and turned yaw about the vertical,
 * at rate times its own speed. A clip that does not loop ends by itself. Returns a handle, or -1
 * when the model is missing or every copy of it is playing.
 */
int Play(Prop prop, int clip, const float *at, float scale, float yaw, bool loop = false, float rate = 1.0f);

/**
 * Moves a playing prop.
 */
void Move(int handle, const float *at);

/**
 * Changes a playing prop's clip.
 */
void Clip(int handle, int clip, bool loop);

/**
 * Resizes a playing prop.
 */
void Resize(int handle, float scale);

/**
 * Fades a playing prop out, ending it.
 */
void Fade(int handle);

/**
 * Ends a playing prop at once.
 */
void End(int handle);

/**
 * Whether a handle is still playing.
 */
bool Live(int handle);

/**
 * The scene washes out to light (RGB, 0 to 255) with the fog pulled in to the fog colour, then
 * eases back to the floor's own light over ease steps.
 */
void Flash(const float *light, const float *fog, int ease_steps);

/**
 * Holds the scene dimmed by level (0 = the floor's own light, 1 = nearly dark) this step. A dim
 * that is not held again eases back over a few steps.
 */
void Dim(float level);

/**
 * Adds rgb (0 to 255 a channel) to the held weapon's light this step.
 */
void WeaponTint(const float *rgb);

/**
 * Adds rgb to the active character's light this step.
 */
void PlayerTint(const float *rgb);

/**
 * Adds rgb to enemy m's light, fading out over steps.
 */
void MonsterTint(int m, const float *rgb, int steps);

// ── Hooks ─────────────────────────────────────────────────────────────────────────────────────

/**
 * Every dungeon step: ages what plays and lays the tints on. From expanded::DungeonStep.
 */
void Step();

/**
 * Draws the props. From DunMainDraw with the other effects.
 */
void Draw();

/**
 * Lays the flash and the dim over the floor's light. From DunMainDraw once the light is set.
 */
void SceneLight();

/**
 * Around the held weapon's draw: begin true before it, false after.
 */
void WeaponDraw(bool begin);

/**
 * A new floor: everything stops.
 */
void Reset();

} // namespace dc::game::expanded::fx

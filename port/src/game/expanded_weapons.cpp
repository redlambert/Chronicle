#include "expanded_weapons.hpp"
#include "expanded_enemies.hpp"
#include "expanded_effects.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <format>
#include <string>
#include <type_traits>
#include <vector>

#include "btactstatus.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "data/data_read.hpp"
#include "dataread.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "camerafollow.hpp"
#include "dungeonmap.hpp"
#include "healeffect.hpp"
#include "dungeonparts.hpp"
#include "dun/gameloop.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "hitvalue.hpp"
#include "itembombeffect.hpp"
#include "mainselect.hpp"
#include "menu_inventory.hpp"
#include "mds.hpp"
#include "menu_save.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "motionmodel.hpp"
#include "randomitem.hpp"
#include "savedata.hpp"
#include "shot_effect_pack.hpp"
#include "shot_effect.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "stockitem.hpp"
#include "text/gametext.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "quest/enhanced.hpp"
#include "quest/framework.hpp"

namespace dc::game {

extern CSHOT *NowShotData; // dun/gameloop.cpp: the player's pellets

} // namespace dc::game

namespace dc::game::expanded {

namespace {

constexpr int kStepsPerSecond = 60;

bool Is(int item_no, std::initializer_list<int> items) {
    return std::ranges::find(items, item_no) != items.end();
}

// ── Super Steve "Sphere Inheritance" ────────────────────────────────────────────────────────

// Xiao's Super Steve carries the effect of the weapon its SynthSphere was made from: every check
// below that asks what a character holds goes through EffectOf, which answers with the sphere's
// weapon while she holds Super Steve. Toan's effects that work through his swings stay his (their
// Xiao versions in Expanded are new shots of their own); the ones that work on kills, hits,
// guards, ailments, chests and lock-on carry over.

/**
 * The weapon whose effect the active character carries.
 */
int ActiveEffect() {
    return UserStatus != nullptr ? EffectOf(UserStatus->cur_chara) : -1;
}

/**
 * The weapon whose effect a weapon record carries: its sphere's for Super Steve, else its own.
 */
int EffectItem(const WEAPON_HAVE *weapon) {
    const int sphere = SphereWeapon(weapon);
    return sphere > 0 ? sphere : weapon->item_no;
}

// Super Steve's pellets with a Baselard or Claymore sphere: the shove comes from this far behind.
constexpr float kSphereKickBack = 4.0f;

struct Pockle;
bool IsPockle(const MONSTOR &monster);
void PockleStep();
void PockleReset();
bool CatLaunch(const float *world, const float *vector, int damage);
void CatStep();
void CatDraw();
void CatReset();

// ── Dungeon messages ────────────────────────────────────────────────────────────────────────

// Message numbers the mod's own lines take in the dungeon window, past every bank's own.
constexpr int kMessageBase = 30000;

text::GameTextFile &Messages() {
    static text::GameTextFile messages;
    return messages;
}

int g_next_message = kMessageBase;

// ── Menu descriptions ───────────────────────────────────────────────────────────────────────

/**
 * A weapon's new description, as it reads in the menus.
 */
struct WeaponDescription {
    int         item_no;
    const char *text;
};

constexpr WeaponDescription kDescriptions[] = {
    {ITEM_WEAPON_DIVINEBEASTTITLE, "Charged shot becomes a\ncat that pounces on\nyour target."},
    {ITEM_WEAPON_SUPER_STEVE, "Gains the effect of\nthe weapon whose\nSynthSphere it holds."},
    {ITEM_WEAPON_POCKLEKUL, "Guard 1 sec: a pockle\nfights at your side."},
    {ITEM_WEAPON_BASELARD, "Heavy Hand: every hit\nknocks enemies far\nback."},
    {ITEM_WEAPON_GLADIUS, "Jacket Hunter: Master\nJackets give 4x ABS."},
    {ITEM_WEAPON_WISE_OWL_SWORD, "In Wise Owl Forest,\nwarns you when a key\nmonster is near."},
    {ITEM_WEAPON_CRYSKNIFE, "Thrown gems do 2x.\nOwned: magic circles\nwork twice as hard."},
    {ITEM_WEAPON_BUSTER_SWORD, "Buster Boost: makes\nAnti attachments\nstronger."},
    {ITEM_WEAPON_KITCHEN_KNIFE, "Enter a spring: bigger\nblade, 2x attack and\nfull WHP for 60 sec."},
    {ITEM_WEAPON_TSUKIKAGE, "Quick first swing.\nMoonlit Focus: charge\nattacks fill faster."},
    {ITEM_WEAPON_SUN_SWORD, "Kills may drop a Sun.\nGuard 1.5 sec; the next\nswing blinds enemies."},
    {ITEM_WEAPON_MACHO_SWORD, "Owned: every weapon\nabsorbs ABS to 2x max.\nExtra carries over."},
    {ITEM_WEAPON_SHAMSHIR, "Swift Strikes:\na faster combo."},
    {ITEM_WEAPON_DARK_CLOUD, "Guard Crush: cuts\nthrough any enemy's\nguard."},
    {ITEM_WEAPON_BRAVE_ARK, "Blocks Freeze, Poison,\nCurse and Goo. Hits\nmimics at any time."},
    {ITEM_WEAPON_AGA_S_SWORD, "Defensive Legacy:\n+15 defense."},
    {ITEM_WEAPON_EVILCISE, "Cursed each floor; no\nother ailments stick.\nCleansed: 1 HP, poison."},
    {ITEM_WEAPON_SMALL_SWORD, "Quick Draw: the first\nswing comes out fast."},
    {ITEM_WEAPON_CLAYMORE, "Greatsword: huge blade\nwith long reach. Hits\nknock enemies back."},
    {ITEM_WEAPON_MANEATER, "Cursed each floor. Low\nWHP: trades 1 HP for\n1 WHP each second."},
    {ITEM_WEAPON_BONE_RAPIER, "Locked doors open\nwithout their key;\nundead stay down."},
    {ITEM_WEAPON_SAX, "Fine Fare: small\nchests hold better\nitems."},
    {ITEM_WEAPON_7BRANCH_SWORD, "Breaks only at +7 or\nmore, into a sphere\nwith 77% of stats."},
    {ITEM_WEAPON_DUSACK, "Faster combo, better\nchests. Hits mimics\nat any time."},
    {ITEM_WEAPON_CROSS_HINDER, "Sanctifier: 2x damage\nand ABS vs undead,\nwho stay down."},
    {ITEM_WEAPON_7THHEAVEN, "Holy sword. Guarding\nblocks all attacks;\ncuts through guards."},
    {ITEM_WEAPON_FLAMINGO, "Locks on from twice as\nfar. Owned: fish notice\nbait from farther."},
    {ITEM_WEAPON_STEEL_SLINGSHOT, "Below a tenth of its\nWHP, each shot wears\nit half as much."},
    {ITEM_WEAPON_INFERNO, "Hits harder the less\nlife and water Goro\nhas left."},
    {ITEM_WEAPON_PARTISAN, "Quick Swing: a combo\na third faster."},
    {ITEM_WEAPON_DE_SANGA, "Vampire: every kill\nheals it 5 WHP."},
    {ITEM_WEAPON_JAVELIN, "Marine: sea enemies\nhave no defense and\ngive 2x ABS."},
    {ITEM_WEAPON_SCORPION, "Poison cures yours and\nsoaks ABS. Owned: fish\nlove Poisonous Apples."},
    {ITEM_WEAPON_SNAIL, "Slime Trail: hits may\nleave enemies stuck."},
    {ITEM_WEAPON_SKUNK, "Its flamethrower\nreaches twice as far."},
    {ITEM_WEAPON_DRAGON_S_Y, "Hold for a 1.5x blast.\nLock-on reaches twice\nas far, moves faster."},
    {ITEM_WEAPON_DOUBLE_IMPACT, "Fires two pellets at\n3/4 power each. Hits\nricochet onward."},
    {ITEM_WEAPON_HARDSHOOTER, "Ricochet: a hit sends\na pellet on to the\nnext enemy."},
    {ITEM_WEAPON_MATADOR, "Hold 1 sec: a 1.5x shot\nthat breaks guards and\nknocks enemies back."},
    {ITEM_WEAPON_STAR_BREAKER, "Shooting Stars: kills\nmay drop an empty\nSynthSphere."},
    {ITEM_WEAPON_HALBERT, "Tornado: its charge\nshot flies faster\nand hits wider."},
    {ITEM_WEAPON_TALL_HAMMER, "Each hit shrinks the\nenemy, down to 30%\nof its size."},
    {ITEM_WEAPON_BANDIT_SLINGSHOT, "Steal Shot: a steal\ntakes the enemy's\nshot, at 2x damage."},
    {ITEM_WEAPON_BANDIT_S_RING, "Steal Shot: a steal\nmakes quick shots the\nenemy's, at 2x damage."},
    {ITEM_WEAPON_MOBIUS_RING, "Hold a full charge:\ndamage grows 1.5x and\nthe orbs grow with it."},
    {ITEM_WEAPON_SUPERNOVA, "Hits may stop, poison,\nanger or slime the\nenemy at random."},
    {ITEM_WEAPON_SECRET_ARMLET, "Owned: bad magic\ncircles turn out as\ngood ones."},
    {ITEM_WEAPON_BLESSING_GUN, "Owned: bait is never\nlost while fishing."},
    {ITEM_WEAPON_CACTUS, "Hits refill water.\nGuard: a giant cactus\nstands 10 sec, pricks."},
    {ITEM_WEAPON_HEAVEN_S_CLOUD, "Typhoon: hold the\nwhirlwind charge to\ngrow the blade to 3x."},
    {ITEM_WEAPON_CHRONICLE_SWORD, "Hits also strike every\nenemy nearby, harder\nthe closer they stand."},
    {ITEM_WEAPON_BIG_BANG, "Whirlwind blasts all\naround. Guard 1.5 sec;\nthe next swing flashes."},
    {ITEM_WEAPON_SWORD_OF_ZEUS, "Guard 1.5 sec; swings\ncall lightning. A long\ncharge: a lunge bolt."},
    {ITEM_WEAPON_MIRAGE, "Guard briefly: a decoy\nlures foes for 12 sec;\nones you hit turn back."},
    {ITEM_WEAPON_HERCULES_WRATH, "Guard: an 18 sec decoy.\n5 sec more: a swing\nblasts all around it."},
    {ITEM_WEAPON_BONE_SLINGSHOT, "Locked doors open\nwithout their key;\nundead stay down."},
    {ITEM_WEAPON_ATLAMILLIA_SWORD, "Owned: a weapon that\nbreaks leaves a Synth-\nSphere in a chest."},
    {ITEM_WEAPON_ANGEL_SHOOTER, "Guarding, Heal works\nevery second."},
    {ITEM_WEAPON_ANGE_GEAR, "Mends the party. Guard:\na giant slingshot\ncatches, returns shots."},
    {ITEM_WEAPON_BABEL_S_SPEAR, "Hits may confuse. Guard\n5 sec: a giant spear\nconfuses the floor."},
    {ITEM_WEAPON_TERRA_SWORD, "Guard 5 sec, lock on:\na boulder drops on the\nenemy, then stays."},
    {ITEM_WEAPON_CHRONICLE2, "Owned: more big chests,\nPowerup Powder stays,\nclown gives weapons."},
    {ITEM_WEAPON_FROZEN_TUNA, "Worn WHP later heals\nGoro. Hits may freeze\neveryone, Goro too."},
};

/**
 * A rewritten description entry, and the bank it was built from.
 */
struct BuiltDescription {
    const short     *buff = nullptr;
    std::vector<s16> codes;
};

std::map<int, BuiltDescription> g_descriptions;

/**
 * A message's text in a bank laid out as ClsMes::SetBuff reads one; null where it has none.
 */
const short *BankMessage(const short *buff, int line) {
    const int    count = buff[0];
    const short *body = &buff[1];

    for (int i = 0; i < count; i++) {
        if (line == body[i * 2 + 1]) {
            return &body[count] + body[i * 2 + 2];
        }
    }

    return nullptr;
}

// ── Blade sizes ─────────────────────────────────────────────────────────────────────────────

/**
 * A weapon model frame whose local matrix is scaled, with the matrix it had before.
 */
struct ScaledBlade {
    CFrame *frame = nullptr;
    int     item_no = -1;
    float   original[3][4] = {};
    float   factor = 1.0f;
};

ScaledBlade g_blade;

/**
 * The frame of the held weapon's model that holds its blade: "w" and its two-digit build-up
 * position, as its model file is named (the Kitchen Knife's c01w08 holds w08).
 */
CFrame *BladeFrame(int item_no) {
    if (NowWeapon == nullptr || NowWeapon->frame == nullptr) {
        return nullptr;
    }

    const WEAPON_DATA *data = GetWeaponData(item_no);

    if (data == nullptr) {
        return nullptr;
    }

    char name[8];
    std::snprintf(name, sizeof(name), "w%02d", data->chain_pos);

    return NowWeapon->frame->SearchFrame(name);
}

/**
 * Whether the frame still holds the size this file last gave it.
 */
bool HoldsScale(const CFrame *frame) {
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            if (std::fabs(frame->local[row][column] - g_blade.original[row][column] * g_blade.factor) > 0.0001f) {
                return false;
            }
        }
    }

    return true;
}

/**
 * Grows the held weapon's blade, its hit bone included, to factor times its size; 1 puts it back.
 */
void ScaleBlade(int item_no, float factor) {
    CFrame *frame = BladeFrame(item_no);

    if (frame == nullptr) {
        g_blade = {};
        return;
    }

    // A new model, another weapon, or one rebuilt at the same place: what the frame holds now is
    // its own size, so it is taken as the original.
    if (frame != g_blade.frame || item_no != g_blade.item_no || !HoldsScale(frame)) {
        g_blade = {};
        g_blade.frame = frame;
        g_blade.item_no = item_no;

        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                g_blade.original[row][column] = frame->local[row][column];
            }
        }
    }

    if (factor == g_blade.factor) {
        return;
    }

    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            frame->local[row][column] = g_blade.original[row][column] * factor;
        }
    }

    g_blade.factor = factor;
    frame->world_valid = false;
}

// ── Claymore "Greatsword" ───────────────────────────────────────────────────────────────────

constexpr float kClaymoreScale = 1.8f;
constexpr float kClaymoreHitBone = 10.384f; // the stock blade's hit bone, units from the hand

// ── Kitchen Knife "Spring's Blessing" ───────────────────────────────────────────────────────

constexpr float kKnifeBlessedLength = 12.0f; // the stock blade reaches 4.187
constexpr float kKnifeStockLength = 4.187f;
constexpr int   kKnifeBlessSteps = 60 * kStepsPerSecond;

int  g_knife_bless_steps = 0;
bool g_knife_was_in_spring = false;

// ── Curses: Evilcise "Jealous Soul" and Maneater "Blood Price" ──────────────────────────────

struct CurseState {
    int  item_no = -1;     // the sword that put it on
    bool resolved = false; // the curse was cleansed on this floor
    bool was_knocked_out = false;
    int  floor = -1;
    int  drain_steps = 0;
};

CurseState g_curse;
CurseState g_curse_xiao; // Super Steve's, from an Evilcise or Maneater sphere

// ── Wise Owl Sword "Wise Owl Always Knows" ──────────────────────────────────────────────────

constexpr float kWiseOwlRange = 500.0f;
constexpr int   kWiseOwlDungeon = 1;

int  g_owl_nearest = -1;
bool g_owl_out_of_range = true;

// ── Floor tracking ──────────────────────────────────────────────────────────────────────────

int g_floor = -1;
int g_map = -1;

void Cure(int chara, int ailment) {
    UserStatus->ailments[chara] &= ~ailment;
}

void Curse(int chara) {
    UserStatus->ailments[chara] |= AILMENT_CURSE;
}

/**
 * The curse a held Evilcise or Maneater keeps on chara: Toan, or Xiao by Super Steve's sphere.
 */
void CurseStep(int chara, int held, CurseState &curse) {
    const bool cursing = held == ITEM_WEAPON_EVILCISE || held == ITEM_WEAPON_MANEATER;

    if (!cursing) {
        // Put away: the sword takes its curse with it.
        if (curse.item_no != -1) {
            Cure(chara, AILMENT_CURSE);
            curse = {};
        }

        return;
    }

    const bool knocked_out = (UserStatus->ailments[chara] & AILMENT_KNOCKED_OUT) != 0;

    if (curse.item_no != held) {
        curse = {};
        curse.item_no = held;
        curse.floor = UserStatus->cur_floor;
        curse.was_knocked_out = knocked_out;

        if (!knocked_out) {
            Curse(chara);
        }

        return;
    }

    if (curse.floor != UserStatus->cur_floor) {
        curse.floor = UserStatus->cur_floor;
        curse.resolved = false;
        curse.was_knocked_out = false;
        Curse(chara);
    }

    if (knocked_out) {
        curse.was_knocked_out = true;
    } else if (curse.was_knocked_out) {
        curse.was_knocked_out = false;

        if (!curse.resolved) {
            Curse(chara);
        }
    } else if (!curse.resolved) {
        const bool cursed = (UserStatus->ailments[chara] & AILMENT_CURSE) != 0;

        if (!cursed) {
            curse.resolved = true;

            // Jealous Soul: breaking its curse leaves Toan poisoned on 1 HP. Blood Price asks nothing.
            if (held == ITEM_WEAPON_EVILCISE) {
                UserStatus->ailments[chara] |= AILMENT_POISON;
                UserStatus->hp[chara] = 1;
            }
        } else if (held == ITEM_WEAPON_EVILCISE) {
            // Jealous Soul: while cursed, every other ailment slides off.
            UserStatus->ailments[chara] &= AILMENT_KNOCKED_OUT | AILMENT_CURSE;
        }
    }

    // Blood Price: a worn-down Maneater feeds on Toan, 1 HP for 1 WHP a second, never below 1 HP.
    if (held == ITEM_WEAPON_MANEATER && NowWeaponHave != nullptr) {
        curse.drain_steps++;

        if (curse.drain_steps >= kStepsPerSecond) {
            curse.drain_steps = 0;
            const float durability = NowWeaponHave->durability_f;
            const float max_durability = NowWeaponHave->durability;

            if (!knocked_out && UserStatus->hp[chara] > 1 && max_durability > 0.0f && durability > 0.0f && durability <= 0.2f * max_durability) {
                UserStatus->hp[chara]--;
                NowWeaponHave->durability_f = std::min(max_durability, durability + 1.0f);
            }
        }
    }
}

/**
 * Wise Owl Always Knows: in Wise Owl Forest, while a Wise Owl Sword is owned, a line tells of
 * the nearest monster carrying one of the forest's keys.
 */
void WiseOwlStep() {
    if (selectMapNo != kWiseOwlDungeon || NowMonstorUnit == nullptr || !Owned(ITEM_WEAPON_WISE_OWL_SWORD)) {
        return;
    }

    int   nearest = -1;
    float nearest_distance = kWiseOwlRange;

    for (int i = 0; i < 16; i++) {
        const MONSTOR &monster = NowMonstorUnit->monster[i];

        if (monster.state != 2 || monster.hp <= 0) {
            continue;
        }

        if (!Is(monster.drop_item, {ITEM_SHINY_STONE, ITEM_RED_BERRY, ITEM_POINTY_CHESTNUT})) {
            continue;
        }

        if (monster.player_distance > 0.0f && monster.player_distance < nearest_distance) {
            nearest_distance = monster.player_distance;
            nearest = i;
        }
    }

    if (nearest == -1) {
        g_owl_out_of_range = true;
        g_owl_nearest = -1;
        return;
    }

    const bool changed = nearest != g_owl_nearest || g_owl_out_of_range;
    g_owl_nearest = nearest;
    g_owl_out_of_range = false;

    if (!changed) {
        return;
    }

    switch (NowMonstorUnit->monster[nearest].drop_item) {
        case ITEM_SHINY_STONE:
            ShowDungeonMessage("Wise Owl senses a shiny stone nearby...", 3 * kStepsPerSecond);
            break;
        case ITEM_RED_BERRY:
            ShowDungeonMessage("Wise Owl senses a red berry nearby...", 3 * kStepsPerSecond);
            break;
        case ITEM_POINTY_CHESTNUT:
            ShowDungeonMessage("Wise Owl senses a pointy chestnut nearby...", 3 * kStepsPerSecond);
            break;
        default:
            break;
    }
}

/**
 * The Kitchen Knife's blessing running down, and the blade sizes of the held sword.
 */
// ── Weapon HP an ability spends ─────────────────────────────────────────────────────────────

// A bolt, a blast or a flash is paid for through the game's own wear, as if the sword had landed
// that many swings: Endurance, Durable, Fragile, the warnings, Auto Repair Powder and the break
// all stay the game's. It is paid on the next walking step, between frames, so a break never
// lands in the middle of the effect that caused it.
constexpr float kSwingWhp = 1.5f;

float g_whp_bill = 0.0f;
int   g_whp_item = -1;

void WhpStep() {
    if (g_whp_bill <= 0.0f || UserStatus == nullptr || NowWeaponHave == nullptr) {
        return;
    }

    // The blade that was spent is gone from the hand: the bill goes with it.
    if (NowWeaponHave->item_no == g_whp_item) {
        SwordDmgCheck1(g_whp_bill, 0);
    }

    g_whp_bill = 0.0f;
    g_whp_item = -1;
}

// Walking steps counted since the game started, for effects that act once a step.
int g_step_count = 0;

// ── The falloff blast ───────────────────────────────────────────────────────────────────────

// One hit on each living enemy in reach, centred on its nearest hit sphere so it is that enemy's
// alone: its weapon attack times the step its distance from the blast falls in (to the nearest
// sphere's edge), and a kick away from the blast. Elementless. Big Bang's whirlwind and landing
// plant it; so will the Sword of Zeus's bolt and Hercules' Wrath.
struct BlastStep {
    float radius;
    float times;
};

constexpr BlastStep kBlastSteps[] = {{50.0f, 1.0f}, {40.0f, 2.0f}, {25.0f, 3.0f}, {10.0f, 4.0f}};
constexpr float     kBlastKickSpeed = 3.5f;
constexpr float     kBlastKickDecay = 0.12f;

// Blast hits planted to pass guards, each good for this step and the next.
struct BlastCrush {
    int hit = -1;
    int damage = 0;
    int step = -1;
};

std::array<BlastCrush, 16> g_blast_crush;
int                        g_blast_crush_next = 0;

int PlantBlast(const float *at, float damage_scale, bool guard_break, int no_kick = -1, float reach_scale = 1.0f) {
    if (NowMonstorUnit == nullptr || NowWeaponHave == nullptr) {
        return 0;
    }

    int planted = 0;

    for (int m = 0; m < 16; m++) {
        const MONSTOR &monster = NowMonstorUnit->monster[m];

        if (monster.state != 2 || monster.hp <= 0 || monster.revealed == 0) {
            continue;
        }

        // The nearest live hit sphere, on the ground plane.
        const auto &spheres = NowMonstorUnit->effect[m];
        int         nearest = -1;
        float       edge = 0.0f;

        for (int sphere = 0; sphere < 16; sphere++) {
            if (spheres.timer[sphere] == 0) {
                continue;
            }

            const float dx = spheres.position[sphere][0] - at[0];
            const float dz = spheres.position[sphere][2] - at[2];
            const float distance = std::max(0.0f, std::sqrt(dx * dx + dz * dz) - spheres.radius[sphere]);

            if (nearest < 0 || distance < edge) {
                nearest = sphere;
                edge = distance;
            }
        }

        if (nearest < 0) {
            continue;
        }

        float times = 0.0f;

        for (const BlastStep &step : kBlastSteps) {
            if (edge <= step.radius * reach_scale) {
                times = step.times;
            }
        }

        if (times <= 0.0f) {
            continue;
        }

        const int damage =
            std::max(1, static_cast<int>(std::lround(static_cast<float>(NowWeaponHave->attack) * times * damage_scale)));
        float centre[4] = {spheres.position[nearest][0], spheres.position[nearest][1], spheres.position[nearest][2], 1.0f};

        NowColData->Set(centre, damage, 1, spheres.radius[nearest] + 1.0f, 0.0f, 2, 2, 0, 0);

        auto &hit = NowColData->hit[NowColData->now_hit];
        hit.vs_monster = NowWeaponHave->vs_monster;
        hit.weapon_flags = NowWeaponHave->flags;
        hit.flags = 0;
        hit.owner = UserStatus->cur_chara;
        hit.attack_no = 0;

        float origin[4] = {at[0], at[1], at[2], 1.0f};
        NowColData->SetKickBack(origin, m == no_kick ? 0.0f : kBlastKickSpeed, kBlastKickDecay, 2);

        if (guard_break) {
            g_blast_crush[g_blast_crush_next] = {NowColData->now_hit, damage, g_step_count};
            g_blast_crush_next = (g_blast_crush_next + 1) % static_cast<int>(g_blast_crush.size());
        }

        planted++;
    }

    return planted;
}

// ── What the effects look like ──────────────────────────────────────────────────────────────

// The game's own effect models, the room's light and the tints, played through
// expanded_effects.cpp. Each flash-bang sword has its look: its blade's tint while it charges and
// stays primed, the glow on Toan while primed, the room's dim while primed, and the light its
// flash washes the room with.
struct SwordLook {
    int   item_no;
    float blade[3];
    float glow[3];
    float prime_dim;
    float light[3];
    float fog[3];
    int   ease_steps;
};

constexpr int kBoltFlashSteps = 24;

constexpr std::array<SwordLook, 3> kSwordLooks = {{
    {ITEM_WEAPON_SUN_SWORD, {200.0f, 190.0f, 150.0f}, {70.0f, 50.0f, 10.0f}, 0.0f, {255.0f, 240.0f, 200.0f}, {255.0f, 255.0f, 255.0f}, 5 * kStepsPerSecond},
    {ITEM_WEAPON_BIG_BANG, {110.0f, 150.0f, 255.0f}, {20.0f, 40.0f, 90.0f}, 0.35f, {220.0f, 230.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, 5 * kStepsPerSecond},
    {ITEM_WEAPON_SWORD_OF_ZEUS, {190.0f, 200.0f, 255.0f}, {30.0f, 30.0f, 60.0f}, 0.5f, {210.0f, 220.0f, 255.0f}, {200.0f, 210.0f, 255.0f}, kBoltFlashSteps},
}};

const SwordLook *LookOf(int item_no) {
    for (const SwordLook &look : kSwordLooks) {
        if (look.item_no == item_no) {
            return &look;
        }
    }

    return nullptr;
}

/**
 * rgb scaled by k into out.
 */
void Scaled(const float *rgb, float k, float *out) {
    out[0] = rgb[0] * k;
    out[1] = rgb[1] * k;
    out[2] = rgb[2] * k;
}

/**
 * A slow pulse between 0.6 and 1, for a glow that breathes.
 */
float Pulse() {
    return 0.8f + 0.2f * std::sin(static_cast<float>(g_step_count) * 0.1f);
}

float RandomYaw() {
    return static_cast<float>(std::rand() % 628) / 100.0f;
}

/**
 * The point over monster m's head: its feet in x and z, and the top of its highest hit sphere,
 * or its feet when it has none live.
 */
void MonsterTop(int m, float *out) {
    sceVu0FVECTOR feet;
    NowMonstorUnit->chara[m][0].GetPosition(feet);
    out[0] = feet[0];
    out[1] = feet[1];
    out[2] = feet[2];
    out[3] = 1.0f;

    const auto &spheres = NowMonstorUnit->effect[m];

    for (int sphere = 0; sphere < 16; sphere++) {
        if (spheres.timer[sphere] != 0) {
            out[1] = std::max(out[1], spheres.position[sphere][1] + spheres.radius[sphere]);
        }
    }
}

// Big Bang's blast is the game's own bomb blast model; the bolt is the Divine Beast Cave
// cutscene's lightning, ten times its size, with the Thunder Gem's crack and a short flash.
constexpr float kExplosionScale = 1.0f;
constexpr float kDetonateScale = 1.5f;
constexpr float kLightningScale = 10.0f;
constexpr int   kLightningStrike = 1; // lightning.cfg's playing clip (its first holds still)

// Guardian Grace: a soft glow on Xiao while she guards, brightened a moment by each heal.
constexpr float kGraceGlow[3] = {45.0f, 45.0f, 35.0f};
constexpr float kGraceGlowFade = 0.05f;
float           g_grace_glow = 0.0f;

// The summons appear in Queens' smoke puff at half its size, played three times as fast.
constexpr float kSmokeScale = 0.5f;
constexpr float kSmokeRate = 3.0f;

void Burst(const float *at, float scale) {
    const float ground[4] = {at[0], at[1], at[2], 1.0f};
    fx::Play(fx::Prop::Explosion, 0, ground, scale, RandomYaw());
    SndSePlay(SE_EXPLOSION, -1, 0);
}

void BoltSeen(const float *at) {
    const SwordLook &zeus = kSwordLooks[2];
    const float      ground[4] = {at[0], at[1], at[2], 1.0f};
    fx::Play(fx::Prop::Lightning, kLightningStrike, ground, kLightningScale, RandomYaw());
    SndSePlay(SE_THUNDER_GEM, -1, 0);
    fx::Flash(zeus.light, zeus.fog, kBoltFlashSteps);
}

// ── Sun Sword "Solar Flash" ─────────────────────────────────────────────────────────────────

// Guard held a second and a half primes the blade; the next swing that lands lets it go. Every
// enemy within 300 units takes a quarter of the attack and a shove, and is blinded (stopped)
// for five seconds. One flash at a time: no new charge until the last one's blindness ends. A
// primed blade left unused for ten seconds lapses. The flash costs 5 WHP.
constexpr int   kSolarChargeSteps = 90;
constexpr int   kSolarPrimedSteps = 10 * kStepsPerSecond;
constexpr int   kSolarBlindSteps = 5 * kStepsPerSecond;
constexpr float kSolarRadius = 300.0f;
constexpr float kSolarDamage = 0.25f;
constexpr float kSolarHitRadius = 25.0f;
constexpr float kSolarKickSpeed = 2.0f;
constexpr float kSolarKickDecay = 0.3f;
constexpr float kSolarWhp = 5.0f;
constexpr int   kGuarding = 5;

enum class Solar { Idle, Charging, Primed };

Solar g_solar = Solar::Idle;
int   g_solar_steps = 0;
int   g_solar_blind = 0;

bool SolarFlashes(int item_no) {
    return item_no == ITEM_WEAPON_SUN_SWORD || item_no == ITEM_WEAPON_BIG_BANG || item_no == ITEM_WEAPON_SWORD_OF_ZEUS;
}

// Sword of Zeus: the flash calls lightning. Each bolt is the blast at half strength at its foot,
// through any guard, and costs 10 WHP; the flash itself costs nothing. Locked on, the primed
// swing strikes the target and every swing of the combo after it strikes again, until the combo
// ends or the lock is let go. Not locked on, the six nearest enemies within 300 take a bolt
// each, billed as one.
constexpr float kBoltDamage = 0.5f;
constexpr float kBoltWhp = 10.0f;
constexpr float kBoltReach = 300.0f;
constexpr int   kBoltMost = 6;

bool g_zeus_chain = false;
int  g_zeus_chain_action = 0;

/**
 * The locked-on enemy, while it lives; -1 otherwise.
 */
int LockedTarget() {
    if (lockOnTargetFlag == 0 || NowMonstorUnit == nullptr || lockOnTargetNo < 0 || lockOnTargetNo >= 16) {
        return -1;
    }

    const MONSTOR &target = NowMonstorUnit->monster[lockOnTargetNo];
    return target.state == 2 && target.hp > 0 ? lockOnTargetNo : -1;
}

/**
 * A bolt on monster m: the half-strength blast where it stands, which takes it without a shove.
 */
void BoltOn(int m) {
    sceVu0FVECTOR at;
    NowMonstorUnit->chara[m][0].GetPosition(at);
    PlantBlast(at, kBoltDamage, true, m);
    BoltSeen(at);
}

/**
 * Bolts on the nearest enemies in reach, billed once; how many were struck.
 */
int BoltVolley() {
    if (NowMonstorUnit == nullptr) {
        return 0;
    }

    std::array<std::pair<float, int>, 16> near;
    int                                   count = 0;

    for (int m = 0; m < 16; m++) {
        const MONSTOR &monster = NowMonstorUnit->monster[m];

        if (monster.state == 2 && monster.hp > 0 && monster.player_distance > 0.0f && monster.player_distance <= kBoltReach) {
            near[count++] = {monster.player_distance, m};
        }
    }

    std::sort(near.begin(), near.begin() + count);

    const int struck = std::min(count, kBoltMost);

    for (int i = 0; i < struck; i++) {
        BoltOn(near[i].second);
    }

    if (struck > 0) {
        DrainWhp(ITEM_WEAPON_SWORD_OF_ZEUS, kBoltWhp);
    }

    return struck;
}

// Big Bang "Detonate": its whirlwind is the blast at Toan's feet, and a primed swing while
// locked on brings the blast down on the target with the flash. Either costs 20 WHP.
constexpr float kDetonateWhp = 20.0f;

void SolarStep(int held) {
    if (g_solar_blind > 0) {
        g_solar_blind--;
    }

    if (!SolarFlashes(held)) {
        g_solar = Solar::Idle;
        return;
    }

    const bool guarding = BtActStatus.guard_mode == kGuarding;

    switch (g_solar) {
        case Solar::Idle:
            if (guarding && g_solar_blind == 0) {
                g_solar = Solar::Charging;
                g_solar_steps = 0;
            }

            break;
        case Solar::Charging:
            if (!guarding || g_solar_blind > 0) {
                g_solar = Solar::Idle;
            } else if (++g_solar_steps >= kSolarChargeSteps) {
                // Primed: the game's own charge flash on Toan.
                g_solar = Solar::Primed;
                g_solar_steps = 0;
                setUnitAmbientAnime(15.0f, 1.0f, 0.0f, 122.0f, 208.0f);
            }

            break;
        case Solar::Primed:
            if (++g_solar_steps >= kSolarPrimedSteps) {
                g_solar = Solar::Idle;
            }

            break;
    }

    // The blade whitens as the guard charges it; primed, it stays lit, a glow breathes on Toan
    // and the room darkens for the strike to come.
    const SwordLook *look = LookOf(held);

    if (look == nullptr || g_solar == Solar::Idle) {
        return;
    }

    float tint[3];

    if (g_solar == Solar::Charging) {
        Scaled(look->blade, static_cast<float>(g_solar_steps) / kSolarChargeSteps, tint);
        fx::WeaponTint(tint);
        return;
    }

    fx::WeaponTint(look->blade);
    Scaled(look->glow, Pulse(), tint);
    fx::PlayerTint(tint);
    fx::Dim(look->prime_dim);
}

/**
 * The flash, as a primed swing lands: a hit on every enemy in reach, each stopped for the
 * blindness.
 */
// Dark Cloud Expanded: an enemy the flash blinds cowers in its guard while it lasts. Each
// species' guard loop by its model, from Expanded's table (GameData/EnemyGuardMotions.cs); one
// with no guard keeps the pose it was caught in.
struct GuardLoop {
    const char *model;
    int         motion;
};

// clang-format off
constexpr GuardLoop kGuardLoops[] = {
        {"c21a", 22}, {"e01a", 6}, {"e03a", 6}, {"e06a", 6}, {"e07a", 6}, {"e08a", 6}, {"e101", 6},
        {"e103", 6}, {"e104", 6}, {"e106", 6}, {"e108", 6}, {"e109", 6}, {"e10a", 6}, {"e111", 6},
        {"e112", 6}, {"e113", 6}, {"e114", 6}, {"e115", 6}, {"e116", 6}, {"e117", 6}, {"e118", 6},
        {"e119", 6}, {"e124", 6}, {"e125", 6}, {"e126", 6}, {"e127", 6}, {"e128", 6}, {"e129", 6},
        {"e130", 6}, {"e131", 6}, {"e132", 6}, {"e133", 6}, {"e135", 6}, {"e136", 6}, {"e137", 6},
        {"e138", 6}, {"e139", 6}, {"e141", 6}, {"e142", 6}, {"e143", 6}, {"e144", 6}, {"e145", 6},
        {"e146", 6}, {"e149", 6}, {"e14a", 6}, {"e150", 6}, {"e151", 6}, {"e152", 6}, {"e155", 6},
        {"e159", 6}, {"e15a", 6}, {"e160", 6}, {"e161", 6}, {"e162", 6}, {"e163", 6}, {"e164", 6},
        {"e16a", 6}, {"e17a", 6}, {"e18a", 6}, {"e19a", 6}, {"e20a", 6}, {"e21a", 6}, {"e22a", 6},
        {"e23a", 6}, {"e24a", 6}, {"e26a", 6}, {"e27a", 6}, {"e28a", 6}, {"e30a", 6}, {"e31a", 6},
        {"e32a", 6}, {"e33a", 6}, {"e35a", 6}, {"e37a", 6}, {"e39a", 6}, {"e40a", 6}, {"e42a", 6},
        {"e43a", 6}, {"e44a", 6}, {"e45a", 6}, {"e46a", 6}, {"e47a", 6}, {"e48a", 6}, {"e49a", 6},
        {"e50a", 6}, {"e51a", 6}, {"e52a", 6}, {"e55a", 6}, {"e56a", 6}, {"e59a", 6}, {"e62a", 6},
        {"e63a", 6}, {"e64a", 6}, {"e65a", 6}, {"e69a", 6}, {"e70a", 6}, {"e71a", 6}, {"e72a", 6},
        {"e73a", 6}, {"e74a", 6}, {"e75a", 6}, {"e76a", 6}, {"e79a", 6}, {"e81a", 6}, {"e83a", 6},
        {"e86a", 6}, {"e90a", 6}, {"e91a", 6},
};
// clang-format on

std::array<bool, 16> g_cower{};

int GuardLoopMotion(const MONSTOR &monster) {
    constexpr int kModels = static_cast<int>(sizeof(NowMonstorUnit->model) / sizeof(NowMonstorUnit->model[0]));

    if (NowMonstorUnit == nullptr || monster.base_model < 0 || monster.base_model >= kModels) {
        return -1;
    }

    const char *name = NowMonstorUnit->model[monster.base_model].model_name[0];

    for (const GuardLoop &entry : kGuardLoops) {
        if (std::strncmp(name, entry.model, std::strlen(entry.model)) == 0) {
            return entry.motion;
        }
    }

    return -1;
}

void SolarBlind() {
    g_solar_blind = kSolarBlindSteps;

    const int damage = std::max(1, static_cast<int>(static_cast<float>(NowWeaponHave->attack) * kSolarDamage));

    for (int m = 0; m < 16; m++) {
        MONSTOR &monster = NowMonstorUnit->monster[m];

        if (monster.state != 2 || monster.hp <= 0 || monster.player_distance <= 0.0f || monster.player_distance > kSolarRadius) {
            continue;
        }

        sceVu0FVECTOR at;
        NowMonstorUnit->chara[m][0].GetPosition(at);

        const auto &spheres = NowMonstorUnit->effect[m];

        for (int sphere = 0; sphere < 16; sphere++) {
            if (spheres.timer[sphere] != 0) {
                at[0] = spheres.position[sphere][0];
                at[1] = spheres.position[sphere][1];
                at[2] = spheres.position[sphere][2];
                break;
            }
        }

        NowColData->Set(at, damage, 1, kSolarHitRadius, 0.0f, 2, 2, 0, 0);

        auto &hit = NowColData->hit[NowColData->now_hit];
        hit.vs_monster = NowWeaponHave->vs_monster;
        hit.weapon_flags = NowWeaponHave->flags;
        hit.flags = GetWeaponElementAttr(NowWeaponHave->best_elem);
        hit.owner = UserStatus->cur_chara;
        hit.attack_no = 0;
        NowColData->SetKickBack(CharaMain.pos, kSolarKickSpeed, kSolarKickDecay, 2);

        monster.stop_timer = std::max(monster.stop_timer, kSolarBlindSteps);
        g_cower[static_cast<std::size_t>(m)] = true;
    }

    // A white pulse on Toan as the room is blinded, and the room washed out with the sword's light.
    setUnitAmbientAnime(90.0f, 1.0f, 255.0f, 255.0f, 255.0f);

    if (const SwordLook *look = LookOf(EffectItem(NowWeaponHave))) {
        fx::Flash(look->light, look->fog, look->ease_steps);
    }
}

bool JudgementDrop();

/**
 * The primed flash let go on target, the enemy the swing was locked on (-1 for none).
 */
void SolarRelease(int target) {
    if (NowMonstorUnit == nullptr || NowWeaponHave == nullptr) {
        return;
    }

    const bool on_target = target >= 0 && target < 16 && NowMonstorUnit->monster[target].state == 2 &&
                           NowMonstorUnit->monster[target].hp > 0;

    // Sword of Zeus: lightning with the flash, which costs nothing of its own.
    if (NowWeaponHave->item_no == ITEM_WEAPON_SWORD_OF_ZEUS) {
        if (on_target) {
            BoltOn(target);
            DrainWhp(ITEM_WEAPON_SWORD_OF_ZEUS, kBoltWhp);
            g_zeus_chain = true;
            g_zeus_chain_action = BtActStatus.action_no;
        } else {
            BoltVolley();
        }

        SolarBlind();
        return;
    }

    SolarBlind();

    // Detonate: locked on, the blast lands on the target, through any guard.
    if (NowWeaponHave->item_no == ITEM_WEAPON_BIG_BANG && on_target) {
        sceVu0FVECTOR at;
        NowMonstorUnit->chara[target][0].GetPosition(at);
        PlantBlast(at, 1.0f, true);
        Burst(at, kDetonateScale);
        DrainWhp(ITEM_WEAPON_BIG_BANG, kDetonateWhp);
        return;
    }

    DrainWhp(NowWeaponHave->item_no, kSolarWhp);
}

void SolarFlash() {
    if (g_solar != Solar::Primed || NowMonstorUnit == nullptr) {
        return;
    }

    g_solar = Solar::Idle;

    // Big Bang and the Sword of Zeus: the judgement blade hanging over the locked-on enemy falls,
    // and the flash waits for it to land.
    if (JudgementDrop()) {
        return;
    }

    SolarRelease(LockedTarget());
}

/**
 * A combo swing of the Sword of Zeus's chain lands: the next bolt and flash on the target.
 */
void ZeusChainHit() {
    const int target = LockedTarget();

    if (target < 0) {
        g_zeus_chain = false;
        return;
    }

    BoltOn(target);
    DrainWhp(ITEM_WEAPON_SWORD_OF_ZEUS, kBoltWhp);
    SolarBlind();
}

// The Sword of Zeus's charge attack: it gives up the whirlwind, and a charge held three seconds
// past the lunge's level is its own, flashed on Toan; released, the lunge brings a bolt down 40
// units ahead of where it lands, or on the locked-on enemy.
constexpr float kLungeCharge = 1.5f;
constexpr int   kZeusChargeSteps = 3 * kStepsPerSecond;
constexpr float kBoltAhead = 40.0f;
constexpr int   kComboFirst = 0x24;
constexpr int   kComboLast = 0x28;
constexpr int   kLunge = 0xF;

int   g_zeus_charge_steps = 0;
bool  g_zeus_charged = false;
bool  g_zeus_lunging = false;
float g_zeus_lunge_from[3] = {};

// Dark Cloud Expanded: the camera held at its world height through the charged lunge, so it does
// not rise and fall with Toan (CameraHold.cs); its distance and angle stay the game's.
bool  g_zeus_camera_held = false;
float g_zeus_camera_y = 0.0f;      // the eye's world height when the lunge began
float g_zeus_camera_height = 0.0f; // the camera's own height setting, put back after

void ZeusCameraHold() {
    if (NowCamera__3 == nullptr) {
        return;
    }

    if (!g_zeus_camera_held) {
        g_zeus_camera_held = true;
        g_zeus_camera_height = NowCamera__3->height;
        g_zeus_camera_y = NowCamera__3->follow[1] + NowCamera__3->height;
    }

    NowCamera__3->height = g_zeus_camera_y - NowCamera__3->follow[1];
}

void ZeusCameraRelease() {
    if (g_zeus_camera_held && NowCamera__3 != nullptr) {
        NowCamera__3->height = g_zeus_camera_height;
    }

    g_zeus_camera_held = false;
}

void ZeusStep(int held) {
    if (held != ITEM_WEAPON_SWORD_OF_ZEUS) {
        ZeusCameraRelease();
        g_zeus_chain = false;
        g_zeus_charged = false;
        g_zeus_lunging = false;
        g_zeus_charge_steps = 0;
        return;
    }

    const int action = BtActStatus.action_no;

    // The chain ends with the combo, a new combo or the lock let go.
    if (g_zeus_chain) {
        const bool swinging = action >= kComboFirst && action <= kComboLast;

        if (!swinging || action < g_zeus_chain_action || LockedTarget() < 0) {
            g_zeus_chain = false;
        } else {
            g_zeus_chain_action = action;
        }
    }

    if (action == 14) {
        if (BtActStatus.charge_time >= kLungeCharge && !g_zeus_charged && ++g_zeus_charge_steps >= kZeusChargeSteps) {
            g_zeus_charged = true;
            setUnitAmbientAnime(15.0f, 1.0f, 0.0f, 122.0f, 208.0f);
        }

        // The room darkens as the charge builds past the lunge's level, the blade whitening.
        if (BtActStatus.charge_time >= kLungeCharge) {
            const float k = g_zeus_charged ? 1.0f : 0.5f + 0.5f * static_cast<float>(g_zeus_charge_steps) / kZeusChargeSteps;
            float       tint[3];
            Scaled(kSwordLooks[2].blade, k, tint);
            fx::WeaponTint(tint);
            fx::Dim(0.75f * k);
        }

        return;
    }

    if (action == kLunge && g_zeus_charged) {
        fx::WeaponTint(kSwordLooks[2].blade);
        fx::Dim(0.75f);

        if (!g_zeus_lunging) {
            g_zeus_lunging = true;
            g_zeus_lunge_from[0] = CharaMain.pos[0];
            g_zeus_lunge_from[1] = CharaMain.pos[1];
            g_zeus_lunge_from[2] = CharaMain.pos[2];
        }

        ZeusCameraHold();
        return;
    }

    // The lunge is over: the bolt.
    if (g_zeus_lunging) {
        if (const int target = LockedTarget(); target >= 0) {
            BoltOn(target);
        } else {
            float       at[4] = {CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], 1.0f};
            const float dx = at[0] - g_zeus_lunge_from[0];
            const float dz = at[2] - g_zeus_lunge_from[2];
            const float length = std::sqrt(dx * dx + dz * dz);

            if (length > 1e-3f) {
                at[0] += dx / length * kBoltAhead;
                at[2] += dz / length * kBoltAhead;
            }

            PlantBlast(at, kBoltDamage, true);
            BoltSeen(at);
        }

        DrainWhp(ITEM_WEAPON_SWORD_OF_ZEUS, kBoltWhp);
        setUnitAmbientAnime(90.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    }

    ZeusCameraRelease();
    g_zeus_lunging = false;
    g_zeus_charged = false;
    g_zeus_charge_steps = 0;
}

// ── Summons: a posed copy of a model, and the lure ──────────────────────────────────────────

bool ActiveMonster(int m);

/**
 * A still copy of a character's model where it stood when taken: every frame's local pose is
 * kept, and each draw lays the copy over the live model's own frames, draws it at the copy's
 * spot and puts the live pose back. The copy is only good while the character keeps the model
 * it was taken from, so Valid() checks the root frame is the same one.
 */
struct PoseCopy {
    struct FrameSnap {
        CFrame       *frame;
        sceVu0FMATRIX local;
        sceVu0FVECTOR scale;
        sceVu0FVECTOR position;
        sceVu0FVECTOR rotation;
        int           no_rotation;
        int           rot_type;
        int           srt;
    };

    static constexpr int kMaxFrames = 192;

    CCharacter *source = nullptr;
    CFrame     *root = nullptr;
    CCharacter *weapon = nullptr;
    CFrame     *weapon_root = nullptr;
    int         count = 0;
    int         weapon_from = 0; // the weapon's frames start here
    FrameSnap   frames[kMaxFrames];
    FrameSnap   live[kMaxFrames];
    float       pos[4] = {};
    CVector3_f_ rotation{};

    static void Collect(CFrame *frame, FrameSnap *out, int &count) {
        for (; frame != nullptr && count < kMaxFrames; frame = frame->brother) {
            Save(frame, out[count++]);
            Collect(frame->child, out, count);
        }
    }

    static void Save(CFrame *frame, FrameSnap &snap) {
        snap.frame = frame;
        std::memcpy(snap.local, frame->local, sizeof(snap.local));
        std::memcpy(snap.scale, frame->scale, sizeof(snap.scale));
        std::memcpy(snap.position, frame->position, sizeof(snap.position));
        std::memcpy(snap.rotation, frame->rotation, sizeof(snap.rotation));
        snap.no_rotation = frame->no_rotation;
        snap.rot_type = frame->rot_type;
        snap.srt = frame->srt;
    }

    static void Load(const FrameSnap &snap) {
        CFrame *frame = snap.frame;
        std::memcpy(frame->local, snap.local, sizeof(snap.local));
        std::memcpy(frame->scale, snap.scale, sizeof(snap.scale));
        std::memcpy(frame->position, snap.position, sizeof(snap.position));
        std::memcpy(frame->rotation, snap.rotation, sizeof(snap.rotation));
        frame->no_rotation = snap.no_rotation;
        frame->rot_type = snap.rot_type;
        frame->srt = snap.srt;
        frame->world_valid = false;
    }

    void Take(CCharacter &chara, CCharacter *held) {
        Drop();

        if (chara.frame == nullptr) {
            return;
        }

        source = &chara;
        root = chara.frame;
        Collect(root, frames, count);
        weapon_from = count;

        if (held != nullptr && held->frame != nullptr) {
            weapon = held;
            weapon_root = held->frame;
            Collect(weapon_root, frames, count);
        }

        sceVu0CopyVector(pos, chara.pos);
        rotation = chara.rotation;
    }

    void Drop() {
        source = nullptr;
        root = nullptr;
        weapon = nullptr;
        weapon_root = nullptr;
        count = 0;
        weapon_from = 0;
    }

    bool Valid() const {
        return source != nullptr && source->frame == root && root != nullptr;
    }

    /**
     * Draws the copy with the scene's ambient light at alpha (0 to 1).
     */
    void Draw(const float *scene_ambient, float alpha) {
        if (!Valid() || alpha <= 0.0f) {
            return;
        }

        const bool with_weapon = weapon != nullptr && weapon->frame == weapon_root;
        const int  used = with_weapon ? count : weapon_from;

        for (int i = 0; i < used; i++) {
            Save(frames[i].frame, live[i]);
            Load(frames[i]);
        }

        float       live_pos[4];
        CVector3_f_ live_rotation = source->rotation;
        sceVu0CopyVector(live_pos, source->pos);
        sceVu0CopyVector(source->pos, pos);
        source->rotation = rotation;

        // The cloth hangs where the live character is, so the copy goes without it.
        static CCloth *no_cloth[CHARA_CLOTH_MAX] = {};
        CCloth      **live_cloth = source->cloth;
        source->cloth = no_cloth;

        const float live_alpha = source->ambient_offset[3];
        source->ambient_offset[3] = 128.0f * std::min(alpha, 1.0f);

        float ambient[4];
        sceVu0CopyVector(ambient, const_cast<float *>(scene_ambient));
        MGSetAmbient(ambient);
        TexManager.ReloadTexture(Vif1Packet, 0x11);
        source->Draw();

        if (with_weapon) {
            TexManager.ReloadTexture(Vif1Packet, 0x1D);
            weapon->Draw();
        }

        source->ambient_offset[3] = live_alpha;
        source->cloth = live_cloth;
        sceVu0CopyVector(source->pos, live_pos);
        source->rotation = live_rotation;

        for (int i = 0; i < used; i++) {
            Load(live[i]);
        }
    }
};

/**
 * A point the enemies go for in place of the player: every "where is the player" an enemy asks
 * is answered with it, until that enemy is hurt, which breaks the spell on it for this lure.
 */
struct Lure {
    bool                 active = false;
    float                pos[4] = {};
    std::array<bool, 16> broken{};

    void Place(const float *at) {
        active = true;
        sceVu0CopyVector(pos, const_cast<float *>(at));
        broken.fill(false);
    }

    void Drop() {
        active = false;
    }

    bool Pulls(int m) const {
        return active && m >= 0 && m < 16 && !broken[m] && ActiveMonster(m);
    }
};

// ── Mirage "Mirage" ─────────────────────────────────────────────────────────────────────────

// Ungaga's guard held a quarter second flashes him and leaves a still copy of him in the guard
// pose where he stood. For 12 seconds (18 from Hercules' Wrath) every enemy goes for the copy
// instead of him; one that is hurt sees through it and turns back to him. A new guard while one
// stands replaces it, the old copy dissolving as the new one appears. It ends on expiry, a weapon
// or party change and a new floor. It stands in a heat shimmer (DecoyHaze).
constexpr int   kMirageGuardSteps = 15;
constexpr int   kMirageSteps = 12 * kStepsPerSecond;
constexpr int   kMirageHerculesSteps = 18 * kStepsPerSecond;
constexpr int   kMirageFadeSteps = kStepsPerSecond / 2;
constexpr float kMirageAlpha = 0.85f;

constexpr int   kMirageHandoffSteps = kStepsPerSecond / 4;

PoseCopy g_mirage_copy;
PoseCopy g_mirage_old;            // the copy a new cast replaced, dissolving where it stood
int      g_mirage_old_steps = 0;  // left of its dissolve
int      g_mirage_delay = 0;      // steps the new copy waits to appear, while the old one dissolves
Lure     g_mirage_lure;
int      g_mirage_steps = 0;      // left to stand
int      g_mirage_life = 0;       // what it started with
int      g_mirage_guard = 0;      // steps the guard has been held
bool     g_mirage_cast = false;   // this guard has cast already
int      g_mirage_weapon = -1;    // the weapon that cast it

bool MirageLine(int item_no) {
    return item_no == ITEM_WEAPON_MIRAGE || item_no == ITEM_WEAPON_HERCULES_WRATH;
}

bool DecoyUp() {
    return g_mirage_lure.active;
}

void MirageEnd() {
    g_mirage_copy.Drop();
    g_mirage_old.Drop();
    g_mirage_old_steps = 0;
    g_mirage_delay = 0;
    g_mirage_lure.Drop();
    g_mirage_steps = 0;
    g_mirage_weapon = -1;
}

void MirageReset() {
    MirageEnd();
    g_mirage_guard = 0;
    g_mirage_cast = false;
}

void MirageCast(int held) {
    // Dark Cloud Expanded: a copy already standing dissolves where it is over a quarter second
    // while the new one waits to appear.
    g_mirage_delay = 0;

    if (g_mirage_lure.active && g_mirage_copy.Valid()) {
        g_mirage_old = g_mirage_copy;
        g_mirage_old_steps = kMirageHandoffSteps;
        g_mirage_delay = kMirageHandoffSteps;
    }

    g_mirage_copy.Take(CharaMain, NowWeapon);
    g_mirage_lure.Place(CharaMain.pos);
    g_mirage_weapon = held;
    g_mirage_life = held == ITEM_WEAPON_HERCULES_WRATH ? kMirageHerculesSteps : kMirageSteps;
    g_mirage_steps = g_mirage_life;
    setUnitAmbientAnime(15.0f, 1.0f, 255.0f, 230.0f, 160.0f);
}

/**
 * Who wields the Mirage: Ungaga with the Mirage line, or Xiao with Super Steve and a sphere of
 * it (Expanded's Mirage.cs: both share the guard pose it waits for).
 */
bool MirageWielder() {
    return UserStatus != nullptr && (UserStatus->cur_chara == CHARA_UNGAGA || UserStatus->cur_chara == CHARA_XIAO);
}

void MirageStep() {
    const int held = UserStatus != nullptr && UserStatus->cur_chara == CHARA_XIAO ? EffectOf(CHARA_XIAO) : HeldBy(CHARA_UNGAGA);

    if (g_mirage_old_steps > 0 && --g_mirage_old_steps == 0) {
        g_mirage_old.Drop();
    }

    if (!MirageLine(held)) {
        if (g_mirage_lure.active || g_mirage_guard != 0) {
            MirageReset();
        }

        return;
    }

    if (g_mirage_lure.active && (held != g_mirage_weapon || !g_mirage_copy.Valid() || --g_mirage_steps <= 0)) {
        MirageEnd();
    }

    if (BtActStatus.guard_mode != kGuarding) {
        g_mirage_guard = 0;
        g_mirage_cast = false;
        return;
    }

    if (!g_mirage_cast && ++g_mirage_guard >= kMirageGuardSteps) {
        g_mirage_cast = true;
        MirageCast(held);
    }
}

void MirageHurt(const MONSTOR &monster) {
    if (!g_mirage_lure.active || NowMonstorUnit == nullptr) {
        return;
    }

    const int m = static_cast<int>(&monster - NowMonstorUnit->monster);

    if (m >= 0 && m < 16) {
        g_mirage_lure.broken[m] = true;
    }
}

// ── Hercules' Wrath "Air Strike" ────────────────────────────────────────────────────────────

// The guard's second level: held a quarter second (the Mirage's decoy, its first) and five more,
// it primes, with the charge flash on Ungaga, for as long as the decoy stands. The next swing's
// hit lands Big Bang's blast on the decoy, where the enemies gather, at twice the reach, through
// any guard, for 20 WHP, and the decoy goes with it. With no decoy up it lands at the spear tip.
constexpr int   kHerculesFirstSteps = 15;
constexpr int   kHerculesChargeSteps = 5 * kStepsPerSecond;
constexpr int   kHerculesPrimedSteps = 18 * kStepsPerSecond;
constexpr float kHerculesReach = 2.0f;
constexpr float kHerculesWhp = 20.0f;
constexpr float kHerculesGold[3] = {200.0f, 160.0f, 40.0f};
constexpr float kHerculesDim = 0.3f;
constexpr float kHerculesLight[3] = {255.0f, 230.0f, 150.0f};
constexpr int   kHerculesFlashSteps = 40;
constexpr int   kSparkleSwirl = 1; // e508_ex.cfg's long swirl

enum class Wrath { Idle, Charging, Primed };

Wrath g_wrath = Wrath::Idle;
int   g_wrath_steps = 0;

void HerculesStep(int held) {
    if (held != ITEM_WEAPON_HERCULES_WRATH) {
        g_wrath = Wrath::Idle;
        return;
    }

    const bool guarding = BtActStatus.guard_mode == kGuarding;

    switch (g_wrath) {
        case Wrath::Idle:
            if (guarding) {
                g_wrath = Wrath::Charging;
                g_wrath_steps = 0;
            }

            break;
        case Wrath::Charging:
            if (!guarding) {
                g_wrath = Wrath::Idle;
            } else if (++g_wrath_steps >= kHerculesFirstSteps + kHerculesChargeSteps) {
                g_wrath = Wrath::Primed;
                g_wrath_steps = 0;
                setUnitAmbientAnime(15.0f, 1.0f, 0.0f, 122.0f, 208.0f);
            }

            break;
        case Wrath::Primed:
            if (++g_wrath_steps >= kHerculesPrimedSteps || !DecoyUp()) {
                g_wrath = Wrath::Idle;
            }

            break;
    }

    // The spear turns gold as the second level charges; primed, it stays gold and the room dims.
    float tint[3];

    if (g_wrath == Wrath::Charging && g_wrath_steps > kHerculesFirstSteps) {
        const float k = static_cast<float>(g_wrath_steps - kHerculesFirstSteps) / kHerculesChargeSteps;
        Scaled(kHerculesGold, k, tint);
        fx::WeaponTint(tint);
    } else if (g_wrath == Wrath::Primed) {
        fx::WeaponTint(kHerculesGold);
        fx::Dim(kHerculesDim);
    }
}

// ── Atlamillia Sword "Atlamillia Insurance" ─────────────────────────────────────────────────

// While an Atlamillia Sword is owned, a weapon that breaks in a dungeon is saved as a SynthSphere
// of itself, at a tenth of its stats per level plus one, up to half; its attachments are lost.
// The sphere waits in a small chest on a random floor of that dungeon that has atla, placed each
// time that floor is built until it is taken (Demon Shaft has none: the next floor built). The
// waiting spheres are kept with the save, in quests.json.
struct InsuredSphere {
    int         dungeon;
    int         floor; // -1: the next floor built
    ATTACH_LIST sphere;
    int         box; // its chest on the floor as built now, or -1
};

std::vector<InsuredSphere> g_insured;
std::string                g_insured_notice; // said once the game's own break message is done

// The save's copy: a count, then each sphere's dungeon, floor and its 32 bytes as eight numbers.
constexpr std::string_view kInsuranceId = "expanded_atlamillia";
constexpr int              kInsuranceWords = sizeof(ATTACH_LIST) / sizeof(s32);

void InsuranceSave() {
    dc::quest::Progress *progress = dc::quest::EditProgress(kInsuranceId);

    if (progress == nullptr) {
        return;
    }

    progress->vars.clear();
    progress->vars["count"] = static_cast<int>(g_insured.size());

    for (std::size_t i = 0; i < g_insured.size(); i++) {
        const InsuredSphere &insured = g_insured[i];
        std::array<s32, kInsuranceWords> words{};
        std::memcpy(words.data(), &insured.sphere, sizeof(ATTACH_LIST));
        progress->vars[std::format("{}.dungeon", i)] = insured.dungeon;
        progress->vars[std::format("{}.floor", i)] = insured.floor;

        for (int word = 0; word < kInsuranceWords; word++) {
            progress->vars[std::format("{}.w{}", i, word)] = words[static_cast<std::size_t>(word)];
        }
    }
}

int InsuredFloor(int dungeon) {
    if (dungeon < 0 || dungeon >= 6 || UserStatus == nullptr) {
        return -1;
    }

    std::array<int, 40> floors;
    int                 count = 0;

    for (int floor = 0; floor < 40; floor++) {
        const bool has_atla = std::ranges::any_of(UserStatus->atra_grid[dungeon][floor], [](s32 slot) { return slot != -1; });
        const bool taken = std::ranges::any_of(g_insured, [&](const InsuredSphere &other) {
            return other.dungeon == dungeon && other.floor == floor;
        });

        if (has_atla && !taken) {
            floors[count++] = floor;
        }
    }

    return count > 0 ? floors[std::rand() % count] : -1;
}

// ── Heaven's Cloud "Typhoon" ────────────────────────────────────────────────────────────────

// Holding the whirlwind charge grows the blade, slowly at first and faster toward the end, to
// three times its size over four seconds; the whirlwind keeps the size and hits as wide.
constexpr float kTyphoonMost = 3.0f;
constexpr int   kTyphoonSteps = 4 * kStepsPerSecond;
constexpr float kTyphoonCurve = 2.5f;
constexpr float kHeavensCloudHitBone = 9.2053f; // the stock blade's hit bone, units from the hand

float g_typhoon_factor = 1.0f;
int   g_typhoon_steps = -1;
bool  g_typhoon_flashed = false;

void TyphoonStep(int held) {
    if (held != ITEM_WEAPON_HEAVEN_S_CLOUD) {
        g_typhoon_factor = 1.0f;
        g_typhoon_steps = -1;
        g_typhoon_flashed = false;
        return;
    }

    if (BtActStatus.action_no == 14 && BtActStatus.charge_level == 2) {
        g_typhoon_steps = std::max(0, g_typhoon_steps) + 1;

        const float t = std::min(1.0f, static_cast<float>(g_typhoon_steps) / kTyphoonSteps);
        g_typhoon_factor = 1.0f + std::pow(t, kTyphoonCurve) * (kTyphoonMost - 1.0f);

        // Full size: the charge's own flash says holding longer buys nothing more.
        if (t >= 1.0f && !g_typhoon_flashed) {
            g_typhoon_flashed = true;
            setUnitAmbientAnime(15.0f, 1.0f, 0.0f, 122.0f, 208.0f);
        }
    } else if (BtActStatus.action_no != 0x18) {
        g_typhoon_factor = 1.0f;
        g_typhoon_steps = -1;
        g_typhoon_flashed = false;
    }
}

void BladeStep(int held) {
    if (held != ITEM_WEAPON_KITCHEN_KNIFE && g_knife_bless_steps > 0) {
        g_knife_bless_steps = 0;
    }

    if (held == ITEM_WEAPON_KITCHEN_KNIFE && g_knife_bless_steps > 0) {
        g_knife_bless_steps--;

        if (g_knife_bless_steps == 0) {
            ShowDungeonMessage("The spring's blessing fades\nfrom the Kitchen Knife...", 4 * kStepsPerSecond);
        }
    }

    // An ally out: Toan's model is left as it stands until he is back.
    if (held == -1) {
        return;
    }

    float factor = 1.0f;

    if (held == ITEM_WEAPON_HEAVEN_S_CLOUD) {
        factor = g_typhoon_factor;
    } else if (held == ITEM_WEAPON_CLAYMORE) {
        factor = kClaymoreScale;
    } else if (held == ITEM_WEAPON_KITCHEN_KNIFE && g_knife_bless_steps > 0) {
        factor = kKnifeBlessedLength / kKnifeStockLength;
    }

    ScaleBlade(held, factor);
}

// ── Xiao: the lock-on stride ────────────────────────────────────────────────────────────────

// Dragon's Y and the weapons built from it move faster while locked on. Her walk is root
// motion, so the strafe and guard-walk motions playing faster carry her faster too.
constexpr float kLockOnStride = 1.3f;
constexpr int   kLockOnMotions[] = {19, 20, 21, 22, 8, 9, 10, 33};

bool g_stride_held = false;

void StrideStep() {
    const bool grants = Is(EffectOf(CHARA_XIAO), {ITEM_WEAPON_DRAGON_S_Y, ITEM_WEAPON_DIVINEBEASTTITLE,
                                                ITEM_WEAPON_ANGEL_SHOOTER, ITEM_WEAPON_ANGE_GEAR});
    const bool want = grants && lockOnTargetFlag != 0 && std::ranges::find(kLockOnMotions, CharaMain.motion_no) !=
                                                             std::end(kLockOnMotions);

    // The game puts the motion back to its own speed whenever the motion changes, so the
    // stride is set again each step.
    if (want) {
        CharaMain.motion_speed = kLockOnStride;
        g_stride_held = true;
    } else if (g_stride_held) {
        if (CharaMain.motion_speed == kLockOnStride) {
            CharaMain.motion_speed = -1.0f;
        }

        g_stride_held = false;
    }
}

// ── Ungaga: the Halberd line's charge shot ─────────────────────────────────────────────────

/**
 * A form of Tornado Charge Buff: how much faster the charge shot flies and how much larger it
 * is drawn and hits.
 */
struct TornadoForm {
    float travel;
    float scale;
};

const TornadoForm *TornadoFormOf(int item_no) {
    static constexpr TornadoForm kHalberd = {1.4f, 1.1f};
    static constexpr TornadoForm kScorpion = {1.7f, 1.3f};
    static constexpr TornadoForm kMirage = {2.0f, 1.5f};
    static constexpr TornadoForm kHercules = {2.5f, 1.7f};

    switch (item_no) {
        case ITEM_WEAPON_HALBERT:
            return &kHalberd;
        case ITEM_WEAPON_SCORPION:
            return &kScorpion;
        case ITEM_WEAPON_MIRAGE:
        case ITEM_WEAPON_CACTUS:
            return &kMirage;
        case ITEM_WEAPON_HERCULES_WRATH:
        case ITEM_WEAPON_TERRA_SWORD:
        case ITEM_WEAPON_BABEL_S_SPEAR:
            return &kHercules;
        default:
            return nullptr;
    }
}

// The effect's description, copied with the form's speeds and radii, stands in for the game's
// while the spear is held; the game's own is kept to put back.
BT_SHOT_EFFECT     g_tornado_data = {};
BT_SHOT_EFFECT    *g_tornado_original = nullptr;
CSHOT_EFFECT      *g_tornado_effect = nullptr;
const TornadoForm *g_tornado_form = nullptr;

void TornadoScale(CSHOT_EFFECT *effect, float scale) {
    float size[4] = {scale, scale, scale, 1.0f};

    for (CCharacter &chara : effect->chara) {
        chara.SetScale(size);
    }
}

void TornadoRestore() {
    if (g_tornado_effect != nullptr && g_tornado_effect->effect_data == &g_tornado_data) {
        g_tornado_effect->effect_data = g_tornado_original;
        TornadoScale(g_tornado_effect, 1.0f);
    }

    g_tornado_effect = nullptr;
    g_tornado_original = nullptr;
    g_tornado_form = nullptr;
}

void TornadoStep() {
    const TornadoForm *form = TornadoFormOf(HeldBy(CHARA_UNGAGA));
    CSHOT_EFFECT      *effect = NowMainEffect;

    if (form == nullptr || effect == nullptr || effect->effect_data == nullptr) {
        TornadoRestore();
        return;
    }

    // Still ours: nothing to do. A floor load or a menu reloads the effect with the game's own
    // description, which is then replaced again.
    if (effect == g_tornado_effect && form == g_tornado_form && effect->effect_data == &g_tornado_data) {
        return;
    }

    TornadoRestore();

    // The shot reads its speed when it starts or changes phase, and its radius each step.
    g_tornado_original = effect->effect_data;
    g_tornado_data = *g_tornado_original;

    for (int phase = 0; phase < 4; phase++) {
        g_tornado_data.speed[phase] *= form->travel;
        g_tornado_data.radius[phase] *= form->scale;
    }

    effect->effect_data = &g_tornado_data;
    g_tornado_effect = effect;
    g_tornado_form = form;
    TornadoScale(effect, form->scale);
}

// ── Goro: Cold Storage ──────────────────────────────────────────────────────────────────────

// The Frozen Tuna banks 2 HP for each WHP it wears. Once Goro is hurt the bank flows back into
// his life at 2 HP a second, while he is below his max; a repair empties it.
constexpr float kTunaHealPerWhp = 2.0f;
constexpr float kTunaHealPerStep = 2.0f / kStepsPerSecond;

float g_tuna_bank = 0.0f;
float g_tuna_carry = 0.0f;
bool  g_tuna_healing = false;
float g_tuna_last_whp = -1.0f;
int   g_tuna_last_hp = -1;
int   g_tuna_rolled_step = -1;

void TunaReset() {
    g_tuna_bank = 0.0f;
    g_tuna_carry = 0.0f;
    g_tuna_healing = false;
    g_tuna_last_whp = -1.0f;
    g_tuna_last_hp = -1;
}

void TunaStep() {
    if (HeldBy(CHARA_GORO) != ITEM_WEAPON_FROZEN_TUNA) {
        TunaReset();
        return;
    }

    const float whp = NowWeaponHave->durability_f;
    const int   hp = UserStatus->hp[CHARA_GORO];
    const int   max_hp = UserStatus->max_hp[CHARA_GORO];

    if (g_tuna_last_whp >= 0.0f) {
        if (whp < g_tuna_last_whp) {
            g_tuna_bank += (g_tuna_last_whp - whp) * kTunaHealPerWhp;
        } else if (whp > g_tuna_last_whp) {
            g_tuna_bank = 0.0f;
            g_tuna_carry = 0.0f;
            g_tuna_healing = false;
        }
    }

    if (g_tuna_last_hp >= 0 && hp < g_tuna_last_hp && g_tuna_bank > 0.0f) {
        g_tuna_healing = true;
    }

    if (g_tuna_healing && g_tuna_bank > 0.0f && hp > 0 && hp < max_hp) {
        const float drain = std::min(kTunaHealPerStep, g_tuna_bank);
        g_tuna_bank -= drain;
        g_tuna_carry += drain;

        const int heal = static_cast<int>(g_tuna_carry);

        if (heal > 0) {
            UserStatus->hp[CHARA_GORO] = static_cast<s16>(std::min(hp + heal, max_hp));
            g_tuna_carry -= static_cast<float>(heal);
        }
    }

    if (g_tuna_bank <= 0.0f) {
        g_tuna_healing = false;
    }

    g_tuna_last_whp = whp;
    g_tuna_last_hp = UserStatus->hp[CHARA_GORO];
}

// ── Borrowed shots ──────────────────────────────────────────────────────────────────────────

// A monster's projectile fired by the party: the monster's own effect, already loaded for the
// floor, copied into an effect of the mod's own that hits enemies instead of the party. Steal
// Shot and Dragon's Breath each keep one.
constexpr int kBorrowedSlots = 6;
constexpr int kBorrowedHitWait = 45;
constexpr int kPlayerTarget = 2;

// Self-destructs, where the monster bursts where it stands: nothing that flies to take.
constexpr int kSelfBursts[] = {16, 17, 18};

struct BorrowedShot {
    CSHOT_EFFECT          effect;
    BT_SHOT_EFFECT        data = {};
    bool                  ready = false;
    const CSHOT_EFFECT   *source = nullptr;
    const BT_SHOT_EFFECT *source_data = nullptr;
    CMonstorUnit         *unit = nullptr;
    int                   floor = -1;
    int                   map = -1;
    u_char               *frames;
    int                   frame_quads;
    CDataAlloc2<1>        alloc{-1};

    BorrowedShot(u_char *storage, std::size_t bytes) : frames(storage), frame_quads(static_cast<int>(bytes / 16)) {}

    void Drop() {
        effect.Initialize();
        ready = false;
        source = nullptr;
        source_data = nullptr;
        unit = nullptr;
    }

    /**
     * Whether what it was built from still stands: the same floor, the same monsters and the
     * monster's effect as it was.
     */
    bool Valid() {
        if (!ready || UserStatus == nullptr) {
            return false;
        }

        if (NowMonstorUnit != unit || UserStatus->cur_floor != floor || selectMapNo != map || source->effect_data != source_data) {
            Drop();
            return false;
        }

        return true;
    }

    /**
     * Builds it from the monster's effect: its description aimed at enemies, and each slot a copy
     * of its model with frames of its own, drawn from the monster's textures.
     */
    void Build(CSHOT_EFFECT &from) {
        Drop();

        alloc.base = frames;
        alloc.buffer = frames;
        alloc.limit = frame_quads;
        alloc.used = 0;

        data = *from.effect_data;
        data.target = kPlayerTarget;

        for (int slot = 0; slot < kBorrowedSlots; slot++) {
            effect.chara[slot] = from.template_chara;
            effect.chara[slot].motion[0] = &effect.chara[slot].motion_type;
            effect.chara[slot].frame = CopyFrameVu1(static_cast<CFrameVu1 *>(from.template_chara.frame), &alloc);
        }

        effect.slot_count = kBorrowedSlots;
        effect.texture_block = from.texture_block;
        effect.effect_data = &data;

        ready = true;
        source = &from;
        source_data = from.effect_data;
        unit = NowMonstorUnit;
        floor = UserStatus->cur_floor;
        map = selectMapNo;
    }

    /**
     * Fires one from origin along direction for owner; false where no slot is free.
     */
    bool Fire(const float *origin, const float *direction, int damage, int owner) {
        sceVu0FVECTOR position = {origin[0], origin[1], origin[2], 1.0f};
        sceVu0FVECTOR target = {origin[0] + direction[0], origin[1] + direction[1], origin[2] + direction[2], 1.0f};

        if (effect.Set(position, target, owner, -1, 0, nullptr, -1) == -1) {
            return false;
        }

        // The shot keeps its own element and ailments; the weapon's abilities and monster
        // bonuses ride along as on any of the character's shots.
        effect.SetWepStatus(NowWeaponHave->flags);
        effect.SetVsMonster(NowWeaponHave->vs_monster);
        effect.SetDmg(damage);
        effect.SetWait(kBorrowedHitWait);
        return true;
    }
};

alignas(64) u_char g_stolen_frames[1024 * 1024];
alignas(64) u_char g_breath_frames[1024 * 1024];
BorrowedShot       g_stolen(g_stolen_frames, sizeof(g_stolen_frames));
BorrowedShot       g_breath(g_breath_frames, sizeof(g_breath_frames));

/**
 * The loaded effect a monster's projectile fires from, or null where it has none that flies.
 */
CSHOT_EFFECT *Projectile(int entry) {
    if (NowShotEffect == nullptr || entry < 0 || entry >= kShotEffectSlots) {
        return nullptr;
    }

    CSHOT_EFFECT &effect = NowShotEffect->effect[entry];

    if (effect.effect_data == nullptr || effect.template_chara.frame == nullptr) {
        return nullptr;
    }

    for (const int burst : kSelfBursts) {
        if (effect.effect_data == BtEntryEffectTbl[burst]) {
            return nullptr;
        }
    }

    return &effect;
}

/**
 * The floor's loaded effect for one of the game's shot descriptions, or null when no monster on
 * the floor fires it.
 */
CSHOT_EFFECT *LoadedEffect(const BT_SHOT_EFFECT *description) {
    if (NowShotEffect == nullptr || description == nullptr) {
        return nullptr;
    }

    for (CSHOT_EFFECT &effect : NowShotEffect->effect) {
        if (effect.effect_data == description && effect.template_chara.frame != nullptr) {
            return &effect;
        }
    }

    return nullptr;
}

// ── Xiao and Ruby: Steal Shot ───────────────────────────────────────────────────────────────

// A steal with the Bandit Slingshot or Bandit's Ring takes the victim's projectile: from then
// until the next steal or the floor's end, Xiao's pellets and Ruby's quick shots are that shot,
// at twice the damage.
constexpr float kStolenDamage = 2.0f;

bool StealsShots(int owner) {
    const int held = EffectOf(owner);

    // Super Steve takes either thief's sphere.
    return (owner == CHARA_XIAO && (held == ITEM_WEAPON_BANDIT_SLINGSHOT || held == ITEM_WEAPON_BANDIT_S_RING)) ||
           (owner == CHARA_RUBY && held == ITEM_WEAPON_BANDIT_S_RING);
}

// ── Xiao: the pellets ───────────────────────────────────────────────────────────────────────

// What the mod knows of each of Xiao's twelve pellet slots.
struct Pellet {
    bool  bounced = false;     // a ricochet, which never ricochets again
    int   depart_from = -1;    // the monster a ricochet is still leaving, or -1
    int   depart_steps = 0;    // steps it has spent leaving
    float kick_back = 0.0f;    // a charged pellet's kick, from this far behind it; 0 for none
    bool  crush = false;       // a Charging Bull pellet, which passes guards
    int   sword = -1;          // Super Steve: the sword whose move a charged pellet carries
    float wind = -1.0f;        // Super Steve with Heaven's Cloud: the burst's charge, 0 to 1, or -1
};

std::array<Pellet, 12> g_pellets;

// The charged shot: held this many steps, then released within the arm window.
constexpr int   kChargeSteps = 60;
constexpr int   kChargeArmSteps = 60;
constexpr float kChargedDamage = 1.5f;
constexpr float kKickSpeed = 2.5f;
constexpr float kKickDecay = 0.1f;

bool g_aim_holding = false;
int  g_aim_steps = 0;
bool g_aim_charged = false;
int  g_charge_armed = 0;
int  g_charge_hold = 0; // steps the shot that armed was held

// Dark Cloud Expanded: Super Steve's shot versions of the other weapons' charges.
// The Halberd line's (Halberd.cs DriveSphere): half a second held; the pellet flies the form's
// travel faster, drawn its size larger, at 1.5x the attack.
constexpr int kTornadoShotSteps = kStepsPerSecond / 2;
// Heaven's Cloud's two stages (HeavensCloudSphere.cs): an eighth of three seconds empowers the
// shot, the whole three is its most. Empowered, the pellet grows to 8x and 1.5x the attack, and
// bursts where it strikes in a wind blast of 60 to 160 units for 0.75x the attack, throwing
// everything clear from a point pulled back toward Xiao.
constexpr int   kCloudFullSteps = 3 * kStepsPerSecond;
constexpr int   kCloudFirstSteps = kCloudFullSteps / 8;
constexpr int   kCloudMostSteps = kCloudFullSteps * 98 / 100;
constexpr float kCloudPelletMost = 8.0f;
constexpr float kCloudDamageMost = 1.5f;
constexpr float kCloudWindLeast = 60.0f;
constexpr float kCloudWindMost = 160.0f;
constexpr float kCloudWindDamage = 0.75f;
constexpr float kCloudWindPullback = 5.0f;
constexpr float kCloudWindKick = 6.0f;
constexpr float kCloudWindDecay = 0.25f;
// Mobius Ring's ramp (MobiusRingSphere.cs): every second and a half held, x1.5 more, the pellet
// growing with it up to 15x.
constexpr int   kMobiusShotCycle = kStepsPerSecond * 3 / 2;
constexpr float kMobiusShotStep = 1.5f;
constexpr float kMobiusPelletMost = 15.0f;
constexpr int   kMobiusShotDamageMost = 65535;
// Moonlit Focus (TsukikageSphere.cs): every pellet leaves at twice its speed.
constexpr float kMoonlitShotSpeed = 2.0f;

// Dragon's Breath: the Gemron shot for each element, Fire to Holy, then the Black Dragon's.
constexpr int   kBreathShots[] = {5, 20, 23, 24, 25, 22};
constexpr float kBreathPelletSize = 5.0f;
constexpr float kBreathKickBack = 4.0f;

// Charging Bull.
constexpr float kBullPelletSize = 2.0f;
constexpr float kBullKickBack = 30.0f;

// Double Impact: two pellets side by side, each at three quarters of the shot's attack.
constexpr float kTwinDamage = 0.75f;
constexpr float kTwinWidth = 1.0f;

// Ricochet.
constexpr float kPelletRadius = 2.0f;
constexpr float kRicochetRange = 120.0f;
constexpr float kRicochetSpeed = 4.0f;
constexpr int   kRicochetDepartSteps = 30;

// Charging Bull hits planted this step, which pass the guard window.
struct CrushHit {
    int hit = -1;
    int damage = 0;
    int step = -1;
};

std::array<CrushHit, 12> g_crush_hits;

/**
 * Starts a pellet in the first free slot, as the game does; the slot, or -1 when none is free.
 */
int SpawnPellet(CSHOT *shots, const float *world, const float *vector, int damage, float size) {
    for (int i = 0; i < 12; i++) {
        if (shots->used[i] == 0) {
            for (int axis = 0; axis < 3; axis++) {
                shots->pos[i][axis] = world[axis];
                shots->vector[i][axis] = vector[axis];
            }

            shots->pos[i][3] = 1.0f;
            shots->vector[i][3] = 0.0f;
            shots->life[i] = 0x78;
            shots->damage[i] = damage;
            shots->size[i] = size;
            shots->halted[i] = 0;
            shots->start_flag[i] = 0;
            shots->used[i] = 1;
            g_pellets[i] = {};
            return i;
        }
    }

    return -1;
}

bool MonsterAlive(int m) {
    const MONSTOR &monster = NowMonstorUnit->monster[m];
    return monster.state != -1 && monster.revealed != 0 && monster.hp > 0;
}

float Distance(const float *a, const float *b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * Whether a point lies within reach of one of a monster's live hit spheres, the pellet's own
 * collision rule.
 */
bool InsideMonster(int m, const float *position, float reach) {
    const auto &effect = NowMonstorUnit->effect[m];

    for (int sphere = 0; sphere < 16; sphere++) {
        if (effect.timer[sphere] != 0 && Distance(effect.position[sphere], position) <= reach + effect.radius[sphere]) {
            return true;
        }
    }

    return false;
}

/**
 * The living monster a point lies inside, but except; -1 for none.
 */
int MonsterAt(const float *position, float reach, int except) {
    if (NowMonstorUnit == nullptr) {
        return -1;
    }

    for (int m = 0; m < 16; m++) {
        if (m != except && MonsterAlive(m) && InsideMonster(m, position, reach)) {
            return m;
        }
    }

    return -1;
}

/**
 * The living monster nearest the one at from, within range of it, and the point to aim at: its
 * live hit sphere nearest position. -1 for none.
 */
int NextTarget(int from, const float *position, float *aim) {
    sceVu0FVECTOR from_pos;
    NowMonstorUnit->chara[from][0].GetPosition(from_pos);

    int   best = -1;
    float best_distance = kRicochetRange;

    for (int m = 0; m < 16; m++) {
        if (m == from || !MonsterAlive(m)) {
            continue;
        }

        sceVu0FVECTOR pos;
        NowMonstorUnit->chara[m][0].GetPosition(pos);
        const float distance = Distance(pos, from_pos);

        if (distance < best_distance) {
            best_distance = distance;
            best = m;
            aim[0] = pos[0];
            aim[1] = pos[1];
            aim[2] = pos[2];
        }
    }

    if (best >= 0) {
        const auto &effect = NowMonstorUnit->effect[best];
        float       nearest = -1.0f;

        for (int sphere = 0; sphere < 16; sphere++) {
            if (effect.timer[sphere] != 0) {
                const float distance = Distance(effect.position[sphere], position);

                if (nearest < 0.0f || distance < nearest) {
                    nearest = distance;
                    aim[0] = effect.position[sphere][0];
                    aim[1] = effect.position[sphere][1];
                    aim[2] = effect.position[sphere][2];
                }
            }
        }
    }

    return best;
}

/**
 * Dragon's Breath: the element's Gemron shot, when a monster on the floor has it loaded.
 */
bool BreathFire(const float *world, const float *vector, int damage) {
    const int element = std::clamp<int>(NowWeaponHave->best_elem, 0, 5);
    const BT_SHOT_EFFECT *wanted = BtEntryEffectTbl[kBreathShots[element]];

    if (!g_breath.Valid() || g_breath.source_data != wanted) {
        CSHOT_EFFECT *loaded = LoadedEffect(wanted);

        if (loaded == nullptr) {
            return false;
        }

        g_breath.Build(*loaded);
    }

    return g_breath.Fire(world, vector, damage, CHARA_XIAO);
}

// ── Ruby: Mobius Ring ───────────────────────────────────────────────────────────────────────

// Once the charge is full, each second and a half it is held multiplies the shot's damage by
// 1.5, from attack plus magic, to at most 65,535. The held ball and the orbs it throws grow with
// the multiplier, up to five times their size, and hit as wide as they are drawn.
constexpr float kMobiusFull = 60.0f;
constexpr int   kMobiusPeriod = 90;
constexpr float kMobiusStep = 1.5f;
constexpr float kMobiusMaxScale = 5.0f;
constexpr int   kMobiusMaxDamage = 65535;

float               g_mobius_mult = 1.0f;
int                 g_mobius_steps = 0;
const CSHOT_EFFECT *g_mobius_effect = nullptr;
float               g_mobius_radius[8] = {1, 1, 1, 1, 1, 1, 1, 1};
int                 g_radius_chara = -1;

/**
 * The slot scales belong to chara from here: another character's are dropped.
 */
void RadiusOwner(const CSHOT_EFFECT *effect, int chara) {
    if (g_mobius_effect != effect || g_radius_chara != chara) {
        std::ranges::fill(g_mobius_radius, 1.0f);
    }

    g_mobius_effect = effect;
    g_radius_chara = chara;
}

int MobiusDamage() {
    const float base = static_cast<float>(NowWeaponHave->attack + NowWeaponHave->magic);
    return static_cast<int>(std::min(base * g_mobius_mult, static_cast<float>(kMobiusMaxDamage)));
}

float MobiusScale() {
    return std::clamp(g_mobius_mult, 1.0f, kMobiusMaxScale);
}

void SetSlotScale(CSHOT_EFFECT *effect, int slot, float scale) {
    float size[4] = {scale, scale, scale, 1.0f};
    effect->chara[slot].SetScale(size);
}

// ── Chronicle Sword ─────────────────────────────────────────────────────────────────────────

// Each of Toan's hits reaches every other enemy within 300 units: half its damage within 50,
// then a share falling with distance, down to a hundredth. The splash never kills: an enemy it
// would finish is left on 1 HP and poisoned. Once a step, however many the hit itself lands on.
constexpr float kSplashRange = 300.0f;
constexpr float kSplashNear = 50.0f;

int g_splash_step = -1;

void ChronicleSplash(const MONSTOR &hit, int owner, int amount) {
    if (owner != CHARA_TOAN || amount <= 0 || NowMonstorUnit == nullptr || ToanWeapon() != ITEM_WEAPON_CHRONICLE_SWORD ||
        g_splash_step == g_step_count) {
        return;
    }

    g_splash_step = g_step_count;

    for (int m = 0; m < 16; m++) {
        MONSTOR &other = NowMonstorUnit->monster[m];
        const float distance = other.player_distance;

        if (&other == &hit || other.state != 2 || other.hp <= 0 || distance <= 0.0f || distance >= kSplashRange) {
            continue;
        }

        const float share = distance < kSplashNear ? 0.5f : std::max(1.0f, (kSplashRange - distance) / 5.0f) / 100.0f;
        const int   damage = static_cast<int>(std::floor(static_cast<float>(amount) * share));

        if (damage <= 0) {
            continue;
        }

        if (other.hp - damage < 1) {
            other.hp = 1;
            other.poison_timer = 1;
        } else {
            other.hp -= damage;
        }

        sceVu0FVECTOR position;
        NowMonstorUnit->chara[m][0].GetPosition(position);
        position[1] += 10.0f;
        HitValueEntry(NowHitValue, position, damage, HIT_VALUE_MONSTER, nullptr);

        // A flash of the poison's colour on each enemy the splash reaches.
        constexpr float kSplashFlash[3] = {110.0f, 60.0f, 140.0f};
        constexpr int   kSplashFlashSteps = 20;
        fx::MonsterTint(m, kSplashFlash, kSplashFlashSteps);
    }
}

/**
 * Whether a monster is one of the ice kind the Frozen Tuna's frost leaves alone: Blizzard,
 * Sam and the Ice Gemron.
 */
bool IceMonster(const MONSTOR &monster) {
    return monster.name_no == 65 || monster.name_no == 85 || monster.name_no == 312;
}

// ── Confuse ─────────────────────────────────────────────────────────────────────────────────

// A weapon ability beside Poison and Stop: one hit in twenty (never on a boss) confuses the enemy
// for 20 seconds. A confused enemy goes after whatever is nearest, another enemy or the player,
// and its blows hurt the enemies they reach. An enemy it hits fights back: it goes after its most
// recent attacker still confused, then back to the player once none is left. Native to Babel's
// Spear; a SynthSphere carries it like any other ability.
constexpr int kConfuseFlag = 1 << kConfuseAbilityBit;
constexpr int kConfusePercent = 5;
constexpr int kConfuseSteps = 20 * kStepsPerSecond;
constexpr int kFriendlyCooldownSteps = kStepsPerSecond / 2;
constexpr int kFriendlyHitLife = 2;
constexpr int kHitPoolSize = 96;
constexpr int kHitPoolReserve = 16;
constexpr int kConfuseNameMessage = kMessageBase + 1000;

/**
 * The confused enemies that have hit one enemy, most recent first.
 */
struct HitByList {
    std::array<s8, 16> slot{};
    int                count = 0;
};

/**
 * A hit planted for a confused or provoked enemy's blow, which only the enemies it may hurt take.
 */
struct FriendlyHit {
    int hit = -1;
    int attacker = -1;
    int step = -1;
};

std::array<int, 16>                 g_confused_until{};
std::array<HitByList, 16>           g_hit_by{};
std::array<std::array<int, 16>, 16> g_friendly_ready{};
std::array<FriendlyHit, 32>         g_friendly{};
int                                 g_friendly_next = 0;

void StarsReset();

void ConfuseReset() {
    StarsReset();
    g_confused_until = {};
    g_hit_by = {};
    g_friendly_ready = {};
    g_friendly = {};
    g_friendly_next = 0;
}

// ── Ruby's Pocklekul "Pockle Call" ──────────────────────────────────────────────────────────

// Ruby's guard held a second summons one of the eight pockles at random (the seven days' and Hell
// Pockle) in front of her; so does Xiao's, while Super Steve carries the Pocklekul's sphere. The
// pockle is a real monster of the floor: its kind is loaded into a free kind slot the first time
// it comes on a floor and it runs its own script, but it fights for the party. It goes for the
// locked-on enemy, else the one nearest its summoner, and walks beside her when there is none.
// Its blows only the enemies take, and an enemy it hits fights back; the party's own hits pass
// through it, and it cannot be locked on to. It grows in over its first frames. One stands at a
// time; it goes when the weapon or the character changes, when it falls, when it is left 300
// units behind, and with the floor. It gives no ABS and drops nothing.
// It appears in the summon smoke, lit white as it grows in. Left out against Expanded: the
// pockles' thrown shots (their shots can only strike the party, so a summoned pockle does not
// shoot).

constexpr std::array<int, 8>          kPockleKinds = {10, 11, 12, 13, 14, 15, 16, 54}; // MonstorTable: Sunday .. Saturday, Hell Pockle
constexpr std::array<const char *, 8> kPockleNames = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                                      "Thursday", "Friday", "Saturday", "Hell Pockle"};
constexpr int   kPockleGuardSteps = kStepsPerSecond;
constexpr int   kPockleGrowSteps = 18;
constexpr float kPockleGrowFrom = 0.1f;
constexpr float kPockleLeftBehind = 300.0f;
constexpr float kPockleSeek = 300.0f;
constexpr float kPockleBeside = 18.0f;
constexpr float kPockleAhead = 20.0f;
constexpr int   kPockleModelRoom = 384 * 1024 / 16; // quads the floor's model buffer must have left

struct Pockle {
    int  monster = -1;  // its slot on the floor, or -1
    int  summoner = -1; // the character who called it
    int  age = 0;       // steps since it came
    int  guard = 0;     // steps the guard has been held
    bool cast = false;  // this guard has been spent
};

Pockle g_pockle;
bool   g_quiet_load = false; // a pockle's kind is loading: the floor's own state stays

bool IsPocklePos(int m) {
    return g_pockle.monster >= 0 && m == g_pockle.monster;
}

bool IsPockle(const MONSTOR &monster) {
    return g_pockle.monster >= 0 && NowMonstorUnit != nullptr && &monster == &NowMonstorUnit->monster[g_pockle.monster];
}

/**
 * The pockle is on the floor and alive.
 */
bool PockleUp() {
    if (g_pockle.monster < 0 || NowMonstorUnit == nullptr) {
        return false;
    }

    const MONSTOR &monster = NowMonstorUnit->monster[g_pockle.monster];
    return monster.state > 0 && monster.hp > 0;
}

/**
 * An enemy that is up and fighting. The pockle is not one: nothing the party does seeks it out.
 */
bool ActiveMonster(int m) {
    const MONSTOR &monster = NowMonstorUnit->monster[m];
    return monster.state == 2 && monster.hp > 0 && monster.revealed != 0 && !IsPocklePos(m);
}

bool Confused(int m) {
    // The pockle fights the enemies for as long as it stands, as a confused one does.
    if (IsPocklePos(m)) {
        return PockleUp();
    }

    return NowMonstorUnit != nullptr && m >= 0 && m < 16 && g_confused_until[m] > g_step_count && ActiveMonster(m);
}

/**
 * The confused enemy an enemy that is not confused goes after: its most recent attacker still
 * confused. -1 for none.
 */
int Provoker(int m) {
    if (NowMonstorUnit == nullptr || m < 0 || m >= 16 || Confused(m) || !ActiveMonster(m)) {
        return -1;
    }

    const HitByList &list = g_hit_by[m];

    for (int i = 0; i < list.count; i++) {
        if (Confused(list.slot[i])) {
            return list.slot[i];
        }
    }

    return -1;
}

bool Attacks(int m) {
    return Confused(m) || Provoker(m) >= 0;
}

/**
 * Whether attacker's blow may hurt victim: a confused enemy's hurts any other enemy, a provoked
 * one's only its provoker.
 */
bool MayHit(int attacker, int victim) {
    return attacker != victim && (Confused(attacker) || Provoker(attacker) == victim);
}

void Provoke(int victim, int attacker) {
    HitByList &list = g_hit_by[victim];
    int        kept = 0;

    for (int i = 0; i < list.count; i++) {
        if (list.slot[i] != attacker) {
            list.slot[kept++] = list.slot[i];
        }
    }

    list.count = kept;

    for (int i = std::min(list.count, 15); i > 0; i--) {
        list.slot[i] = list.slot[i - 1];
    }

    list.slot[0] = static_cast<s8>(attacker);
    list.count = std::min(list.count + 1, 16);
}

void MonsterPosition(int m, float *out) {
    NowMonstorUnit->chara[m][0].GetPosition(out);
}

/**
 * What a confused enemy goes after: the nearest active enemy on the ground plane, or -1 for the
 * player when the player is nearer.
 */
int ConfusedTarget(int m) {
    float me[4];
    MonsterPosition(m, me);

    auto flat = [&me](const float *at) {
        const float dx = at[0] - me[0];
        const float dz = at[2] - me[2];
        return dx * dx + dz * dz;
    };

    int   best = -1;
    float best_distance = flat(CharaMain.pos);

    for (int other = 0; other < 16; other++) {
        if (other == m || !ActiveMonster(other)) {
            continue;
        }

        float at[4];
        MonsterPosition(other, at);
        const float distance = flat(at);

        if (distance < best_distance) {
            best = other;
            best_distance = distance;
        }
    }

    return best;
}

/**
 * The point to go for on victim: its live hit sphere nearest from, else its position. A large
 * enemy's position is deeper in its body than an attacker's approach reaches.
 */
void AimAt(int victim, const float *from, float *out) {
    MonsterPosition(victim, out);

    const auto &spheres = NowMonstorUnit->effect[victim];
    float       best = -1.0f;

    for (int sphere = 0; sphere < 16; sphere++) {
        if (spheres.timer[sphere] == 0) {
            continue;
        }

        const float distance = Distance(spheres.position[sphere], from);

        if (best < 0.0f || distance < best) {
            best = distance;
            out[0] = spheres.position[sphere][0];
            out[1] = spheres.position[sphere][1];
            out[2] = spheres.position[sphere][2];
        }
    }

    out[3] = 1.0f;
}

const FriendlyHit *FindFriendly(int hit) {
    for (const FriendlyHit &friendly : g_friendly) {
        if (friendly.hit == hit && friendly.step >= g_step_count - kFriendlyHitLife) {
            return &friendly;
        }
    }

    return nullptr;
}

/**
 * The next hit from index from on that reaches a sphere, by CCollisionData::FindMonsterHit's own
 * rule. -1 for none.
 */
int NextMonsterHit(int from, const float *position, float radius) {
    for (int i = from; i < kHitPoolSize; i++) {
        const COLLISION_HIT &record = NowColData->hit[i];

        if (NowColData->active[i] != 0 && (record.target_mask & 2) && record.phase == record.ready_phase &&
            Distance(position, record.pos) <= radius + record.radius) {
            return i;
        }
    }

    return -1;
}

/**
 * Clears the confusion that has run out and forgets the attackers that came to their senses.
 */
// The dizzy stars spin over the confused enemies nearest the player, a few at a time.
constexpr int   kStarsMost = 6;
constexpr float kStarsLift = 2.0f;
constexpr float kStarsScale = 1.0f;

std::array<int, 16> g_stars;

void StarsReset() {
    g_stars.fill(-1);
}

void StarsStep() {
    std::array<std::pair<float, int>, 16> near;
    int                                   count = 0;

    for (int m = 0; m < 16; m++) {
        if (ActiveMonster(m) && Confused(m)) {
            near[count++] = {NowMonstorUnit->monster[m].player_distance, m};
        }
    }

    std::sort(near.begin(), near.begin() + count);
    std::array<bool, 16> shown{};

    for (int i = 0; i < std::min(count, kStarsMost); i++) {
        shown[near[i].second] = true;
    }

    for (int m = 0; m < 16; m++) {
        if (!shown[m]) {
            if (g_stars[m] >= 0) {
                fx::Fade(g_stars[m]);
                g_stars[m] = -1;
            }

            continue;
        }

        float top[4];
        MonsterTop(m, top);
        top[1] += kStarsLift;

        if (!fx::Live(g_stars[m])) {
            g_stars[m] = fx::Play(fx::Prop::Stars, 0, top, kStarsScale, 0.0f, true);
        }

        fx::Move(g_stars[m], top);
    }
}

void ConfuseStep() {
    if (NowMonstorUnit == nullptr) {
        return;
    }

    StarsStep();

    for (int m = 0; m < 16; m++) {
        if (g_confused_until[m] != 0 && !Confused(m)) {
            g_confused_until[m] = 0;
        }
    }

    for (HitByList &list : g_hit_by) {
        int kept = 0;

        for (int i = 0; i < list.count; i++) {
            if (Confused(list.slot[i])) {
                list.slot[kept++] = list.slot[i];
            }
        }

        list.count = kept;
    }
}

// ── The pockle's summon and its life ────────────────────────────────────────────────────────

/**
 * Who would call a pockle now: Ruby holding the Pocklekul, or Xiao with its sphere on Super
 * Steve. -1 for nobody.
 */
int PockleSummoner() {
    if (UserStatus == nullptr) {
        return -1;
    }

    const int chara = UserStatus->cur_chara;

    if ((chara == CHARA_RUBY || chara == CHARA_XIAO) && EffectOf(chara) == ITEM_WEAPON_POCKLEKUL) {
        return chara;
    }

    return -1;
}

/**
 * Takes the pockle off the floor, as the floor's clean-up does for every monster.
 */
void PockleEnd() {
    if (g_pockle.monster >= 0 && NowMonstorUnit != nullptr) {
        CMonstorUnit &unit = *NowMonstorUnit;
        const int     m = g_pockle.monster;
        MONSTOR      &monster = unit.monster[m];

        if (monster.state > 0) {
            if (monster.hp > 0) {
                unit.alive_count--;
            }

            monster.state = -1;
            unit.script_state[m] = 0;

            for (int j = 0; j < 16; j++) {
                unit.effect[m].timer[j] = 0;
                unit.effect2[m].active[j] = 0;
            }

            for (int j = 0; j < 12; j++) {
                unit.effect3[m].frame[j] = nullptr;
                unit.effect3[m].timer[j] = 0;
            }

            unit.effect3[m].count = 0;
            unit.event[m].frame = nullptr;
            unit.event[m].timer = 0;
            unit.event2[m].frame = nullptr;
            unit.event2[m].timer = 0;
        }

        g_hit_by[m] = {};
    }

    g_pockle.monster = -1;
    g_pockle.summoner = -1;
    g_pockle.age = 0;
}

void PockleReset() {
    // A new floor's clean-up has taken it already.
    g_pockle = {};
}

/**
 * The floor's kind slot holding this pockle, loading it into a free one the first time it comes
 * on the floor. -1 when there is no room.
 */
int PockleKindSlot(int kind) {
    CMonstorUnit        &unit = *NowMonstorUnit;
    MONSTOR_MODEL       &description = MonstorTable[kind];

    for (int slot = 0; slot < unit.model_count && slot < 9; slot++) {
        if (unit.model[slot].name_no == description.name_no && unit.script_data[slot] != nullptr) {
            return slot;
        }
    }

    if (unit.model_count >= 9) {
        std::printf("[Expanded] Pocklekul: no free monster kind slot on this floor\n");
        return -1;
    }

    if (MonstorModelBuffer.limit > 0 && MonstorModelBuffer.limit - MonstorModelBuffer.used < kPockleModelRoom) {
        std::printf("[Expanded] Pocklekul: the floor's model buffer is full (%d of %d)\n", MonstorModelBuffer.used,
                    MonstorModelBuffer.limit);
        return -1;
    }

    // Loaded without its shots: a monster's shots only ever strike the party.
    const s16 shots[2] = {description.shot_effect[0], description.shot_effect[1]};
    description.shot_effect[0] = -1;
    description.shot_effect[1] = -1;

    const int slot = unit.model_count;
    g_quiet_load = true;
    unit.SetupBaseModel(slot, kind, 0x26, &MonstorModelBuffer);
    g_quiet_load = false;

    description.shot_effect[0] = shots[0];
    description.shot_effect[1] = shots[1];

    if (unit.model_count != slot + 1 || unit.script_data[slot] == nullptr) {
        std::printf("[Expanded] Pocklekul: kind %d did not load\n", kind);
        return -1;
    }

    std::printf("[Expanded] Pocklekul: kind %d loaded into slot %d (model buffer %d of %d)\n", kind, slot,
                MonstorModelBuffer.used, MonstorModelBuffer.limit);
    return slot;
}

void PockleSummon(int summoner) {
    CMonstorUnit &unit = *NowMonstorUnit;
    const int     pick = std::rand() % static_cast<int>(kPockleKinds.size());
    const int     slot = PockleKindSlot(kPockleKinds[pick]);

    if (slot < 0) {
        return;
    }

    float at[4] = {CharaMain.pos[0] + std::sin(CharaMain.rotation.y) * kPockleAhead, CharaMain.pos[1],
                   CharaMain.pos[2] + std::cos(CharaMain.rotation.y) * kPockleAhead, 1.0f};

    if (unit.SetupViewMonstor(slot, at, 0) == 0) {
        std::printf("[Expanded] Pocklekul: no free monster slot on this floor\n");
        return;
    }

    const int m = unit.current_monster;
    MONSTOR  &monster = unit.monster[m];

    // Nothing to win from it, and nothing to lock on to.
    monster.exp = 0;
    monster.money = 0;
    monster.money_chance = 0;
    monster.drops_items = 0;
    monster.rare_item = -1;
    monster.steal_item = -1;
    monster.kind = MONSTER_KIND_NO_LOCK_ON;
    monster.lockon_enabled = false;
    monster.shot_effect = -1;
    monster.shot_effect2 = -1;

    unit.chara[m][0].SetRotation(0.0f, CharaMain.rotation.y, 0.0f);
    unit.chara[m][0].SetScale(kPockleGrowFrom, kPockleGrowFrom, kPockleGrowFrom);

    g_pockle.monster = m;
    g_pockle.summoner = summoner;
    g_pockle.age = 0;
    g_hit_by[m] = {};

    setUnitAmbientAnime(15.0f, 1.0f, 150.0f, 100.0f, 120.0f);
    ShowDungeonMessage(std::format("{} summoned", kPockleNames[pick]), 150);

    // It appears in a puff of smoke, lit white as it grows in.
    constexpr float kSummonTint[3] = {120.0f, 120.0f, 120.0f};
    constexpr int   kSummonTintSteps = 30;
    fx::Play(fx::Prop::Smoke, 0, at, kSmokeScale, CharaMain.rotation.y, false, kSmokeRate);
    fx::MonsterTint(m, kSummonTint, kSummonTintSteps);
}

/**
 * What the pockle goes for: the locked-on enemy, else the one nearest its summoner. -1 for none.
 */
int PockleTarget() {
    const int locked = LockedTarget();

    if (locked >= 0 && ActiveMonster(locked)) {
        return locked;
    }

    int   best = -1;
    float best_distance = kPockleSeek * kPockleSeek;

    for (int m = 0; m < 16; m++) {
        if (!ActiveMonster(m)) {
            continue;
        }

        float at[4];
        MonsterPosition(m, at);
        const float dx = at[0] - CharaMain.pos[0];
        const float dz = at[2] - CharaMain.pos[2];
        const float distance = dx * dx + dz * dz;

        if (distance < best_distance) {
            best_distance = distance;
            best = m;
        }
    }

    return best;
}

/**
 * The point the pockle's script moves to: its enemy, or beside its summoner.
 */
void PockleAim(float *target) {
    const int victim = PockleTarget();

    if (victim >= 0) {
        float me[4];
        MonsterPosition(g_pockle.monster, me);
        AimAt(victim, me, target);
        return;
    }

    const float yaw = CharaMain.rotation.y;
    target[0] = CharaMain.pos[0] + std::cos(yaw) * kPockleBeside;
    target[1] = CharaMain.pos[1];
    target[2] = CharaMain.pos[2] - std::sin(yaw) * kPockleBeside;
    target[3] = 1.0f;
}

void PockleStep() {
    if (NowMonstorUnit == nullptr || UserStatus == nullptr) {
        return;
    }

    const int summoner = PockleSummoner();

    // It stands while its summoner is out with the weapon, alive, and near.
    if (g_pockle.monster >= 0) {
        bool ends = summoner < 0 || summoner != g_pockle.summoner || !PockleUp();

        if (!ends) {
            float at[4];
            MonsterPosition(g_pockle.monster, at);
            const float dx = at[0] - CharaMain.pos[0];
            const float dz = at[2] - CharaMain.pos[2];

            if (dx * dx + dz * dz > kPockleLeftBehind * kPockleLeftBehind) {
                std::printf("[Expanded] Pocklekul: the pockle was left behind\n");
                ends = true;
            }
        }

        if (ends) {
            // One that fell is the game's to clear away.
            if (PockleUp()) {
                PockleEnd();
            } else {
                g_pockle.monster = -1;
                g_pockle.summoner = -1;
            }
        } else if (g_pockle.age < kPockleGrowSteps) {
            g_pockle.age++;
            const float share = static_cast<float>(g_pockle.age) / kPockleGrowSteps;
            const float size = kPockleGrowFrom + (1.0f - kPockleGrowFrom) * share;
            NowMonstorUnit->chara[g_pockle.monster][0].SetScale(size, size, size);
        }
    }

    if (summoner < 0 || BtActStatus.guard_mode != kGuarding) {
        g_pockle.guard = 0;
        g_pockle.cast = false;
        return;
    }

    // The guard's charge: a second, then the flash and the summon unless one stands.
    if (g_pockle.cast || ++g_pockle.guard < kPockleGuardSteps) {
        return;
    }

    g_pockle.cast = true;

    if (g_pockle.monster < 0) {
        PockleSummon(summoner);
    }
}

// ── Ungaga: what rises from the ground and what falls on it ─────────────────────────────────

// Three guard charges leave something standing on the floor: Babel's Spear and the Cactus raise
// a giant copy of themselves point-up out of the ground, and the Terra Sword drops a boulder on
// the locked-on enemy. Each stands as a solid column for a while: enemies walk around it and the
// player is held out of it, and what touches it is hit. One of each kind stands at a time; all go
// with a weapon or party change and a new floor. Babel's Spear and the Cactus stop enemy shots.
// Left out against Expanded: the Terra Sword's shadow, and a copy sinks away instead of fading.

/**
 * A model drawn on the floor in place of where it belongs: the held weapon's frames laid out by a
 * world matrix of the mod's own for one draw, then put back on the hand.
 */
struct FloorModel {
    CFrame *root = nullptr;
    int     block = -1;       // the texture block to reload before drawing
    int     axis = 1;         // the model axis that points up
    float   sign = 1.0f;      // +1: the tip is toward the axis's max
    float   tip = 0.0f;       // the tip's distance from the root along that axis
    bool    weapon = false;   // the live weapon: put back on the hand after drawing

    /**
     * Takes the held weapon, finding its long axis from its bound box.
     */
    bool TakeWeapon(CCharacter *held) {
        root = nullptr;

        if (held == nullptr || held->frame == nullptr) {
            return false;
        }

        CFrame     *frame = held->frame;
        CFrame     *parent = frame->parent;
        const int   reference = frame->reference;
        CBoxVu0     box;

        frame->parent = nullptr;
        frame->reference = 0;
        frame->GetBoundBox(&box, 1);
        frame->parent = parent;
        frame->reference = reference;

        axis = 0;

        for (int i = 1; i < 3; i++) {
            if (box.max[i] - box.min[i] > box.max[axis] - box.min[axis]) {
                axis = i;
            }
        }

        sign = std::fabs(box.max[axis]) >= std::fabs(box.min[axis]) ? 1.0f : -1.0f;
        tip = sign > 0.0f ? box.max[axis] : -box.min[axis];
        root = frame;
        block = 0x1D;
        weapon = true;
        return true;
    }

    bool Valid() const {
        return root != nullptr && (!weapon || (NowWeapon != nullptr && NowWeapon->frame == root));
    }

    static void Invalidate(CFrame *frame) {
        for (; frame != nullptr; frame = frame->brother) {
            frame->world_valid = false;
            Invalidate(frame->child);
        }
    }

    /**
     * Draws it with its root at x, y, z, scaled, turned yaw about the vertical. A weapon is stood
     * on its tip axis; anything else is drawn as authored.
     */
    void Draw(float x, float y, float z, float scale, float yaw, bool point_down = false, float alpha = 1.0f) {
        if (!Valid() || scale <= 0.0f || alpha <= 0.0f) {
            return;
        }

        CFrame       *parent = root->parent;
        const int     reference = root->reference;
        const int     srt = root->srt;
        sceVu0FMATRIX local;
        std::memcpy(local, root->local, sizeof(local));

        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        sceVu0FMATRIX place = {};

        if (weapon) {
            // The tip axis to up; the two others to the ground plane, kept right-handed.
            const int   b = (axis + 1) % 3;
            const int   d = (axis + 2) % 3;
            const float up = point_down ? -sign : sign;
            const float flat_b[2] = {0.0f, 1.0f}; // to z
            const float flat_d[2] = {up, 0.0f};   // to x (mirrored with the tip)

            place[axis][1] = up * scale;
            place[b][0] = scale * (flat_b[0] * c + flat_b[1] * s);
            place[b][2] = scale * (-flat_b[0] * s + flat_b[1] * c);
            place[d][0] = scale * (flat_d[0] * c + flat_d[1] * s);
            place[d][2] = scale * (-flat_d[0] * s + flat_d[1] * c);
        } else {
            place[0][0] = scale * c;
            place[0][2] = -scale * s;
            place[1][1] = scale;
            place[2][0] = scale * s;
            place[2][2] = scale * c;
        }

        place[3][0] = x;
        place[3][1] = y;
        place[3][2] = z;
        place[3][3] = 1.0f;

        root->parent = nullptr;
        root->reference = 0;
        root->srt = 0;
        std::memcpy(root->local, place, sizeof(place));
        Invalidate(root);

        if (block >= 0) {
            TexManager.ReloadTexture(Vif1Packet, block);
        }

        // Drawn see-through through the ambient light's alpha, as a character's own is.
        float saved_ambient[4];
        MGGetAmbient(saved_ambient);

        if (alpha < 1.0f) {
            float ambient[4];
            std::memcpy(ambient, saved_ambient, sizeof(ambient));
            ambient[3] = 128.0f * alpha;
            MGSetAmbient(ambient);
        }

        MGDraw(root);

        if (alpha < 1.0f) {
            MGSetAmbient(saved_ambient);
        }

        root->parent = parent;
        root->reference = reference;
        root->srt = srt;
        std::memcpy(root->local, local, sizeof(local));
        Invalidate(root);
    }
};

// ── The judgement blade: Big Bang and the Sword of Zeus ─────────────────────────────────────

// While either sword is primed and locked on, a copy of it twice its size hangs point-down over
// the target, growing in, and follows it. The primed swing lets it fall; where it lands the flash
// goes off (Big Bang's blast or the Sword of Zeus's bolt with it). It shrinks away when the lock
// or the prime goes.
constexpr float kJudgementScale = 2.0f;
constexpr float kJudgementHover = 6.0f;     // between the target's head and the blade's tip
constexpr int   kJudgementGrowSteps = 15;
constexpr float kJudgementGravity = 0.4f;   // units a step, each step

struct Judgement {
    FloorModel model;
    bool       up = false;
    bool       falling = false;
    int        target = -1;
    int        item = -1;
    int        grow = 0;
    float      x = 0.0f;
    float      z = 0.0f;
    float      ground = 0.0f;
    float      tip_y = 0.0f;
    float      speed = 0.0f;
    float      yaw = 0.0f;
};

Judgement g_judgement;

void JudgementEnd() {
    g_judgement.up = false;
    g_judgement.falling = false;
    g_judgement.model.root = nullptr;
    g_judgement.target = -1;
}

/**
 * Over the target while it lives: its feet, and the hover height above its head.
 */
void JudgementFollow(float hover) {
    Judgement &blade = g_judgement;

    if (!ActiveMonster(blade.target)) {
        return;
    }

    float top[4];
    MonsterTop(blade.target, top);
    blade.x = top[0];
    blade.z = top[2];
    blade.ground = NowMonstorUnit->chara[blade.target][0].pos[1];

    if (hover >= 0.0f) {
        blade.tip_y = top[1] + hover;
    }
}

void JudgementStep(int held) {
    Judgement &blade = g_judgement;
    const bool toan_out = UserStatus->cur_chara == CHARA_TOAN;

    if (blade.up && (!toan_out || held != blade.item || !blade.model.Valid())) {
        JudgementEnd();
    }

    if (blade.falling) {
        JudgementFollow(-1.0f);
        blade.speed += kJudgementGravity;
        blade.tip_y -= blade.speed;

        if (blade.tip_y <= blade.ground) {
            const int target = blade.target;
            JudgementEnd();
            SolarRelease(target);
        }

        return;
    }

    const int  target = LockedTarget();
    const bool hang = toan_out && (held == ITEM_WEAPON_BIG_BANG || held == ITEM_WEAPON_SWORD_OF_ZEUS) && g_solar == Solar::Primed &&
                      target >= 0;

    if (hang) {
        if (!blade.up || blade.target != target) {
            if (!blade.model.TakeWeapon(NowWeapon)) {
                return;
            }

            blade.up = true;
            blade.grow = 0;
            blade.target = target;
            blade.item = held;
            blade.yaw = RandomYaw();
        }

        blade.grow = std::min(blade.grow + 1, kJudgementGrowSteps);
        JudgementFollow(kJudgementHover);
    } else if (blade.up && --blade.grow <= 0) {
        JudgementEnd();
    }
}

bool JudgementDrop() {
    Judgement &blade = g_judgement;

    if (!blade.up || blade.falling || blade.grow < kJudgementGrowSteps / 2 || !ActiveMonster(blade.target)) {
        return false;
    }

    blade.falling = true;
    blade.speed = 0.0f;
    blade.grow = kJudgementGrowSteps;
    return true;
}

void JudgementDraw() {
    Judgement &blade = g_judgement;

    if (!blade.up || UserStatus == nullptr || UserStatus->cur_chara != CHARA_TOAN || !blade.model.Valid()) {
        return;
    }

    // Point-down, so the grip sits the blade's length above the tip.
    const float grown = static_cast<float>(blade.grow) / kJudgementGrowSteps;
    const float scale = kJudgementScale * grown;
    const float grip_y = blade.tip_y + blade.model.tip * scale;
    blade.model.Draw(blade.x, grip_y, blade.z, scale, blade.yaw, true);

    // Dark Cloud Expanded: the sword's glow disc rides on the blade, halfway down its length,
    // growing in and shrinking away with it.
    if (const SwordLook *look = LookOf(blade.item)) {
        constexpr float kGlowShare = 0.6f;     // of the blade's length across
        constexpr float kGlowStrength = 0.7f;

        const float length = blade.model.tip * scale;
        const float at[3] = {blade.x, blade.tip_y + 0.5f * length, blade.z};
        fx::GlowDisc(at, kGlowShare * length, look->blade, kGlowStrength * grown);
    }
}

/**
 * Master Utan's boulder (gedit/s96/chara/iwa.chr), loaded the first time the Terra Sword primes
 * and kept for the session. Its texture goes in block 0x30, which nothing else in a dungeon uses.
 */
constexpr int kRockTextureBlock = 0x30;
constexpr int kRockFileBytes = 512 * 1024;
constexpr int kRockModelBytes = 256 * 1024;

alignas(64) u_long128 g_rock_file[kRockFileBytes / 16];
alignas(64) u_long128 g_rock_model_bytes[kRockModelBytes / 16];
CDataAlloc2<1>        g_rock_alloc;
CFrame               *g_rock_frame = nullptr;
bool                  g_rock_tried = false;
int                   g_rock_texture_floor = -1; // the floor the texture was entered for

CFrame *RockModel() {
    if (!g_rock_tried) {
        g_rock_tried = true;
        char path[] = "gedit/s96/chara/iwa.chr";
        int  size = 0;

        if (LoadFileBG(path, g_rock_file, &size) != 0 && size > 0 && size <= kRockFileBytes) {
            char  mds_name[] = "iwa.mds";
            int   mds_size = 0;
            auto *pack = reinterpret_cast<u_int *>(g_rock_file);
            u_int *mds = GetPackFile(pack, mds_name, &mds_size);

            if (mds != nullptr) {
                g_rock_alloc.base = reinterpret_cast<u_char *>(g_rock_model_bytes);
                g_rock_alloc.used = 0;
                g_rock_alloc.limit = kRockModelBytes / 16;
                g_rock_frame = LoadMDSFile(mds, &g_rock_alloc, 0, nullptr, nullptr);
            }
        }

        std::printf("[Expanded] Terra Sword boulder %s\n", g_rock_frame != nullptr ? "loaded" : "NOT FOUND");
    }

    // The dungeon's texture blocks are rebuilt on a floor change, so the picture goes back in.
    if (g_rock_frame != nullptr && UserStatus != nullptr && g_rock_texture_floor != UserStatus->cur_floor + 100 * selectMapNo) {
        char  img_name[] = "iwa.img";
        int   img_size = 0;
        u_int *img = GetPackFile(reinterpret_cast<u_int *>(g_rock_file), img_name, &img_size);

        if (img != nullptr) {
            TexManager.DeleteTextureBlock(kRockTextureBlock);
            TexManager.EnterIMGFile(reinterpret_cast<u_char *>(img), kRockTextureBlock, 0, 0);
            g_rock_texture_floor = UserStatus->cur_floor + 100 * selectMapNo;
        }
    }

    return g_rock_frame;
}

// Dark Cloud Expanded: Super Steve's columns borrow two models from the towns. Each is read once
// a session (the file's model and texture copied out, the rest let go) and its texture entered
// again on each floor, in a block of its own past the item models' (0x38 to 0x3D).
constexpr int kTreesTextureBlock = 0x3E;
constexpr int kNutTextureBlock = 0x3F;

struct BorrowedModel {
    std::vector<u_long128> mds;        // the model file, kept: the frames point into it
    std::vector<u_long128> arena;      // what LoadMDSFile builds the frames in
    std::vector<u_long128> image;      // the IMG bank entered on each floor
    CDataAlloc2<1>         alloc;
    CFrame                *frame = nullptr;
    bool                   tried = false;
    int                    texture_floor = -1;
};

/**
 * Builds the model's frames from an MDS copied out of a file.
 */
void BuildBorrowed(BorrowedModel &model, const std::uint8_t *mds, std::size_t mds_size, std::size_t arena_bytes) {
    model.mds.assign((mds_size + 15) / 16, u_long128{});
    std::memcpy(model.mds.data(), mds, mds_size);
    model.arena.assign(arena_bytes / 16, u_long128{});
    model.alloc.base = reinterpret_cast<u_char *>(model.arena.data());
    model.alloc.used = 0;
    model.alloc.limit = static_cast<int>(arena_bytes / 16);
    model.frame = LoadMDSFile(reinterpret_cast<u_int *>(model.mds.data()), &model.alloc, 0, nullptr, nullptr);
}

/**
 * Keeps the bank as one IMG holding only the named picture (a town bank holds many).
 */
bool KeepPicture(BorrowedModel &model, const std::uint8_t *bank, std::size_t bank_size, std::string_view picture) {
    if (bank_size < sizeof(IMG_head)) {
        return false;
    }

    const auto *head = reinterpret_cast<const IMG_head *>(bank);
    const auto *entries = reinterpret_cast<const IMG_entry *>(head + 1);
    const u_int count = head->pictures;

    if (sizeof(IMG_head) + count * sizeof(IMG_entry) > bank_size) {
        return false;
    }

    for (u_int i = 0; i < count; i++) {
        if (std::string_view(entries[i].name, strnlen(entries[i].name, sizeof(entries[i].name))) != picture) {
            continue;
        }

        // The picture runs to the next one's start, or the bank's end.
        std::size_t end = bank_size;

        for (u_int j = 0; j < count; j++) {
            if (entries[j].offset > entries[i].offset && entries[j].offset < end) {
                end = entries[j].offset;
            }
        }

        if (entries[i].offset >= end) {
            return false;
        }

        constexpr std::size_t kHead = sizeof(IMG_head) + sizeof(IMG_entry);
        const std::size_t     size = end - entries[i].offset;
        model.image.assign((kHead + size + 15) / 16, u_long128{});
        auto *out = reinterpret_cast<std::uint8_t *>(model.image.data());
        IMG_head  one_head = *head;
        IMG_entry one = entries[i];
        one_head.pictures = 1;
        one.offset = kHead;
        std::memcpy(out, &one_head, sizeof(one_head));
        std::memcpy(out + sizeof(IMG_head), &one, sizeof(one));
        std::memcpy(out + kHead, bank + entries[i].offset, size);
        return true;
    }

    return false;
}

/**
 * The dungeon's texture blocks are rebuilt on a floor change, so the picture goes back in.
 */
void EnterBorrowedTexture(BorrowedModel &model, int block) {
    if (model.frame == nullptr || model.image.empty() || UserStatus == nullptr) {
        return;
    }

    const int floor = UserStatus->cur_floor + 100 * selectMapNo;

    if (model.texture_floor != floor) {
        TexManager.DeleteTextureBlock(block);
        TexManager.EnterIMGFile(reinterpret_cast<u_char *>(model.image.data()), block, 0, 0);
        model.texture_floor = floor;
    }
}

/**
 * Queens' two trees and their grass (Expanded's QueensTrees.cs): the georama part e03t01 of
 * gedit/e03/scene.scn, its display model (the first of its two MDS blocks) with the grass
 * square's mesh dropped, and its picture e03b04 from e03b01.img in gedit/e03/img.pak.
 */
BorrowedModel g_trees;

CFrame *TreesModel() {
    if (!g_trees.tried) {
        g_trees.tried = true;
        std::vector<std::uint8_t> scene;
        std::vector<std::uint8_t> pak;
        const char               *why = nullptr;

        if (!dc::data::DataReadReadFile("gedit/e03/scene.scn", scene) || !dc::data::DataReadReadFile("gedit/e03/img.pak", pak)) {
            why = "gedit/e03/scene.scn or img.pak not readable";
        }

        // The scene's sub-file directory: 0x30-byte entries from 0x10, the name, then its
        // offset and size at +0x10 and +0x14.
        std::size_t sub = 0;
        std::size_t sub_size = 0;

        for (std::size_t o = 0x10; why == nullptr && o + 0x30 <= scene.size(); o += 0x30) {
            const char *name = reinterpret_cast<const char *>(&scene[o]);
            const auto  length = strnlen(name, 16);

            if (length == 0 || !std::isalnum(static_cast<unsigned char>(name[0]))) {
                break;
            }

            if (std::string_view(name, length) == "e03t01") {
                u_int at = 0;
                u_int size = 0;
                std::memcpy(&at, &scene[o + 0x10], 4);
                std::memcpy(&size, &scene[o + 0x14], 4);
                sub = at;
                sub_size = size;
                break;
            }
        }

        if (why == nullptr && (sub == 0 || sub_size == 0 || sub + sub_size > scene.size())) {
            why = "e03t01 is not in the scene";
        }

        // Its first two 16-aligned "MDS" blocks: the display model, then the collision model.
        std::size_t blocks[2] = {0, 0};
        int         found = 0;

        for (std::size_t p = 0; why == nullptr && p + 4 <= sub_size && found < 2; p += 16) {
            if (std::memcmp(&scene[sub + p], "MDS\0", 4) == 0) {
                blocks[found++] = p;
            }
        }

        if (why == nullptr && found < 2) {
            why = "e03t01's model blocks not found";
        }

        if (why == nullptr) {
            std::vector<std::uint8_t> mds(scene.begin() + static_cast<std::ptrdiff_t>(sub + blocks[0]),
                                          scene.begin() + static_cast<std::ptrdiff_t>(sub + blocks[1]));

            // The grass square is textured as Queens' paving: its node's mesh offset (+0x28 of
            // its 0x70-byte entry; count at +8, table at +0xC) zeroed, every other node kept.
            u_int count = 0;
            u_int table = 0;
            bool  dropped = false;
            std::memcpy(&count, &mds[8], 4);
            std::memcpy(&table, &mds[0xC], 4);

            for (u_int i = 0; i < count && table + (i + 1) * 0x70 <= mds.size(); i++) {
                const char *name = reinterpret_cast<const char *>(&mds[table + i * 0x70 + 8]);

                if (std::string_view(name, strnlen(name, 0x20)) == "grid__a7f") {
                    std::memset(&mds[table + i * 0x70 + 0x28], 0, 4);
                    dropped = true;
                }
            }

            int    bank_size = 0;
            char   bank_name[] = "e03b01.img";
            u_int *bank = GetPackFile(reinterpret_cast<u_int *>(pak.data()), bank_name, &bank_size);

            if (!dropped) {
                why = "the grass square is not in the model";
            } else if (bank == nullptr || bank_size <= 0 ||
                       !KeepPicture(g_trees, reinterpret_cast<const std::uint8_t *>(bank), static_cast<std::size_t>(bank_size), "e03b04")) {
                why = "e03b04 is not in img.pak's e03b01.img";
            } else {
                BuildBorrowed(g_trees, mds.data(), mds.size(), 256 * 1024);

                if (g_trees.frame == nullptr) {
                    why = "the model did not build";
                }
            }
        }

        if (why != nullptr) {
            std::printf("[Expanded] Queens trees NOT LOADED: %s\n", why);
        } else {
            std::printf("[Expanded] Queens trees loaded\n");
        }
    }

    EnterBorrowedTexture(g_trees, kTreesTextureBlock);
    return g_trees.frame;
}

/**
 * The nut (Expanded's KinomiModel.cs): gedit/s04/chara/e114kinomi.chr's e114kinomi.mds and
 * kinomi.img, as they are.
 */
BorrowedModel g_nut;

CFrame *NutModel() {
    if (!g_nut.tried) {
        g_nut.tried = true;
        std::vector<std::uint8_t> pack;
        const char               *why = nullptr;

        if (!dc::data::DataReadReadFile("gedit/s04/chara/e114kinomi.chr", pack)) {
            why = "gedit/s04/chara/e114kinomi.chr not readable";
        } else {
            int    mds_size = 0;
            int    img_size = 0;
            char   mds_name[] = "e114kinomi.mds";
            char   img_name[] = "kinomi.img";
            u_int *mds = GetPackFile(reinterpret_cast<u_int *>(pack.data()), mds_name, &mds_size);
            u_int *img = GetPackFile(reinterpret_cast<u_int *>(pack.data()), img_name, &img_size);

            if (mds == nullptr || img == nullptr || mds_size <= 0 || img_size <= 0) {
                why = "the pack has no e114kinomi.mds or kinomi.img";
            } else {
                g_nut.image.assign((static_cast<std::size_t>(img_size) + 15) / 16, u_long128{});
                std::memcpy(g_nut.image.data(), img, static_cast<std::size_t>(img_size));
                BuildBorrowed(g_nut, reinterpret_cast<const std::uint8_t *>(mds), static_cast<std::size_t>(mds_size), 64 * 1024);

                if (g_nut.frame == nullptr) {
                    why = "the model did not build";
                }
            }
        }

        if (why != nullptr) {
            std::printf("[Expanded] Super Steve's nut NOT LOADED: %s\n", why);
        } else {
            std::printf("[Expanded] Super Steve's nut loaded\n");
        }
    }

    EnterBorrowedTexture(g_nut, kNutTextureBlock);
    return g_nut.frame;
}

/**
 * One hit on monster m's nearest live hit sphere to (x, z): its damage, a kick from origin, and
 * through any guard when guard_break.
 */
bool PlantHitNear(int m, float x, float z, float reach, int damage, const float *origin, float kick, bool guard_break,
                  float max_height = 1e9f) {
    if (NowMonstorUnit == nullptr || NowWeaponHave == nullptr || !ActiveMonster(m)) {
        return false;
    }

    const auto &spheres = NowMonstorUnit->effect[m];
    int         nearest = -1;
    float       edge = 0.0f;

    for (int sphere = 0; sphere < 16; sphere++) {
        if (spheres.timer[sphere] == 0 || spheres.position[sphere][1] - spheres.radius[sphere] > max_height) {
            continue;
        }

        const float dx = spheres.position[sphere][0] - x;
        const float dz = spheres.position[sphere][2] - z;
        const float distance = std::max(0.0f, std::sqrt(dx * dx + dz * dz) - spheres.radius[sphere]);

        if (nearest < 0 || distance < edge) {
            nearest = sphere;
            edge = distance;
        }
    }

    if (nearest < 0 || edge > reach) {
        return false;
    }

    float centre[4] = {spheres.position[nearest][0], spheres.position[nearest][1], spheres.position[nearest][2], 1.0f};
    NowColData->Set(centre, std::max(1, damage), 1, spheres.radius[nearest] + 1.0f, 0.0f, 2, 2, 0, 0);

    auto &hit = NowColData->hit[NowColData->now_hit];
    hit.vs_monster = NowWeaponHave->vs_monster;
    hit.weapon_flags = NowWeaponHave->flags;
    hit.flags = 0;
    hit.owner = UserStatus->cur_chara;
    hit.attack_no = 0;

    float from[4] = {origin[0], origin[1], origin[2], 1.0f};
    NowColData->SetKickBack(from, kick, kBlastKickDecay, 2);

    if (guard_break) {
        g_blast_crush[g_blast_crush_next] = {NowColData->now_hit, std::max(1, damage), g_step_count};
        g_blast_crush_next = (g_blast_crush_next + 1) % static_cast<int>(g_blast_crush.size());
    }

    return true;
}

int WeaponAttack() {
    return NowWeaponHave != nullptr ? NowWeaponHave->attack : 0;
}

enum class ColumnKind { Spear, Cactus, Rock };

struct Column {
    ColumnKind kind;
    bool       up = false;
    FloorModel model;
    float      x = 0.0f, z = 0.0f, ground = 0.0f;
    float      scale = 1.0f;
    float      yaw = 0.0f;
    float      radius = 6.0f;   // solid to enemies and the player within this
    int        age = 0;         // steps since it was raised
    int        life = 0;        // steps it stands before it goes
    int        gone = 0;        // steps it takes to sink away after
    int        next_hit = 0;
    int        target = -1;     // Babel's Spear: the enemy it rises under
    bool       struck = false;
    float      fall_y = 0.0f;   // the rock: its bottom's height while it falls
    float      fall_speed = 0.0f;
    bool       falling = false;
    int        weapon_item = -1;
    // Dark Cloud Expanded: Super Steve's forms (Xiao with a Cactus, Babel's Spear or Terra Sword
    // sphere): Queens' trees, the statue and the nut.
    int        wielder = -1;      // the character who raised it
    float      exposed = 0.0f;    // of the model's length out of the ground, before scaling
    float      draw_dx = 0.0f;    // the model's root from the column's centre, at 1x
    float      draw_dz = 0.0f;
    float      sink = 0.0f;       // the rock and the nut: how far into the ground they rest
    float      beam_scale = 1.0f; // Babel's beam
    bool       trees = false;     // Queens' trees: a wall, no spikes, no shadow
    bool       statue = false;    // Super Steve's statue: its two hands in place of the spikes
    bool       nut = false;       // the nut: lands on the target's head, bonks and confuses it
    bool       hopping = false;   // the nut's hop off the head
    float      hop_x = 0.0f;      // the hop's drift a step
    float      hop_z = 0.0f;
    float      hop_y = 0.0f;      // its rise a step
};

// Babel's Spear "Curse of Babel".
constexpr int   kBabelGuardSteps = 5 * kStepsPerSecond;
constexpr float kBabelScale = 5.0f;
constexpr float kBabelExposed = 10.0f;     // of the model's length out of the ground, before scaling
constexpr float kBabelRadius = 8.0f;
constexpr float kBabelStrikeReach = 6.0f;
constexpr int   kBabelRiseSteps = kStepsPerSecond / 2;
constexpr int   kBabelLifeSteps = 20 * kStepsPerSecond;
constexpr int   kBabelGoneSteps = 220;      // Expanded's vanish, 3.67 s
constexpr float kBabelSpin = 240.0f * 3.14159265f / 180.0f / kStepsPerSecond;
constexpr float kBabelNoLockReach = 60.0f;
constexpr float kBabelAhead = 15.0f;
// Cactus "Desert Bloom".
constexpr int   kCactusGuardSteps = 15;
constexpr float kCactusScale = 3.0f;
constexpr float kCactusExposed = 6.8f;
constexpr float kCactusRadius = 6.0f;
constexpr int   kCactusRiseSteps = 18;
constexpr int   kCactusLifeSteps = 10 * kStepsPerSecond;
constexpr int   kCactusGoneSteps = kStepsPerSecond / 2;
constexpr float kCactusAhead = 10.0f;
// The spikes of both: a sixth of the attack every quarter second to what touches the column.
constexpr float kSpikeReach = 2.0f;
constexpr float kSpikeShare = 1.0f / 6.0f;
constexpr int   kSpikeSteps = kStepsPerSecond / 4;
constexpr float kSpikeKick = 1.0f;
// Terra Sword "Big Rock".
constexpr int   kRockGuardSteps = 5 * kStepsPerSecond;
constexpr float kRockDrop = 500.0f;
constexpr float kRockGravity = 180.0f / (kStepsPerSecond * kStepsPerSecond);
constexpr float kRockGrowDrop = 150.0f;
constexpr float kRockRadius = 21.0f;
constexpr float kRockBlast = kRockRadius + 5.0f;
constexpr float kRockSink = 4.0f;
constexpr int   kRockLifeSteps = 20 * kStepsPerSecond;
constexpr int   kRockGoneSteps = kStepsPerSecond / 2;
constexpr float kRockWhp = 10.0f;
// Super Steve's statue (Expanded's BabelCopy.cs StatueForm): a 4x upright copy of Super Steve,
// all but its feet out, a solid column of its feet's width; its two hands strike within 6 units
// for half the attack, one passing every 180 degrees of the 240 degree a second turn (0.75 s).
constexpr float kStatueScale = 4.0f;
constexpr float kStatueExposed = 3.94f;
constexpr float kStatueRadius = 2.0f;
constexpr float kStatueHandReach = 4.0f;    // past the column: 6 units from its centre
constexpr float kStatueHandShare = 0.5f;
constexpr int   kStatueHandSteps = 45;
constexpr float kStatueBeamScale = 0.7f;
// Queens' trees (Expanded's Cactus.cs TreesForm): 74.3 tall at 1x, all of it out, at 0.7x; the
// column covers both trunks; centred on the spot by the grass square's centre (model -6, -1);
// turned a quarter from the facing. They only block: no spikes, no shadow.
constexpr float kTreesScale = 0.7f;
constexpr float kTreesTop = 74.3f;
constexpr float kTreesRadius = 9.5f;
constexpr float kTreesCentreX = -6.0f;
constexpr float kTreesCentreZ = -1.0f;
constexpr float kTreesTurn = 3.14159265f / 2.0f;
// Super Steve's nut (Expanded's TerraSword.cs and NutBonk.cs): 2x (radius 3.8), it falls onto the
// target's head, bonks it for half the attack through any guard and confuses it while the nut
// stays; it hops off (up 80, sideways 45 units a second, gravity 400) and rests 20 s on the floor.
constexpr float kNutScale = 2.0f;
constexpr float kNutRadius = 1.9f * kNutScale;
constexpr float kNutSink = 0.5f;
constexpr float kNutBonkShare = 0.5f;
constexpr float kNutBonkReach = 6.0f;
constexpr float kNutHopUp = 80.0f / kStepsPerSecond;
constexpr float kNutHopSide = 45.0f / kStepsPerSecond;
constexpr float kNutHopGravity = 400.0f / (kStepsPerSecond * kStepsPerSecond);
constexpr float kNutShade[3] = {-31.0f, -31.0f, -31.0f};   // the rock's darkening, lighter (0.8 for 0.55)

// How they look: the spear's guard charge tints it light blue and the Dark Genie's small beam
// stands with it (its rise, its loop, its vanish); the Cactus comes up in a puff of smoke; the
// Terra Sword glows green while primed, its target darkens as the boulder falls, and the landing
// sends out the bombs' shockwave.
constexpr float kBabelTint[3] = {80.0f, 150.0f, 230.0f};
constexpr int   kBeamRise = 0;
constexpr int   kBeamStand = 1;
constexpr int   kBeamVanish = 2;
constexpr float kBeamScale = 1.0f;
constexpr float kTerraTint[3] = {60.0f, 170.0f, 40.0f};
constexpr float kTargetShade[3] = {-70.0f, -70.0f, -70.0f};
constexpr float kShockRadius = 60.0f;
constexpr float kShockSteps = 30.0f;

Column g_babel{ColumnKind::Spear};
Column g_cactus{ColumnKind::Cactus};
Column g_rock{ColumnKind::Rock};
int    g_babel_beam = -1;
int    g_column_guard = 0;     // steps the guard has been held
bool   g_column_cast = false;  // this guard has raised something already
bool   g_rock_primed = false;

/**
 * The root's height for a weapon copy whose exposed share of its length is out by t (0 to 1).
 */
float CopyRoot(const Column &column, float exposed, float t) {
    const float top = column.ground + exposed * column.scale;
    const float buried = column.ground - 4.0f;
    const float tip_y = buried + (top - buried) * std::clamp(t, 0.0f, 1.0f);
    return tip_y - column.model.tip * column.scale;
}

/**
 * Where the column's root stands this step, and whether it is out of the ground enough to block.
 */
float ColumnRoot(const Column &column, bool &solid) {
    solid = false;

    switch (column.kind) {
        case ColumnKind::Spear: {
            float t = static_cast<float>(column.age) / kBabelRiseSteps;

            if (column.age > column.life) {
                t = 1.0f - static_cast<float>(column.age - column.life) / column.gone;
            }

            solid = t > 0.5f;
            return CopyRoot(column, column.exposed, t);
        }
        case ColumnKind::Cactus: {
            // Dark Cloud Expanded: it stands where it rose and fades away (ColumnsDraw), no longer
            // solid once it starts to.
            const float t = static_cast<float>(column.age) / kCactusRiseSteps;

            solid = t > 0.5f && column.age <= column.life;
            return CopyRoot(column, column.exposed, t);
        }
        case ColumnKind::Rock: {
            // The nut's root is at its centre, as the rock's is; the nut never blocks.
            if (column.falling || column.hopping) {
                return column.fall_y + (column.nut ? kNutRadius / kNutScale * column.scale : column.radius);
            }

            float sink = 0.0f;

            if (column.age > column.life && !column.nut) {
                sink = 2.0f * column.radius * static_cast<float>(column.age - column.life) / column.gone;
            }

            solid = !column.nut && sink < column.radius;
            return column.ground + column.radius - column.sink - sink;
        }
    }

    return column.ground;
}

bool ColumnSolid(const Column &column) {
    bool solid = false;

    if (column.up) {
        ColumnRoot(column, solid);
    }

    return solid;
}

void ColumnEnd(Column &column) {
    if (&column == &g_babel) {
        fx::End(g_babel_beam);
        g_babel_beam = -1;
    }

    column.up = false;
    column.model.root = nullptr;
    column.target = -1;
    column.falling = false;
    column.hopping = false;
}

void ColumnsReset() {
    ColumnEnd(g_babel);
    ColumnEnd(g_cactus);
    ColumnEnd(g_rock);
    g_column_guard = 0;
    g_column_cast = false;
    g_rock_primed = false;
}

/**
 * The locked-on enemy, else the nearest within reach of Ungaga; -1 with none.
 */
int ColumnTarget(float reach) {
    const int locked = LockedTarget();

    if (locked >= 0) {
        return locked;
    }

    int   best = -1;
    float best_distance = reach * reach;

    for (int m = 0; m < 16 && NowMonstorUnit != nullptr; m++) {
        if (!ActiveMonster(m)) {
            continue;
        }

        float at[4];
        MonsterPosition(m, at);
        const float dx = at[0] - CharaMain.pos[0];
        const float dz = at[2] - CharaMain.pos[2];

        if (dx * dx + dz * dz < best_distance) {
            best_distance = dx * dx + dz * dz;
            best = m;
        }
    }

    return best;
}

void Ahead(float distance, float &x, float &z) {
    x = CharaMain.pos[0] + std::sin(CharaMain.rotation.y) * distance;
    z = CharaMain.pos[2] + std::cos(CharaMain.rotation.y) * distance;
}

void RaiseCopy(Column &column, int held, float scale, float radius, int life, int gone) {
    if (!column.model.TakeWeapon(NowWeapon)) {
        column.up = false;
        return;
    }

    column.up = true;
    column.scale = scale;
    column.radius = radius;
    column.life = life;
    column.gone = gone;
    column.age = 0;
    column.next_hit = 0;
    column.struck = false;
    column.yaw = CharaMain.rotation.y;
    column.weapon_item = held;
    column.draw_dx = 0.0f;
    column.draw_dz = 0.0f;
    column.trees = false;
    column.statue = false;
}

void RaiseBabel(int held, int wielder) {
    Column &column = g_babel;
    column.target = ColumnTarget(kBabelNoLockReach);

    if (column.target >= 0) {
        float at[4];
        MonsterPosition(column.target, at);
        column.x = at[0];
        column.z = at[2];
        column.ground = at[1];
    } else {
        Ahead(kBabelAhead, column.x, column.z);
        column.ground = CharaMain.pos[1];
    }

    // Super Steve's sphere raises a statue of Super Steve in the spear's place.
    const bool statue = wielder == CHARA_XIAO;
    RaiseCopy(column, held, statue ? kStatueScale : kBabelScale, statue ? kStatueRadius : kBabelRadius, kBabelLifeSteps,
              kBabelGoneSteps);
    column.wielder = wielder;
    column.statue = statue;
    column.exposed = statue ? kStatueExposed : kBabelExposed;
    column.beam_scale = statue ? kStatueBeamScale : kBeamScale;
    setUnitAmbientAnime(15.0f, 1.0f, 12.0f, 50.0f, 84.0f);

    if (column.up) {
        const float at[4] = {column.x, column.ground, column.z, 1.0f};
        fx::End(g_babel_beam);
        g_babel_beam = fx::Play(fx::Prop::Beam, kBeamRise, at, column.beam_scale, 0.0f);
    }
}

void RaiseTrees(int held, int wielder) {
    CFrame *frame = TreesModel();

    if (frame == nullptr) {
        return;
    }

    Column &column = g_cactus;
    Ahead(kCactusAhead, column.x, column.z);
    column.ground = CharaMain.pos[1];
    column.up = true;
    column.model = {};
    column.model.root = frame;
    column.model.block = kTreesTextureBlock;
    column.model.tip = kTreesTop;
    column.scale = kTreesScale;
    column.radius = kTreesRadius;
    column.life = kCactusLifeSteps;
    column.gone = kCactusGoneSteps;
    column.age = 0;
    column.next_hit = 0;
    column.struck = false;
    column.weapon_item = held;
    column.wielder = wielder;
    column.trees = true;
    column.statue = false;
    column.exposed = kTreesTop;
    column.yaw = std::remainder(CharaMain.rotation.y + kTreesTurn, 2.0f * 3.14159265f);

    // The model's root, so the grass square's centre lands on the spot.
    const float c = std::cos(column.yaw);
    const float s = std::sin(column.yaw);
    column.draw_dx = -(c * kTreesCentreX + s * kTreesCentreZ);
    column.draw_dz = -(-s * kTreesCentreX + c * kTreesCentreZ);

    const float at[4] = {column.x, column.ground, column.z, 1.0f};
    fx::Play(fx::Prop::Smoke, 0, at, kSmokeScale, CharaMain.rotation.y, false, kSmokeRate);
}

void RaiseCactus(int held, int wielder) {
    // Super Steve's sphere raises Queens' trees instead.
    if (wielder == CHARA_XIAO) {
        RaiseTrees(held, wielder);
        return;
    }

    Column &column = g_cactus;
    Ahead(kCactusAhead, column.x, column.z);
    column.ground = CharaMain.pos[1];
    RaiseCopy(column, held, kCactusScale, kCactusRadius, kCactusLifeSteps, kCactusGoneSteps);
    column.wielder = wielder;
    column.exposed = kCactusExposed;

    if (column.up) {
        const float at[4] = {column.x, column.ground, column.z, 1.0f};
        fx::Play(fx::Prop::Smoke, 0, at, kSmokeScale, CharaMain.rotation.y, false, kSmokeRate);
    }
}

void DropRock(int target, int held, int wielder) {
    // Super Steve's sphere drops the nut instead, onto the target's head.
    const bool nut = wielder == CHARA_XIAO;
    CFrame    *frame = nut ? NutModel() : RockModel();

    if (frame == nullptr) {
        return;
    }

    Column &column = g_rock;
    float   at[4];
    MonsterPosition(target, at);
    column.up = true;
    column.model = {};
    column.model.root = frame;
    column.model.block = nut ? kNutTextureBlock : kRockTextureBlock;
    column.wielder = wielder;
    column.nut = nut;
    column.hopping = false;
    column.sink = nut ? kNutSink : kRockSink;
    column.x = at[0];
    column.z = at[2];
    column.ground = at[1];
    column.radius = nut ? kNutRadius : kRockRadius;
    column.scale = 1.0f;
    column.yaw = static_cast<float>(std::rand() % 628) / 100.0f;
    column.target = target;
    column.falling = true;
    column.fall_y = at[1] + kRockDrop;
    column.fall_speed = 0.0f;
    column.life = kRockLifeSteps;
    column.gone = kRockGoneSteps;
    column.age = 0;
    column.next_hit = 0;
    column.weapon_item = held;
}

/**
 * The spikes: a hit on every enemy touching the column, from its centre, once every interval.
 */
void Spikes(Column &column, float share, float reach, float kick, float top, int interval = kSpikeSteps) {
    if (column.age < column.next_hit) {
        return;
    }

    column.next_hit = column.age + interval;
    const int   damage = static_cast<int>(std::lround(WeaponAttack() * share));
    const float origin[4] = {column.x, column.ground, column.z, 1.0f};

    for (int m = 0; m < 16; m++) {
        PlantHitNear(m, column.x, column.z, column.radius + reach, damage, origin, kick, false, top);
    }
}

/**
 * Curse of Babel's floor: every regular enemy confused while the spear stands and fades.
 */
void BabelConfusion() {
    const int until = g_step_count + 2;

    for (int m = 0; m < 16; m++) {
        if (ActiveMonster(m) && NowMonstorUnit->monster[m].kind != MONSTER_KIND_NO_LOCK_ON) {
            g_confused_until[m] = std::max(g_confused_until[m], until);
        }
    }
}

void BabelStep() {
    Column &column = g_babel;
    column.age++;

    if (column.age > column.life + column.gone) {
        ColumnEnd(column);
        return;
    }

    // It follows its enemy while it rises; the tip strikes once it is out.
    if (column.age < kBabelRiseSteps && column.target >= 0 && ActiveMonster(column.target)) {
        float at[4];
        MonsterPosition(column.target, at);
        column.x = at[0];
        column.z = at[2];
    }

    if (!column.struck && column.age >= kBabelRiseSteps) {
        column.struck = true;
        const float origin[4] = {column.x, column.ground, column.z, 1.0f};

        for (int m = 0; m < 16; m++) {
            PlantHitNear(m, column.x, column.z, kBabelStrikeReach, WeaponAttack(), origin, 2.02f, true);
        }
    }

    if (column.age >= kBabelRiseSteps && column.age <= column.life) {
        column.yaw += kBabelSpin;

        if (column.statue) {
            Spikes(column, kStatueHandShare, kStatueHandReach, kSpikeKick, column.ground + column.exposed * column.scale,
                   kStatueHandSteps);
        } else {
            Spikes(column, kSpikeShare, kSpikeReach, kSpikeKick, column.ground + column.exposed * column.scale);
        }
    }

    // The beam rises with it, stands looping while it stands, and vanishes as it sinks.
    const float at[4] = {column.x, column.ground, column.z, 1.0f};

    if (column.age == kBabelRiseSteps) {
        if (fx::Live(g_babel_beam)) {
            fx::Clip(g_babel_beam, kBeamStand, true);
        } else {
            g_babel_beam = fx::Play(fx::Prop::Beam, kBeamStand, at, kBeamScale, 0.0f, true);
        }
    } else if (column.age == column.life + 1) {
        fx::Clip(g_babel_beam, kBeamVanish, false);
    }

    fx::Move(g_babel_beam, at);
    BabelConfusion();
}

void CactusStep() {
    Column &column = g_cactus;
    column.age++;

    if (column.age > column.life + column.gone) {
        ColumnEnd(column);
        return;
    }

    if (column.age >= kCactusRiseSteps && column.age <= column.life && !column.trees) {
        Spikes(column, kSpikeShare, kSpikeReach, kSpikeKick, column.ground + column.exposed * column.scale);
    }
}

/**
 * Super Steve's nut: it falls onto the target's head, bonks it and hops off to rest on the floor.
 * The enemy stays confused while the nut stays.
 */
void NutStep() {
    Column &column = g_rock;
    const bool on_target = column.target >= 0 && ActiveMonster(column.target);

    if (column.falling) {
        float head = column.ground;

        if (on_target) {
            float at[4];
            float top[4];
            MonsterPosition(column.target, at);
            MonsterTop(column.target, top);
            column.x = at[0];
            column.z = at[2];
            column.ground = at[1];
            head = top[1];
            fx::MonsterTint(column.target, kNutShade, 2);
        }

        column.fall_speed += kRockGravity;
        column.fall_y -= column.fall_speed;
        const float dropped = column.ground + kRockDrop - column.fall_y;
        column.scale = std::clamp(dropped / kRockGrowDrop, 0.01f, 1.0f) * kNutScale;

        if (column.fall_y > head) {
            return;
        }

        column.falling = false;
        column.scale = kNutScale;

        if (!on_target) {
            // It missed: it simply comes to rest.
            column.target = -1;
            column.age = 0;
            return;
        }

        // The bonk: half the attack through any guard, with a plain blow's kick from the player.
        const float origin[4] = {CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], 1.0f};
        const int   damage = std::max(1, static_cast<int>(std::lround(WeaponAttack() * kNutBonkShare)));
        PlantHitNear(column.target, column.x, column.z, kNutBonkReach, damage, origin, kSpikeKick, true);

        // The hop off the head, away from the player.
        float dx = column.x - CharaMain.pos[0];
        float dz = column.z - CharaMain.pos[2];
        const float length = std::sqrt(dx * dx + dz * dz);

        if (length > 0.01f) {
            dx /= length;
            dz /= length;
        } else {
            dx = std::sin(CharaMain.rotation.y);
            dz = std::cos(CharaMain.rotation.y);
        }

        column.hopping = true;
        column.hop_x = dx * kNutHopSide;
        column.hop_z = dz * kNutHopSide;
        column.hop_y = kNutHopUp;
        column.fall_y = head;
        std::printf("[Expanded] Super Steve's nut bonks enemy %d for %d\n", column.target, damage);
        return;
    }

    // The bonked enemy stays confused while the nut stays.
    if (column.target >= 0 && ActiveMonster(column.target)) {
        g_confused_until[column.target] = std::max(g_confused_until[column.target], g_step_count + 2);
    }

    if (column.hopping) {
        column.x += column.hop_x;
        column.z += column.hop_z;
        column.fall_y += column.hop_y;
        column.hop_y -= kNutHopGravity;

        if (column.hop_y < 0.0f && column.fall_y <= column.ground - column.sink) {
            column.hopping = false;
            column.age = 0;
        }

        return;
    }

    column.age++;

    if (column.age > column.life + column.gone) {
        ColumnEnd(column);
    }
}

void RockStep() {
    Column &column = g_rock;

    if (column.nut) {
        NutStep();
        return;
    }

    if (column.falling) {
        // It follows the enemy down until it lands.
        if (column.target >= 0 && ActiveMonster(column.target)) {
            float at[4];
            MonsterPosition(column.target, at);
            column.x = at[0];
            column.z = at[2];
            column.ground = at[1];
        }

        if (column.target >= 0 && ActiveMonster(column.target)) {
            fx::MonsterTint(column.target, kTargetShade, 2);
        }

        column.fall_speed += kRockGravity;
        column.fall_y -= column.fall_speed;
        const float dropped = column.ground + kRockDrop - column.fall_y;
        column.scale = std::clamp(dropped / kRockGrowDrop, 0.01f, 1.0f);

        if (column.fall_y > column.ground - kRockSink) {
            return;
        }

        // The landing: four times the attack to every enemy within reach, through any guard.
        column.falling = false;
        column.scale = 1.0f;
        column.age = 0;
        column.next_hit = kStepsPerSecond;
        const float origin[4] = {column.x, column.ground, column.z, 1.0f};

        for (int m = 0; m < 16; m++) {
            PlantHitNear(m, column.x, column.z, kRockBlast, WeaponAttack() * 4, origin, kBlastKickSpeed * 1.41421356f, true);
        }

        DrainWhp(column.weapon_item, kRockWhp);
        setUnitAmbientAnime(15.0f, 1.0f, 80.0f, 150.0f, 20.0f);

        // The bombs' shockwave rings out from where it lands.
        if (CShockWave *wave = NowShockWave) {
            wave->position[0] = column.x;
            wave->position[1] = column.ground;
            wave->position[2] = column.z;
            wave->position[3] = 1.0f;
            wave->base_radius = kShockRadius;
            wave->expand_radius = kShockRadius;
            wave->radius = 0.0f;
            wave->expand_steps = kShockSteps;
            wave->phase = 0.0f;
            wave->alpha = 0.0f;
            wave->active = 1;
        }

        SndSePlay(SE_GORO_SMASH, -1, 0);
        return;
    }

    column.age++;

    if (column.age > column.life + column.gone) {
        ColumnEnd(column);
        return;
    }

    // Resting, it crushes what is under it once a second.
    if (column.age <= column.life && column.age >= column.next_hit) {
        column.next_hit = column.age + kStepsPerSecond;
        const float origin[4] = {column.x, column.ground, column.z, 1.0f};

        for (int m = 0; m < 16; m++) {
            PlantHitNear(m, column.x, column.z, column.radius, WeaponAttack(), origin, 1.0f, true);
        }
    }
}

/**
 * Who can raise a column: Ungaga with his weapon, or Xiao with Super Steve (its sphere names the
 * effect); -1 for anyone else.
 */
int ColumnWielder() {
    const int chara = UserStatus->cur_chara;

    if (chara == CHARA_UNGAGA) {
        return CHARA_UNGAGA;
    }

    if (chara == CHARA_XIAO && HeldBy(CHARA_XIAO) == ITEM_WEAPON_SUPER_STEVE) {
        return CHARA_XIAO;
    }

    return -1;
}

void ColumnsStep() {
    const int  wielder = ColumnWielder();
    const int  held = wielder >= 0 ? EffectOf(wielder) : -1;
    const bool ungaga_out = wielder >= 0;

    // Each goes with its weapon (or sphere) leaving the hand or its wielder leaving the field.
    for (Column *column : {&g_babel, &g_cactus, &g_rock}) {
        if (column->up && (column->wielder != wielder || held != column->weapon_item ||
                           (column->model.weapon && !column->model.Valid()))) {
            ColumnEnd(*column);
        }
    }

    if (!ungaga_out || (held != ITEM_WEAPON_BABEL_S_SPEAR && held != ITEM_WEAPON_CACTUS && held != ITEM_WEAPON_TERRA_SWORD)) {
        g_column_guard = 0;
        g_column_cast = false;
        g_rock_primed = false;
    } else if (BtActStatus.guard_mode != kGuarding) {
        g_column_guard = 0;
        g_column_cast = false;
    } else if (!g_column_cast) {
        g_column_guard++;

        if (held == ITEM_WEAPON_CACTUS && g_column_guard >= kCactusGuardSteps) {
            g_column_cast = true;
            RaiseCactus(held, wielder);
        } else if (held == ITEM_WEAPON_BABEL_S_SPEAR && !g_babel.up && g_column_guard >= kBabelGuardSteps) {
            g_column_cast = true;
            RaiseBabel(held, wielder);
        } else if (held == ITEM_WEAPON_TERRA_SWORD && !g_rock.up && g_column_guard >= kRockGuardSteps) {
            g_column_cast = true;
            g_rock_primed = (wielder == CHARA_XIAO ? NutModel() : RockModel()) != nullptr;
            setUnitAmbientAnime(15.0f, 1.0f, 80.0f, 150.0f, 20.0f);
        }
    }

    // The guard's charge tints the spear and the sword; primed, the Terra Sword stays green.
    if (ungaga_out && BtActStatus.guard_mode == kGuarding && !g_column_cast) {
        float tint[3];

        if (held == ITEM_WEAPON_BABEL_S_SPEAR && !g_babel.up) {
            Scaled(kBabelTint, static_cast<float>(g_column_guard) / kBabelGuardSteps, tint);
            fx::WeaponTint(tint);
        } else if (held == ITEM_WEAPON_TERRA_SWORD && !g_rock.up) {
            Scaled(kTerraTint, static_cast<float>(g_column_guard) / kRockGuardSteps, tint);
            fx::WeaponTint(tint);
        }
    }

    if (g_rock_primed && held == ITEM_WEAPON_TERRA_SWORD) {
        fx::WeaponTint(kTerraTint);
    }

    // Primed, the boulder waits for a lock-on.
    if (g_rock_primed && held == ITEM_WEAPON_TERRA_SWORD && !g_rock.up) {
        const int target = LockedTarget();

        if (target >= 0) {
            g_rock_primed = false;
            DropRock(target, held, wielder);
        }
    }

    if (g_babel.up) {
        BabelStep();
    }

    if (g_cactus.up) {
        CactusStep();
    }

    if (g_rock.up) {
        RockStep();
    }

    // The player is held out of what stands.
    for (const Column *column : {&g_babel, &g_cactus, &g_rock}) {
        if (!ColumnSolid(*column)) {
            continue;
        }

        const float keep = column->radius + 6.0f;
        const float dx = CharaMain.pos[0] - column->x;
        const float dz = CharaMain.pos[2] - column->z;
        const float distance = std::sqrt(dx * dx + dz * dz);

        if (distance < keep && distance > 0.01f) {
            CharaMain.SetPosition(column->x + dx / distance * keep, CharaMain.pos[1], column->z + dz / distance * keep);
        }
    }
}

/**
 * Dark Cloud Expanded: the rock's and the cactus's round shadows on the floor (GroundShadow),
 * gone as each starts to go. The rock's grows from nothing over the last 300 units of its fall.
 */
void ColumnShadow(const Column &column, float scale) {
    constexpr float kShadowDarkness = 0.6f;

    if (column.age > column.life) {
        return;
    }

    if (column.kind == ColumnKind::Rock) {
        constexpr float kShadowGrowDrop = 300.0f;
        constexpr float kShadowLeast = 0.01f;

        float share = 1.0f;

        if (column.falling || column.hopping) {
            share = std::clamp(1.0f - (column.fall_y - column.ground) / kShadowGrowDrop, kShadowLeast, 1.0f);
        }

        fx::GroundShadow(column.x, column.ground, column.z, share * column.radius, kShadowDarkness);
        return;
    }

    if (column.kind == ColumnKind::Cactus && !column.trees) {
        // The cactus's body, its arms and spikes left out, as Expanded's.
        constexpr float kCactusShadow = 1.8f;

        fx::GroundShadow(column.x, column.ground, column.z, kCactusShadow * scale, kShadowDarkness);
    }
}

void ColumnsDraw() {
    if (UserStatus == nullptr || ColumnWielder() < 0) {
        return;
    }

    for (Column *column : {&g_babel, &g_cactus, &g_rock}) {
        if (!column->up) {
            continue;
        }

        bool        solid = false;
        const float root = ColumnRoot(*column, solid);
        float       scale = column->scale;
        float       alpha = 1.0f;

        if (column->kind == ColumnKind::Cactus) {
            // Dark Cloud Expanded: a touch of squash and stretch, overshooting to 3.4x as it comes
            // up before it settles at 3x, then a half-second fade instead of sinking.
            constexpr float kPeakRatio = 3.4f / 3.0f;
            constexpr int   kSettleSteps = 12;

            const float bounce = std::clamp(static_cast<float>(column->age) / (kCactusRiseSteps + kSettleSteps), 0.0f, 1.0f);
            scale *= 1.0f + (kPeakRatio - 1.0f) * std::sin(3.14159265f * bounce);

            if (column->age > column->life) {
                alpha = 1.0f - static_cast<float>(column->age - column->life) / column->gone;
            }
        }

        // The nut fades away where it rests, as the cactus does.
        if (column->nut && !column->falling && !column->hopping && column->age > column->life) {
            alpha = 1.0f - static_cast<float>(column->age - column->life) / column->gone;
        }

        // The borrowed models' pictures go back in after a floor change.
        if (column->trees) {
            TreesModel();
        } else if (column->nut) {
            NutModel();
        }

        ColumnShadow(*column, scale);
        column->model.Draw(column->x + column->draw_dx * scale, root, column->z + column->draw_dz * scale, scale, column->yaw, false,
                           alpha);
    }
}

/**
 * Stops monster m's move this step when it would walk into a standing column.
 */
bool ColumnBlocksMonster(int m) {
    MONSTOR &monster = NowMonstorUnit->monster[m];
    float    at[4];
    MonsterPosition(m, at);
    const float next_x = at[0] + monster.movement[0] * monster.movement_speed;
    const float next_z = at[2] + monster.movement[2] * monster.movement_speed;

    for (const Column *column : {&g_babel, &g_cactus, &g_rock}) {
        if (!ColumnSolid(*column)) {
            continue;
        }

        const float keep = column->radius + monster.body_radius;
        const float now_dx = at[0] - column->x;
        const float now_dz = at[2] - column->z;
        const float next_dx = next_x - column->x;
        const float next_dz = next_z - column->z;

        // Only a step that brings it nearer, inside the column's keep.
        if (next_dx * next_dx + next_dz * next_dz < keep * keep &&
            next_dx * next_dx + next_dz * next_dz < now_dx * now_dx + now_dz * now_dz) {
            monster.movement[0] = 0.0f;
            monster.movement[1] = 0.0f;
            monster.movement[2] = 0.0f;
            monster.movement_speed = 0.0f;
            return true;
        }
    }

    return false;
}

// ── Angel Gear "Guardian Reflector" ─────────────────────────────────────────────────────────

// While Xiao guards with it, a copy of the slingshot twice its size stands 12 units in front of
// her, turned to the nearest enemy shot coming at her, else the nearest enemy. A shot that reaches
// it, or her, is caught and fired back from it at the nearest enemy, with the weapon's attack and
// the shot's own element and ailments. Enemy blows that land on it wear it down: the fifth breaks
// it, with the weapon-break sound, and it cannot stand again for four seconds. Enemies near it
// go for it in place of her. It grows in as it stands and shrinks away as it goes. Its health
// shows on the attack gauge, which refills over the four seconds once it breaks; each shot it
// fires back it draws first, pulling back and snapping forward.
constexpr float kShieldScale = 2.0f;
constexpr float kShieldAhead = 12.0f;
constexpr float kShieldHeight = 7.5f;
constexpr float kShieldCatch = 10.0f;      // a shot this near the pouch is caught
constexpr float kShieldBody = 8.0f;        // ...or this near Xiao
constexpr float kShieldWatch = 120.0f;     // shots this near are what it turns to
constexpr int   kShieldHits = 5;
constexpr int   kShieldCooldown = 4 * kStepsPerSecond;
constexpr int   kShieldHitGap = kStepsPerSecond / 2;  // one enemy's blow counts once in this
constexpr int   kReflectGap = kStepsPerSecond / 4;    // between two shots it fires back
constexpr float kShieldLure = 60.0f;       // enemies this near it go for it
constexpr int   kShieldGrowSteps = 10;
constexpr int   kShieldDrawSteps = kStepsPerSecond / 6; // the pull before each shot it fires back
constexpr int   kShieldSnapSteps = kStepsPerSecond / 15;
constexpr float kShieldPull = 2.5f;        // how far it draws back, at 1x
constexpr float kGaugeFull = 100.0f;

alignas(64) u_char g_reflect_frames[1024 * 1024];
BorrowedShot       g_reflect(g_reflect_frames, sizeof(g_reflect_frames));

struct CaughtShot {
    CSHOT_EFFECT *from;
};

FloorModel              g_shield_model;
bool                    g_shield_up = false;
float                   g_shield_pos[4] = {};
float                   g_shield_yaw = 0.0f;
int                     g_shield_hits = 0;
int                     g_shield_broken = 0;   // steps left before it may stand again
std::array<int, 16>     g_shield_hit_at{};
std::vector<CaughtShot> g_caught;
int                     g_reflect_ready = 0;
int                     g_shield_grow = 0;     // 0 to kShieldGrowSteps: how far it has grown in
float                   g_shield_last[4] = {}; // where it stood, while it shrinks away
int                     g_shield_pull = -1;    // steps into its draw before a shot, or -1
int                     g_shield_snap = 0;     // steps left of its snap forward after one
bool                    g_shield_gauge = false; // the attack gauge shows its health

bool ReflectorHeld() {
    return UserStatus != nullptr && UserStatus->cur_chara == CHARA_XIAO && EffectOf(CHARA_XIAO) == ITEM_WEAPON_ANGE_GEAR;
}

void ShieldReset() {
    g_shield_up = false;
    g_shield_model.root = nullptr;
    g_shield_hits = 0;
    g_shield_broken = 0;
    g_shield_hit_at = {};
    g_caught.clear();
    g_reflect_ready = 0;
    g_reflect.Drop();
    g_shield_grow = 0;
    g_shield_pull = -1;
    g_shield_snap = 0;
    g_shield_gauge = false;
}

/**
 * Dark Cloud Expanded: while the slingshot stands, or is broken and coming back, the attack
 * gauge is its health: a fifth gone with each blow, then refilling from empty over the cooldown.
 * Once it is neither, the gauge is the game's again.
 */
void ShieldGauge() {
    const bool held = ReflectorHeld();

    if (held && g_shield_up) {
        BtActStatus.action_gauge = kGaugeFull * static_cast<float>(kShieldHits - g_shield_hits) / kShieldHits;
        g_shield_gauge = true;
        return;
    }

    if (held && g_shield_broken > 0) {
        BtActStatus.action_gauge = kGaugeFull * (1.0f - static_cast<float>(g_shield_broken) / kShieldCooldown);
        g_shield_gauge = true;
        return;
    }

    // Handed back full, as a guard ends with the slingshot whole or ready again.
    if (g_shield_gauge) {
        g_shield_gauge = false;
        BtActStatus.action_gauge = kGaugeFull;
    }
}

float FlatDistance(const float *a, const float *b) {
    const float dx = a[0] - b[0];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

/**
 * The nearest live enemy to a point, or -1.
 */
int NearestMonster(const float *from, float reach) {
    int   best = -1;
    float best_distance = reach;

    for (int m = 0; m < 16 && NowMonstorUnit != nullptr; m++) {
        if (!ActiveMonster(m)) {
            continue;
        }

        float at[4];
        MonsterPosition(m, at);
        const float distance = FlatDistance(at, from);

        if (distance < best_distance) {
            best_distance = distance;
            best = m;
        }
    }

    return best;
}

void ReflectorStep() {
    if (g_shield_broken > 0) {
        g_shield_broken--;
    }

    if (g_shield_up) {
        g_shield_grow = std::min(g_shield_grow + 1, kShieldGrowSteps);
        sceVu0CopyVector(g_shield_last, g_shield_pos);
    } else if (g_shield_grow > 0) {
        g_shield_grow--;
    }

    const bool guarding = ReflectorHeld() && BtActStatus.guard_mode == kGuarding && g_shield_broken == 0;

    if (!guarding) {
        g_shield_up = false;

        if (!ReflectorHeld()) {
            g_shield_model.root = nullptr;
            g_shield_grow = 0;
            g_caught.clear();
        }
    } else {
        if (!g_shield_up) {
            g_shield_up = g_shield_model.TakeWeapon(NowWeapon);
            g_shield_model.weapon = true;
            g_shield_hits = 0;
            g_shield_grow = 0;
        }

        // Turned to the nearest shot coming at her, else the nearest enemy, else ahead.
        float bearing_to[4] = {};
        float nearest_shot = kShieldWatch;
        bool  found = false;

        if (NowShotEffect != nullptr) {
            for (CSHOT_EFFECT &effect : NowShotEffect->effect) {
                for (int slot = 0; slot < effect.slot_count && slot < 8; slot++) {
                    if (effect.active[slot] == 0) {
                        continue;
                    }

                    const float distance = FlatDistance(effect.chara[slot].pos, CharaMain.pos);

                    if (distance < nearest_shot) {
                        nearest_shot = distance;
                        sceVu0CopyVector(bearing_to, effect.chara[slot].pos);
                        found = true;
                    }
                }
            }
        }

        if (!found) {
            const int m = NearestMonster(CharaMain.pos, 1e9f);

            if (m >= 0) {
                MonsterPosition(m, bearing_to);
                found = true;
            }
        }

        g_shield_yaw = found ? std::atan2(bearing_to[0] - CharaMain.pos[0], bearing_to[2] - CharaMain.pos[2]) : CharaMain.rotation.y;
        g_shield_pos[0] = CharaMain.pos[0] + std::sin(g_shield_yaw) * kShieldAhead;
        g_shield_pos[1] = CharaMain.pos[1] + kShieldHeight;
        g_shield_pos[2] = CharaMain.pos[2] + std::cos(g_shield_yaw) * kShieldAhead;
        g_shield_pos[3] = 1.0f;

        // Every enemy shot that reaches the pouch or her is caught.
        if (NowShotEffect != nullptr) {
            for (CSHOT_EFFECT &effect : NowShotEffect->effect) {
                for (int slot = 0; slot < effect.slot_count && slot < 8; slot++) {
                    if (effect.active[slot] == 0) {
                        continue;
                    }

                    const float *at = effect.chara[slot].pos;
                    const bool   pouch = FlatDistance(at, g_shield_pos) < kShieldCatch;
                    const bool   body = FlatDistance(at, CharaMain.pos) < kShieldBody;

                    if (pouch || body) {
                        effect.OffEffect(slot);

                        if (g_caught.size() < 8) {
                            g_caught.push_back({&effect});
                        }
                    }
                }
            }
        }
    }

    ShieldGauge();

    if (g_shield_snap > 0) {
        g_shield_snap--;
    }

    // What was caught goes back out, one at a time, at the nearest enemy.
    if (g_reflect_ready > 0) {
        g_reflect_ready--;
    }

    if (g_caught.empty() || g_reflect_ready > 0 || !ReflectorHeld() || NowWeaponHave == nullptr) {
        g_shield_pull = -1;
        return;
    }

    // Standing, it draws before each shot.
    if (g_shield_up && ++g_shield_pull < kShieldDrawSteps) {
        return;
    }

    g_shield_pull = -1;
    g_shield_snap = g_shield_up ? kShieldSnapSteps : 0;

    CSHOT_EFFECT *from = g_caught.front().from;
    g_caught.erase(g_caught.begin());

    if (from->effect_data == nullptr || from->template_chara.frame == nullptr) {
        return;
    }

    if (!g_reflect.Valid() || g_reflect.source != from) {
        g_reflect.Build(*from);
    }

    float origin[4];
    sceVu0CopyVector(origin, g_shield_up ? g_shield_pos : CharaMain.pos);
    origin[1] = CharaMain.pos[1] + kShieldHeight;

    float     direction[4] = {std::sin(g_shield_yaw), 0.0f, std::cos(g_shield_yaw), 0.0f};
    const int m = NearestMonster(origin, 1e9f);

    if (m >= 0) {
        float at[4];
        MonsterPosition(m, at);
        at[1] += kShieldHeight;
        const float dx = at[0] - origin[0];
        const float dy = at[1] - origin[1];
        const float dz = at[2] - origin[2];
        const float length = std::max(0.01f, std::sqrt(dx * dx + dy * dy + dz * dz));
        direction[0] = dx / length;
        direction[1] = dy / length;
        direction[2] = dz / length;
    }

    g_reflect.Fire(origin, direction, NowWeaponHave->attack, CHARA_XIAO);

    // The draw counts toward the gap, so it fires back as often as before.
    g_reflect_ready = g_shield_up ? kReflectGap - kShieldDrawSteps : kReflectGap;
}

/**
 * An enemy's blow at a point: one that lands on the standing slingshot wears it down.
 */
void ShieldBlow(int attacker, const float *at, float radius) {
    if (!g_shield_up || attacker < 0 || attacker >= 16 || FlatDistance(at, g_shield_pos) > radius + 6.0f * kShieldScale) {
        return;
    }

    if (g_shield_hit_at[attacker] > g_step_count) {
        return;
    }

    g_shield_hit_at[attacker] = g_step_count + kShieldHitGap;

    if (++g_shield_hits >= kShieldHits) {
        g_shield_up = false;
        g_shield_grow = 0;
        g_shield_model.root = nullptr;
        g_shield_broken = kShieldCooldown;
        setUnitAmbientAnime(15.0f, 1.0f, 255.0f, 255.0f, 255.0f);
        SndSePlay(SE_WEAPON_BREAK, -1, 0);
    }
}

void ShieldDraw() {
    if (g_shield_grow <= 0 || !ReflectorHeld() || !g_shield_model.Valid()) {
        return;
    }

    const float  scale = kShieldScale * static_cast<float>(g_shield_grow) / kShieldGrowSteps;
    const float *at = g_shield_up ? g_shield_pos : g_shield_last;

    // Its draw: back toward Xiao as it pulls, then past its place a moment as it snaps.
    float pull = 0.0f;

    if (g_shield_up && g_shield_pull >= 0) {
        pull = kShieldPull * scale * static_cast<float>(g_shield_pull) / kShieldDrawSteps;
    } else if (g_shield_up && g_shield_snap > 0) {
        pull = -0.5f * kShieldPull * scale * static_cast<float>(g_shield_snap) / kShieldSnapSteps;
    }

    const float x = at[0] - std::sin(g_shield_yaw) * pull;
    const float z = at[2] - std::cos(g_shield_yaw) * pull;
    g_shield_model.Draw(x, at[1] - g_shield_model.tip * scale * 0.5f, z, scale, g_shield_yaw);
}

/**
 * Whether monster m goes for the standing slingshot in place of Xiao, and where it is.
 */
bool ShieldPulls(int m, float *target) {
    if (!g_shield_up || !ReflectorHeld() || !ActiveMonster(m) || NowMonstorUnit->monster[m].kind == MONSTER_KIND_NO_LOCK_ON) {
        return false;
    }

    float at[4];
    MonsterPosition(m, at);

    if (FlatDistance(at, g_shield_pos) > kShieldLure) {
        return false;
    }

    sceVu0CopyVector(target, g_shield_pos);
    target[1] = CharaMain.pos[1];
    return true;
}
// ── Floor cleared ───────────────────────────────────────────────────────────────────────────

// Once every monster on a floor is gone, a message says so. It waits half a second with the floor
// empty, so a floor still loading its monsters never shows it.
constexpr int kFloorClearSettleSteps = kStepsPerSecond / 2;
constexpr int kFloorClearMessageSteps = 4 * kStepsPerSecond;

bool g_floor_had_monsters = false;
bool g_floor_clear_shown = false;
int  g_floor_empty_steps = 0;

void FloorClearReset() {
    g_floor_had_monsters = false;
    g_floor_clear_shown = false;
    g_floor_empty_steps = 0;
}

void FloorClearStep() {
    if (NowMonstorUnit == nullptr || g_floor_clear_shown) {
        return;
    }

    bool any = false;

    for (const MONSTOR &monster : NowMonstorUnit->monster) {
        if (monster.state != -1 && monster.hp > 0) {
            any = true;
            break;
        }
    }

    if (any) {
        g_floor_had_monsters = true;
        g_floor_empty_steps = 0;
        return;
    }

    if (!g_floor_had_monsters || ++g_floor_empty_steps < kFloorClearSettleSteps) {
        return;
    }

    g_floor_clear_shown = true;
    ShowDungeonMessage("There is no sign of monsters\non this floor.", kFloorClearMessageSteps);
}

// ── Empty SynthSphere ───────────────────────────────────────────────────────────────────────

// An unfilled SynthSphere in a weapon's first hole raises a weapon below +5 to +5, each level
// gained adding 1 to Attack, Endurance, Speed and Magic. Taken out while the weapon is still +5,
// the weapon goes back. The levels given are kept in the record's spare byte, so a save keeps
// them; a level-up makes them the weapon's own.
constexpr int kSphereLevel = 5;

bool EmptySphere(const ATTACH_LIST &attach) {
    return attach.item_no == ITEM_ATTACH_SYNTHESIS_SPHERE && attach.sphere_weapon_no == 0;
}

char &SphereLevels(WEAPON_HAVE *weapon) {
    return weapon->unk_26[0];
}

void SphereBoost(WEAPON_HAVE *weapon, int levels) {
    weapon->level = static_cast<s16>(weapon->level + levels);
    weapon->attack = static_cast<s16>(weapon->attack + levels);
    weapon->endurance = static_cast<s16>(weapon->endurance + levels);
    weapon->speed = static_cast<s16>(weapon->speed + levels);
    weapon->magic = static_cast<s16>(weapon->magic + levels);
}

void EmptySphereAttach(WEAPON_HAVE *weapon, int hole) {
    if (hole != 0 || !EmptySphere(weapon->attach[0]) || weapon->level >= kSphereLevel) {
        return;
    }

    const int levels = kSphereLevel - weapon->level;
    SphereBoost(weapon, levels);
    SphereLevels(weapon) = static_cast<char>(levels);
}

void EmptySphereDetach(WEAPON_HAVE *weapon, int hole) {
    const int levels = SphereLevels(weapon);

    if (hole != 0 || !EmptySphere(weapon->attach[0]) || weapon->level != kSphereLevel || levels <= 0) {
        return;
    }

    SphereBoost(weapon, -levels);
    SphereLevels(weapon) = 0;
}

// ── Xiao's Divine Beast Title "Spirit Beast" ────────────────────────────────────────────────

// The charged shot leaves the slingshot as Xiao's cat form instead of a pellet: an animated copy
// of the town cat (gedit/s86/chara/c04cat.chr) that grows in over its first frames as it flies
// the pellet's line, falls under its own weight, lands, walks to the locked-on enemy (else the
// one nearest Xiao), crouches and leaps on it for the charged shot's damage with a heavy kick,
// then fades. With nothing to chase it sits and looks again every half second. It strikes at
// once whatever it touches in flight, fades after ten seconds, and goes with Xiao or the floor.
// Super Steve carrying the Divine Beast Title's sphere fires it too.
// Left out against Expanded: the wings, mask, cape and glow of its baked model (this is the
// plain town cat), the float-up leap clip (it leaps in its own leap clip), a floor probe (it walks
// on the height Xiao stood at), and the pellet shrinking away as the cat grows on it.

constexpr int   kCatTextureBlock = 0x31;
constexpr int   kCatFileBytes = 1536 * 1024;
constexpr int   kCatModelBytes = 2048 * 1024;
constexpr int   kCatGrowSteps = 6;
constexpr int   kCatFlySteps = 24;
constexpr float kCatFlySpeed = 4.0f;
constexpr float kCatFallGravity = 0.08f;
constexpr float kCatAirDrag = 0.96f;
constexpr int   kCatLandSteps = 33;
constexpr float kCatWalkSpeed = 0.9f;
constexpr float kCatPounceRange = 30.0f;
constexpr int   kCatReadySteps = 25;
constexpr int   kCatLeapSteps = 32;
constexpr float kCatLeapHeight = 6.4f;
constexpr float kCatTouch = 3.0f;
constexpr float kCatHitReach = 12.0f;
constexpr float kCatKick = 2.0f;
constexpr float kCatSeek = 300.0f;
constexpr int   kCatSitLook = kStepsPerSecond / 2;
constexpr int   kCatLifeSteps = 10 * kStepsPerSecond;
constexpr int   kCatFadeSteps = 14;

// The town cat's clips, in its own frames.
struct CatClip {
    int   start;
    int   end;
    float speed;
    bool  loops;
};

enum CatClipNo { kCatStand, kCatReady, kCatRun, kCatLeap, kCatLand, kCatWalk, kCatSit };

constexpr std::array<CatClip, 7> kCatClips = {{
    {10, 20, 0.1f, true},    // stand
    {95, 105, 0.4f, false},  // ready crouch
    {120, 136, 0.5f, true},  // run
    {205, 214, 0.5f, false}, // leap
    {215, 227, 0.36f, false},// land
    {60, 80, 1.0f, true},    // walk
    {30, 40, 0.1f, true},    // sit
}};

enum class CatPhase { None, Fly, Fall, Land, Seek, Ready, Leap, Sit, Fade };

struct Cat {
    CatPhase phase = CatPhase::None;
    float    pos[3] = {};
    float    vel[3] = {};
    float    yaw = 0.0f;
    float    scale = 1.0f;
    float    ground = 0.0f;
    float    from[3] = {};   // the leap's start
    float    to[3] = {};     // the leap's end
    int      age = 0;
    int      steps = 0;      // in the phase
    int      target = -1;
    int      damage = 0;
    int      clip = -1;
};

alignas(64) u_long128 g_cat_file[kCatFileBytes / 16];
alignas(64) u_long128 g_cat_work[kCatFileBytes / 16];
alignas(64) u_long128 g_cat_model_bytes[kCatModelBytes / 16];
int                   g_cat_file_size = 0;
bool                  g_cat_file_tried = false;
CDataAlloc2<1>        g_cat_alloc;
CCharacter            g_cat_chara;
int                   g_cat_loaded_floor = -1; // the floor the model was loaded for
int                   g_cat_motion_no = -1;
Cat                   g_cat;

/**
 * Loads the town cat for this floor: the file once a session, the model and its picture again on
 * each floor, whose texture blocks are rebuilt.
 */
bool CatModel() {
    if (UserStatus == nullptr) {
        return false;
    }

    if (!g_cat_file_tried) {
        g_cat_file_tried = true;
        char path[] = "gedit/s86/chara/c04cat.chr";

        if (LoadFileBG(path, g_cat_file, &g_cat_file_size) == 0 || g_cat_file_size <= 0 || g_cat_file_size > kCatFileBytes) {
            g_cat_file_size = 0;
        }

        std::printf("[Expanded] Divine Beast Title cat %s (%d bytes)\n", g_cat_file_size > 0 ? "loaded" : "NOT FOUND",
                    g_cat_file_size);
    }

    if (g_cat_file_size <= 0) {
        return false;
    }

    const int floor = UserStatus->cur_floor + 100 * selectMapNo;

    if (g_cat_loaded_floor == floor) {
        return g_cat_chara.frame != nullptr;
    }

    g_cat_loaded_floor = floor;

    // The pack is worked on in a copy, as the game loads every pack fresh from the disc.
    std::memcpy(g_cat_work, g_cat_file, static_cast<size_t>(g_cat_file_size));
    auto *pack = reinterpret_cast<u_int *>(g_cat_work);

    // Its configuration, whatever the pack calls it.
    char  cfg_name[64] = "info.cfg";
    int   cfg_size = 0;

    if (GetPackFile(pack, cfg_name, &cfg_size) == nullptr) {
        for (int index = 0; index < 64; index++) {
            char *name = nullptr;
            int   size = 0;

            if (GetPackFile(pack, index, &name, &size) == nullptr || name == nullptr) {
                break;
            }

            const std::string_view entry(name);

            if (entry.size() > 4 && entry.substr(entry.size() - 4) == ".cfg") {
                std::snprintf(cfg_name, sizeof(cfg_name), "%s", name);
                break;
            }
        }
    }

    TexManager.DeleteTextureBlock(kCatTextureBlock);
    g_cat_alloc.base = reinterpret_cast<u_char *>(g_cat_model_bytes);
    g_cat_alloc.used = 0;
    g_cat_alloc.limit = kCatModelBytes / 16;
    g_cat_chara.Initialize();
    g_cat_chara.LoadPackData3(pack, cfg_name, &g_cat_alloc, kCatTextureBlock, &g_cat_alloc, 1, 0);

    g_cat_motion_no = g_cat_chara.motion_start[0] >= 0 ? g_cat_chara.motion_start[0] : 0;
    std::printf("[Expanded] Divine Beast Title cat model %s (%s, motion %d, %d of %d quads)\n",
                g_cat_chara.frame != nullptr ? "ready" : "FAILED", cfg_name, g_cat_motion_no, g_cat_alloc.used,
                g_cat_alloc.limit);
    return g_cat_chara.frame != nullptr;
}

/**
 * Plays one of the cat's clips through its first motion, whose frame window is set to the clip.
 */
void CatPlay(int clip) {
    if (g_cat.clip == clip) {
        return;
    }

    g_cat.clip = clip;
    MOTION_INFO *info = g_cat_chara.motion_type.motion_info;

    if (info == nullptr) {
        return;
    }

    const CatClip &want = kCatClips[clip];
    info[0].start = want.start;
    info[0].end = want.end;
    info[0].speed = want.speed;
    g_cat_chara.SetMotion(g_cat_motion_no, want.loops ? 4 : 4 | 2);
}

void CatReset() {
    g_cat = {};
    g_cat_chara.fade_out = 0;
}

void CatFade() {
    g_cat.phase = CatPhase::Fade;
    g_cat.steps = 0;
    g_cat_chara.fade[3] = 0.0f;
    g_cat_chara.fade_out = 1;
}

/**
 * The enemy the cat goes for: the locked-on one, else the one nearest Xiao. -1 for none.
 */
int CatTarget() {
    const int locked = LockedTarget();

    if (locked >= 0 && ActiveMonster(locked)) {
        return locked;
    }

    int   best = -1;
    float best_distance = kCatSeek * kCatSeek;

    for (int m = 0; m < 16 && NowMonstorUnit != nullptr; m++) {
        if (!ActiveMonster(m)) {
            continue;
        }

        float at[4];
        MonsterPosition(m, at);
        const float dx = at[0] - CharaMain.pos[0];
        const float dz = at[2] - CharaMain.pos[2];

        if (dx * dx + dz * dz < best_distance) {
            best_distance = dx * dx + dz * dz;
            best = m;
        }
    }

    return best;
}

void CatStrike(int m) {
    const float origin[4] = {g_cat.pos[0], g_cat.pos[1], g_cat.pos[2], 1.0f};
    PlantHitNear(m, g_cat.pos[0], g_cat.pos[2], kCatHitReach, g_cat.damage, origin, kCatKick, false);
    CatFade();
}

void CatFace(float dx, float dz) {
    if (dx * dx + dz * dz > 1e-4f) {
        g_cat.yaw = std::atan2(dx, dz);
    }
}

bool CatLaunch(const float *world, const float *vector, int damage) {
    if (!CatModel()) {
        return false;
    }

    const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);

    if (length < 1e-3f) {
        return false;
    }

    g_cat = {};
    g_cat.phase = CatPhase::Fly;

    for (int axis = 0; axis < 3; axis++) {
        g_cat.pos[axis] = world[axis];
        g_cat.vel[axis] = vector[axis] / length * kCatFlySpeed;
    }

    g_cat.ground = CharaMain.pos[1];
    g_cat.damage = std::max(1, damage);
    g_cat.scale = 0.05f;
    CatFace(g_cat.vel[0], g_cat.vel[2]);
    g_cat_chara.fade_out = 0;
    g_cat_chara.fade[0] = 1.0f;
    CatPlay(kCatRun);
    return true;
}

void CatStep() {
    if (g_cat.phase == CatPhase::None) {
        return;
    }

    if (UserStatus == nullptr || UserStatus->cur_chara != CHARA_XIAO || NowMonstorUnit == nullptr) {
        CatReset();
        return;
    }

    g_cat.age++;
    g_cat.steps++;

    if (g_cat.phase != CatPhase::Fade && g_cat.age > kCatLifeSteps) {
        CatFade();
    }

    switch (g_cat.phase) {
        case CatPhase::Fly:
        case CatPhase::Fall: {
            g_cat.scale = std::min(1.0f, static_cast<float>(g_cat.age) / kCatGrowSteps);

            if (g_cat.phase == CatPhase::Fall) {
                g_cat.vel[0] *= kCatAirDrag;
                g_cat.vel[2] *= kCatAirDrag;
                g_cat.vel[1] -= kCatFallGravity;
            }

            for (int axis = 0; axis < 3; axis++) {
                g_cat.pos[axis] += g_cat.vel[axis];
            }

            // Whatever it flies into, it strikes.
            for (int m = 0; m < 16; m++) {
                if (ActiveMonster(m) && InsideMonster(m, g_cat.pos, kCatTouch)) {
                    CatStrike(m);
                    return;
                }
            }

            if (g_cat.phase == CatPhase::Fly && g_cat.steps >= kCatFlySteps) {
                g_cat.phase = CatPhase::Fall;
                g_cat.steps = 0;
                g_cat.vel[1] = std::min(g_cat.vel[1], 0.0f);
                CatPlay(kCatLeap);
            }

            if (g_cat.pos[1] <= g_cat.ground) {
                g_cat.pos[1] = g_cat.ground;
                g_cat.phase = CatPhase::Land;
                g_cat.steps = 0;
                CatPlay(kCatLand);
            }

            break;
        }

        case CatPhase::Land:
            if (g_cat.steps >= kCatLandSteps) {
                g_cat.phase = CatPhase::Seek;
                g_cat.steps = 0;
            }

            break;

        case CatPhase::Sit:
            if (g_cat.steps < kCatSitLook) {
                break;
            }

            g_cat.phase = CatPhase::Seek;
            g_cat.steps = 0;
            [[fallthrough]];

        case CatPhase::Seek: {
            if (g_cat.target < 0 || !ActiveMonster(g_cat.target)) {
                g_cat.target = CatTarget();
            }

            if (g_cat.target < 0) {
                g_cat.phase = CatPhase::Sit;
                g_cat.steps = 0;
                CatPlay(kCatSit);
                break;
            }

            float at[4];
            MonsterPosition(g_cat.target, at);
            const float dx = at[0] - g_cat.pos[0];
            const float dz = at[2] - g_cat.pos[2];
            const float distance = std::sqrt(dx * dx + dz * dz);
            CatFace(dx, dz);

            if (distance <= kCatPounceRange) {
                g_cat.phase = CatPhase::Ready;
                g_cat.steps = 0;
                CatPlay(kCatReady);
                break;
            }

            g_cat.pos[0] += dx / distance * kCatWalkSpeed;
            g_cat.pos[2] += dz / distance * kCatWalkSpeed;
            CatPlay(kCatWalk);
            break;
        }

        case CatPhase::Ready: {
            if (!ActiveMonster(g_cat.target)) {
                g_cat.phase = CatPhase::Seek;
                g_cat.steps = 0;
                break;
            }

            float at[4];
            MonsterPosition(g_cat.target, at);
            CatFace(at[0] - g_cat.pos[0], at[2] - g_cat.pos[2]);

            if (g_cat.steps >= kCatReadySteps) {
                g_cat.phase = CatPhase::Leap;
                g_cat.steps = 0;
                std::memcpy(g_cat.from, g_cat.pos, sizeof(g_cat.from));
                AimAt(g_cat.target, g_cat.pos, at);
                std::memcpy(g_cat.to, at, sizeof(g_cat.to));
                CatPlay(kCatLeap);
            }

            break;
        }

        case CatPhase::Leap: {
            // It homes on the enemy as it moves, along an arc to the point it aimed for.
            if (ActiveMonster(g_cat.target)) {
                float at[4];
                AimAt(g_cat.target, g_cat.pos, at);
                std::memcpy(g_cat.to, at, sizeof(g_cat.to));
            }

            const float t = std::min(1.0f, static_cast<float>(g_cat.steps) / kCatLeapSteps);

            for (int axis = 0; axis < 3; axis++) {
                g_cat.pos[axis] = g_cat.from[axis] + (g_cat.to[axis] - g_cat.from[axis]) * t;
            }

            g_cat.pos[1] += kCatLeapHeight * 4.0f * t * (1.0f - t);
            CatFace(g_cat.to[0] - g_cat.from[0], g_cat.to[2] - g_cat.from[2]);

            if (ActiveMonster(g_cat.target) && (t >= 1.0f || InsideMonster(g_cat.target, g_cat.pos, kCatTouch))) {
                CatStrike(g_cat.target);
            } else if (t >= 1.0f) {
                g_cat.pos[1] = std::max(g_cat.ground, g_cat.pos[1]);
                g_cat.phase = CatPhase::Land;
                g_cat.steps = 0;
                g_cat.target = -1;
                CatPlay(kCatLand);
            }

            break;
        }

        case CatPhase::Fade:
            if (g_cat.steps >= kCatFadeSteps) {
                CatReset();
            }

            break;

        case CatPhase::None:
            break;
    }
}

void CatDraw() {
    if (g_cat.phase == CatPhase::None || g_cat_chara.frame == nullptr || UserStatus == nullptr ||
        UserStatus->cur_chara != CHARA_XIAO) {
        return;
    }

    const float size = std::max(0.01f, g_cat.scale);
    g_cat_chara.SetPosition(g_cat.pos[0], g_cat.pos[1], g_cat.pos[2]);
    g_cat_chara.SetRotation(0.0f, g_cat.yaw, 0.0f);
    g_cat_chara.SetScale(size, size, size);
    TexManager.ReloadTexture(Vif1Packet, kCatTextureBlock);
    g_cat_chara.Step();
    g_cat_chara.Draw();
}

} // namespace

bool IsPockleSlot(int monster_no) {
    return IsPocklePos(monster_no);
}

bool QuietModelLoad() {
    return g_quiet_load;
}

// ── Who holds what ──────────────────────────────────────────────────────────────────────────

int ToanWeapon() {
    if (UserStatus == nullptr || NowWeaponHave == nullptr || UserStatus->cur_chara != CHARA_TOAN) {
        return -1;
    }

    return NowWeaponHave->item_no;
}

int OwnedCount(int item_no) {
    CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    if (status == nullptr) {
        return 0;
    }

    int count = 0;

    for (const auto &weapons : status->chara_weapons) {
        for (int slot = 0; slot < 10; slot++) {
            if (weapons[slot].item_no == item_no) {
                count++;
            }
        }
    }

    if (CStockItem *stock = SaveData->GetStockItem(); stock != nullptr) {
        for (const WEAPON_HAVE &weapon : stock->weapons) {
            if (weapon.item_no == item_no) {
                count++;
            }
        }
    }

    return count;
}

bool Owned(int item_no) {
    return OwnedCount(item_no) > 0;
}

bool SwiftStrikes(int item_no) {
    return Is(item_no, {ITEM_WEAPON_SHAMSHIR, ITEM_WEAPON_DUSACK, ITEM_WEAPON_7BRANCH_SWORD, ITEM_WEAPON_ATLAMILLIA_SWORD, ITEM_WEAPON_CHRONICLE_SWORD});
}

bool QuickDraw(int item_no) {
    return Is(item_no, {ITEM_WEAPON_SMALL_SWORD, ITEM_WEAPON_TSUKIKAGE, ITEM_WEAPON_HEAVEN_S_CLOUD});
}

bool MoonlitFocus(int item_no) {
    return Is(item_no, {ITEM_WEAPON_TSUKIKAGE, ITEM_WEAPON_HEAVEN_S_CLOUD});
}

bool GuardCrush(int item_no) {
    return Is(item_no, {ITEM_WEAPON_DARK_CLOUD, ITEM_WEAPON_7THHEAVEN});
}

bool NoFoolsGold(int item_no) {
    return Is(item_no, {ITEM_WEAPON_DUSACK, ITEM_WEAPON_BRAVE_ARK});
}

bool HeavyHand(int item_no) {
    return Is(item_no, {ITEM_WEAPON_BASELARD, ITEM_WEAPON_CLAYMORE});
}

bool Sanctifier(int item_no) {
    return item_no == ITEM_WEAPON_CROSS_HINDER;
}

bool SolarHarvest(int item_no) {
    return Is(item_no, {ITEM_WEAPON_SUN_SWORD, ITEM_WEAPON_BIG_BANG, ITEM_WEAPON_SWORD_OF_ZEUS});
}

bool KeepsUndeadDown(int item_no) {
    return Is(item_no, {ITEM_WEAPON_BONE_RAPIER, ITEM_WEAPON_BONE_SLINGSHOT, ITEM_WEAPON_CROSS_HINDER});
}

int FineFareTier(int item_no) {
    switch (item_no) {
        case ITEM_WEAPON_SAX:
            return 1;
        case ITEM_WEAPON_DUSACK:
            return 2;
        case ITEM_WEAPON_7BRANCH_SWORD:
        case ITEM_WEAPON_ATLAMILLIA_SWORD:
        case ITEM_WEAPON_CHRONICLE_SWORD:
            return 3;
        default:
            return 0;
    }
}

bool LongLockOn(int item_no) {
    return Is(item_no, {ITEM_WEAPON_CROSS_HINDER, ITEM_WEAPON_BIG_BANG, ITEM_WEAPON_SWORD_OF_ZEUS});
}

// ── Toan's swings ───────────────────────────────────────────────────────────────────────────

void ToanSwingStep(float &time) {
    const int held = ToanWeapon();

    // Quick Draw: once the opening swing's step-in has run, the wind-up is skipped and the
    // motion goes straight to the weapon trail, the swing sound and the hit.
    constexpr float kWindUpSettled = 820.5f;
    constexpr float kTrailStart = 824.0f;

    if (QuickDraw(held) && BtActStatus.action_no == 0x24 && time > kWindUpSettled && time < kTrailStart) {
        CharaMain.motion_type.state.time = kTrailStart;
        time = kTrailStart;
    }

    // Swift Strikes: the five combo motions play at 0.4 frames a step instead of 0.3. Each is
    // checked by its frames first, so a motion list that is not Toan's is never touched.
    struct Swing {
        int motion;
        int start;
        int end;
    };

    constexpr std::array<Swing, 5> kSwings = {
        {{36, 820, 830}, {37, 830, 838}, {38, 838, 847}, {39, 847, 857}, {40, 856, 884}}
    };
    constexpr float kStock = 0.3f;
    constexpr float kFast = 0.4f;

    MOTION_INFO *motions = CharaMain.motion_type.motion_info;

    if (motions == nullptr) {
        return;
    }

    const float want = SwiftStrikes(held) ? kFast : kStock;

    for (const Swing &swing : kSwings) {
        MOTION_INFO &motion = motions[swing.motion];

        if (motion.start != swing.start || motion.end != swing.end) {
            continue;
        }

        if (std::fabs(motion.speed - kStock) > 0.01f && std::fabs(motion.speed - kFast) > 0.01f) {
            continue;
        }

        motion.speed = want;
    }
}

/**
 * Super Steve's shot versions of two sword abilities, each step Xiao is out. Swift Strikes (a
 * Shamshir, Dusack, 7 Branch Sword, Atlamillia Sword, Chronicle Sword or Partisan sphere): her
 * draw plays 1.6 times as fast and her shoot at 0.95 frames a step, the fastest that still fires
 * (Shamshir.cs DriveSphere). Quick Draw (a Small Sword, Tsukikage or Heaven's Cloud sphere): the
 * gauge between shots refills at once (SmallSwordSphere.cs); the release fires at once through
 * QuickDrawRelease.
 */
void XiaoSphereStep() {
    struct Swing {
        int   motion;
        int   start;
        int   end;
        float fast;
    };

    constexpr std::array<Swing, 2> kShots = {{{11, 240, 251, 0.7f * 1.6f}, {13, 251, 255, 0.95f}}};
    constexpr float                kStock = 0.7f;

    if (UserStatus == nullptr || UserStatus->cur_chara != CHARA_XIAO) {
        return;
    }

    const int held = EffectOf(CHARA_XIAO);

    if (QuickDraw(held) && BtActStatus.action_on == 0) {
        BtActStatus.action_gauge = 100.0f;
    }

    MOTION_INFO *motions = CharaMain.motion_type.motion_info;

    if (motions == nullptr) {
        return;
    }

    const bool swift = SwiftStrikes(held) || held == ITEM_WEAPON_PARTISAN;

    for (const Swing &shot : kShots) {
        MOTION_INFO &motion = motions[shot.motion];

        // Only Xiao's own shot motions, at a speed this or the game set.
        if (motion.start != shot.start || motion.end != shot.end) {
            continue;
        }

        if (std::fabs(motion.speed - kStock) > 0.01f && std::fabs(motion.speed - shot.fast) > 0.01f) {
            continue;
        }

        motion.speed = swift ? shot.fast : kStock;
    }
}

int ToanAttack(int attack) {
    // Spring's Blessing: the blessed Kitchen Knife hits twice as hard.
    if (ToanWeapon() == ITEM_WEAPON_KITCHEN_KNIFE && g_knife_bless_steps > 0) {
        return attack * 2;
    }

    return attack;
}

float LockOnRate() {
    // Sanctifier's reach, which Big Bang and the Sword of Zeus carry on.
    if (LongLockOn(ActiveEffect())) {
        return 2.0f;
    }

    // Flamingo: Xiao locks on from twice as far, and so do the Dragon's Y's line.
    if (Is(EffectOf(CHARA_XIAO), {ITEM_WEAPON_FLAMINGO, ITEM_WEAPON_DRAGON_S_Y, ITEM_WEAPON_DIVINEBEASTTITLE,
                                ITEM_WEAPON_ANGEL_SHOOTER, ITEM_WEAPON_ANGE_GEAR})) {
        return 2.0f;
    }

    return 1.0f;
}

int HeldBy(int chara) {
    if (UserStatus == nullptr || NowWeaponHave == nullptr || UserStatus->cur_chara != chara) {
        return -1;
    }

    return NowWeaponHave->item_no;
}

int SphereWeapon(const WEAPON_HAVE *weapon) {
    if (weapon == nullptr || weapon->item_no != ITEM_WEAPON_SUPER_STEVE) {
        return -1;
    }

    // A weapon holds one SynthSphere at most; the weapon it was made from names the effect.
    for (const ATTACH_LIST &attach : weapon->attach) {
        if (attach.item_no == ITEM_ATTACH_SYNTHESIS_SPHERE && attach.sphere_weapon_no > 0 &&
            attach.sphere_weapon_no != ITEM_WEAPON_SUPER_STEVE) {
            return attach.sphere_weapon_no;
        }
    }

    return -1;
}

int EffectOf(int chara) {
    const int held = HeldBy(chara);

    if (chara == CHARA_XIAO && held == ITEM_WEAPON_SUPER_STEVE) {
        const int sphere = SphereWeapon(NowWeaponHave);

        if (sphere > 0) {
            return sphere;
        }
    }

    return held;
}

// Super Steve's sphere on show (Expanded's SuperSteve.cs DriveSphereIcon and DriveSphereSprite):
// the sphere weapon's icon over Steve on the dungeon HUD, and a slingshot sphere's own pellet.
// The icon is copied into a spare cell of the HUD sheet (itempack, which stays all floor) each
// time the game copies the equipped weapon's there, and drawn 20 across just above Steve's
// raised hands, fading with the HUD.
constexpr int kSphereIconCellX = 64;   // blank in the art and outside every copy the game makes
constexpr int kSphereIconCellY = 32;
constexpr int kSphereIconX = 0x1D;     // Steve's own icon is at (0x1D, 0x1A4), 32 across
constexpr int kSphereIconY = 0x1A4 - 21;
constexpr int kSphereIconSize = 20;

int g_icon_sphere = -1;    // the sphere whose icon is in the cell now
int g_pellet_sphere = -1;  // the sphere the pellet sprite was last logged for

/**
 * The sphere on the weapon Xiao has equipped, when she is out with Super Steve; else -1.
 */
int EquippedSphere() {
    if (UserStatus == nullptr || UserStatus->cur_chara != CHARA_XIAO) {
        return -1;
    }

    const int slot = UserStatus->equipped_weapon_slot[CHARA_XIAO];

    if (slot < 0 || slot >= 11) {
        return -1;
    }

    return SphereWeapon(&UserStatus->chara_weapons[CHARA_XIAO][slot]);
}

void SphereIconCopy() {
    const int sphere = EquippedSphere();

    if (sphere <= 0) {
        g_icon_sphere = -1;
        return;
    }

    const COM_ITEM_INFO *info = GetCommonItemInfo(sphere);
    char                 source[] = "wepicon";
    char                 target[] = "itempack";

    // The icon sheet is only there while the game copies from it.
    if (info == nullptr || info->icon_index < 0 || TexManager.GetTexture(source, -1) == nullptr ||
        TexManager.GetTexture(target, -1) == nullptr) {
        return;
    }

    const int icon = info->icon_index;
    setItemToReserved(source, (icon % 8) * 32, (icon >> 3) * 32, target, kSphereIconCellX, kSphereIconCellY);

    if (g_icon_sphere != sphere) {
        std::printf("[SuperSteve] sphere icon: weapon %d (icon %d)\n", sphere, icon);
    }

    g_icon_sphere = sphere;
}

void SphereIconDraw(int alpha) {
    // Only the icon copied for the sphere she has on now.
    if (g_icon_sphere <= 0 || EquippedSphere() != g_icon_sphere) {
        return;
    }

    char      name[] = "itempack";
    CTexture *icons = TexManager.GetTexture(name, -1);

    if (icons == nullptr) {
        return;
    }

    set2DSprite(Vif1Packet, icons, CRect_i_(kSphereIconX, kSphereIconY, kSphereIconSize, kSphereIconSize),
                CRect_i_(kSphereIconCellX, kSphereIconCellY, 0x20, 0x20), static_cast<unsigned char>(alpha));
}

int PelletSpriteItem(int item_no) {
    if (item_no != ITEM_WEAPON_SUPER_STEVE) {
        return item_no;
    }

    // A slingshot's sphere brings its pellet: the sheet has one cell a slingshot.
    const int  sphere = SphereWeapon(NowWeaponHave);
    const bool slingshot = sphere >= ITEM_WEAPON_WOODEN_SLINGSHOT && sphere <= ITEM_WEAPON_ANGE_GEAR;

    if (sphere != g_pellet_sphere) {
        g_pellet_sphere = sphere;

        if (slingshot) {
            std::printf("[SuperSteve] pellet sprite: the sphere weapon's (item %d)\n", sphere);
        }
    }

    return slingshot ? sphere : item_no;
}

void UngagaSwingStep() {
    // Partisan: the three combo motions play a third faster, Swift Strikes' factor. The
    // second swing hands over within half a frame of its end, so its step stays under 0.5.
    struct Swing {
        int   motion;
        int   start;
        int   end;
        float stock;
    };

    constexpr std::array<Swing, 3> kSwings = {
        {{0x25, 670, 678, 0.28f}, {0x26, 678, 694, 0.32f}, {0x27, 695, 714, 0.32f}}
    };
    constexpr float kFactor = 4.0f / 3.0f;

    MOTION_INFO *motions = CharaMain.motion_type.motion_info;

    if (motions == nullptr || UserStatus == nullptr || UserStatus->cur_chara != CHARA_UNGAGA) {
        return;
    }

    const bool fast = HeldBy(CHARA_UNGAGA) == ITEM_WEAPON_PARTISAN;

    for (const Swing &swing : kSwings) {
        MOTION_INFO &motion = motions[swing.motion];

        if (motion.start != swing.start || motion.end != swing.end) {
            continue;
        }

        const float quick = swing.stock * kFactor;

        if (std::fabs(motion.speed - swing.stock) > 0.01f && std::fabs(motion.speed - quick) > 0.01f) {
            continue;
        }

        motion.speed = fast ? quick : swing.stock;
    }
}

int GoroAttack(int attack) {
    // Inferno: the hammer hits harder the worse Goro is doing, by the share of life he is
    // missing plus half the share of water, both of his attack capped at 350.
    if (HeldBy(CHARA_GORO) != ITEM_WEAPON_INFERNO) {
        return attack;
    }

    constexpr int kAttackCap = 350;

    const int   chara = CHARA_GORO;
    const float total = static_cast<float>(std::min(attack, kAttackCap));
    const float max_hp = std::max<float>(1.0f, UserStatus->max_hp[chara]);
    const float max_water = std::max(1.0f, UserStatus->water_max[chara]);
    const float hp_missing = 1.0f - static_cast<float>(UserStatus->hp[chara]) / max_hp;
    const float water_missing = 1.0f - UserStatus->water_now[chara] / max_water;
    const float boost = total * (std::max(0.0f, hp_missing) + 0.5f * std::max(0.0f, water_missing));

    return static_cast<int>(total + boost);
}

float EnemyDefense(float defense, const MONSTOR &monster, int owner) {
    // Javelin: a sea enemy has no defense against it.
    if (owner >= 0 && EffectOf(owner) == ITEM_WEAPON_JAVELIN && monster.attachment_kind == MONSTER_FAMILY_SEA) {
        return 0.0f;
    }

    return defense;
}

void OnEnemyHit(MONSTOR &monster, int owner, bool poisoned) {
    if (owner < 0) {
        return;
    }

    const int held = EffectOf(owner);

    // Snail: one hit in twenty leaves the enemy stuck in its slime.
    constexpr int kSnailGooPercent = 5;
    constexpr int kGooFrames = 180;

    if (held == ITEM_WEAPON_SNAIL && std::rand() % 100 < kSnailGooPercent && monster.stop_timer == 0 &&
        monster.poison_timer == 0 && monster.anger_timer == 0) {
        monster.slow_timer = kGooFrames;
    }

    // Supernova: one hit in nine puts one of four ailments on the enemy at random: stop, poison,
    // anger or goo.
    constexpr int kSupernovaPercent = 11;
    constexpr int kSupernovaFrames = 300;
    constexpr int kSupernovaGooFrames = 180;

    if (held == ITEM_WEAPON_SUPERNOVA && std::rand() % 100 < kSupernovaPercent) {
        switch (std::rand() % 4) {
            case 0:
                monster.stop_timer = kSupernovaFrames;
                break;
            case 1:
                monster.poison_timer = 1;
                break;
            case 2:
                monster.anger_timer = kSupernovaFrames;
                break;
            default:
                monster.slow_timer = kSupernovaGooFrames;
                break;
        }
    }

    // Tall Hammer: each hit shrinks the enemy by a tenth of its size, down to 30%.
    constexpr float kShrinkStep = 0.1f;
    constexpr float kShrinkLeast = 0.3f;

    if (held == ITEM_WEAPON_TALL_HAMMER && NowMonstorUnit != nullptr) {
        const int index = static_cast<int>(&monster - NowMonstorUnit->monster);

        if (index >= 0 && index < 16) {
            CCharacter &body = NowMonstorUnit->chara[index][0];
            float       size[4];
            body.GetScale(size);

            if (size[0] > kShrinkLeast || size[1] > kShrinkLeast || size[2] > kShrinkLeast) {
                for (int axis = 0; axis < 3; axis++) {
                    size[axis] = std::max(kShrinkLeast, size[axis] - kShrinkStep);
                }

                body.SetScale(size);
            }
        }
    }

    // Cold Storage: one hit in twenty freezes every enemy on the floor but the ice kind, for
    // five seconds, and Goro with them for three. One roll a step, however many it hits.
    constexpr int kTunaPercent = 5;
    constexpr int kTunaEnemyFrames = 300;
    constexpr int kTunaSelfFrames = 180;

    if (held == ITEM_WEAPON_FROZEN_TUNA && NowMonstorUnit != nullptr && g_tuna_rolled_step != g_step_count) {
        g_tuna_rolled_step = g_step_count;

        if (std::rand() % 100 < kTunaPercent) {
            for (MONSTOR &other : NowMonstorUnit->monster) {
                if (other.state == 2 && !IceMonster(other)) {
                    other.stop_timer = kTunaEnemyFrames;
                }
            }

            BtSetStatusErr(AILMENT_FREEZE);

            if ((UserStatus->ailments[owner] & AILMENT_FREEZE) != 0) {
                UserStatus->ailment_frames[owner] = kTunaSelfFrames;
            }
        }
    }

    // Scorpion: when its poison takes hold, Ungaga's own poison is drawn out and the weapon
    // soaks up half the enemy's ABS.
    if (held == ITEM_WEAPON_SCORPION && poisoned && NowWeaponHave != nullptr) {
        UserStatus->ailments[owner] &= ~AILMENT_POISON;

        const int max_exp = GetWeaponMaxExp(NowWeaponHave);

        if (NowWeaponHave->experience < max_exp) {
            NowWeaponHave->experience = static_cast<s16>(std::min(max_exp, NowWeaponHave->experience + monster.exp / 2));
        }
    }
}

void OnEnemyDamaged(const MONSTOR &monster, int owner, int amount) {
    ChronicleSplash(monster, owner, amount);

    if (amount > 0) {
        MirageHurt(monster);
    }

    // Absorb: the Cactus's hits fill Ungaga's water by a tenth of the damage they deal, ten
    // to a drop, up to his gauge; rock, metal and undead foes give nothing. One hit a step
    // counts, however many it lands.
    constexpr std::array<s16, 20> kDry = {1, 3, 5, 25, 30, 31, 32, 33, 40, 55, 64, 69, 70, 77, 90, 91, 303, 317, 318, 319};
    static int last_step = -1;

    if ((owner != CHARA_UNGAGA && owner != CHARA_XIAO) || amount <= 0 || EffectOf(owner) != ITEM_WEAPON_CACTUS ||
        last_step == g_step_count || std::ranges::find(kDry, monster.name_no) != kDry.end()) {
        return;
    }

    last_step = g_step_count;

    float      &water = UserStatus->water_now[owner];
    const float most = UserStatus->water_max[owner];

    if (water < most) {
        water = std::min(most, water + static_cast<float>(amount) / 10.0f);
    }
}

float WearAmount(float amount) {
    // Steel Slingshot: once its WHP is down to a tenth, each shot wears it half as much.
    constexpr float kLowShare = 0.1f;

    const int held = EffectOf(CHARA_XIAO);

    if (held == ITEM_WEAPON_STEEL_SLINGSHOT &&
        NowWeaponHave->durability_f <= kLowShare * static_cast<float>(NowWeaponHave->durability)) {
        return amount * 0.5f;
    }

    return amount;
}

float FlameSpacing() {
    // Skunk: the flamethrower reaches twice as far.
    return HeldBy(CHARA_OSMOND) == ITEM_WEAPON_SKUNK ? 4.0f : 2.0f;
}

float ExtraChargePerStep() {
    // Moonlit Focus: the charge gathers twice as fast, so the lunge is ready in a quarter of a
    // second and the whirlwind in three quarters.
    return MoonlitFocus(ToanWeapon()) ? 1.0f / kStepsPerSecond : 0.0f;
}

void ToanKickBack(float *origin, float speed, float decay) {
    // Heavy Hand: every hit throws the enemy about 17 units, a third of Big Bang's blast at the
    // blast's own fade (distance is speed squared over twice the decay).
    constexpr float kHeavyHandSpeed = 2.02f;
    constexpr float kHeavyHandDecay = 0.12f;

    if (HeavyHand(ToanWeapon())) {
        speed = kHeavyHandSpeed;
        decay = kHeavyHandDecay;
    }

    NowColData->SetKickBack(origin, speed, decay, 2);

    // The Sword of Zeus's chain: each swing of the combo strikes again.
    if (g_zeus_chain) {
        ZeusChainHit();
    }

    // Solar Flash: the primed blade lets go as this swing lands.
    SolarFlash();
}

float ToanHitSphere(float *hit_pos, float radius) {
    // Greatsword: the grown blade carries its hit bone out to 1.8 times as far from the hand,
    // which would sail over anything close. One sphere spanning the stock reach to the grown
    // one keeps both: its centre midway along that span, its radius widened by half of it.
    // Typhoon's grown blade does the same.
    float scale = 1.0f;
    float bone = 0.0f;

    if (ToanWeapon() == ITEM_WEAPON_CLAYMORE) {
        scale = kClaymoreScale;
        bone = kClaymoreHitBone;
    } else if (ToanWeapon() == ITEM_WEAPON_HEAVEN_S_CLOUD) {
        scale = g_typhoon_factor;
        bone = kHeavensCloudHitBone;
    }

    if (scale <= 1.0f || g_blade.frame == nullptr) {
        return radius;
    }

    sceVu0FVECTOR hand;
    sceVu0FVECTOR origin = {0.0f, 0.0f, 0.0f, 1.0f};
    g_blade.frame->GetWorldPosition(hand, origin);

    const float toward_hand = (scale - 1.0f) / (2.0f * scale);

    for (int axis = 0; axis < 3; axis++) {
        hit_pos[axis] -= (hit_pos[axis] - hand[axis]) * toward_hand;
    }

    return radius + bone * (scale - 1.0f) / 2.0f;
}

// ── Every step on a floor ───────────────────────────────────────────────────────────────────

void GraceStep();

void DungeonStep() {
    if (UserStatus == nullptr) {
        return;
    }

    if (g_floor != UserStatus->cur_floor || g_map != selectMapNo) {
        g_floor = UserStatus->cur_floor;
        g_map = selectMapNo;
        OnFloorStart();
    }

    const int held = ToanWeapon();

    // Hero's Courage: Freeze, Poison, Curse and Goo slide off Toan, and off Xiao while Super
    // Steve carries the Brave Ark's sphere.
    if (ActiveEffect() == ITEM_WEAPON_BRAVE_ARK) {
        Cure(UserStatus->cur_chara, AILMENT_FREEZE | AILMENT_POISON | AILMENT_CURSE | AILMENT_GOO);
    }

    // The curses follow Toan's own weapon whoever is out, so an ally's turn does not lift them.
    // Super Steve with an Evilcise or Maneater sphere curses Xiao the same way.
    if (UserStatus->cur_chara == CHARA_TOAN) {
        CurseStep(CHARA_TOAN, held, g_curse);
    } else if (UserStatus->cur_chara == CHARA_XIAO) {
        CurseStep(CHARA_XIAO, EffectOf(CHARA_XIAO), g_curse_xiao);
    }

    XiaoSphereStep(); // Dark Cloud Expanded: Super Steve's Swift Strikes and Quick Draw.
    TyphoonStep(held);
    SolarStep(held);
    JudgementStep(held);
    ZeusStep(held);
    MirageStep();
    HerculesStep(HeldBy(CHARA_UNGAGA));
    ColumnsStep();
    BladeStep(held);
    WiseOwlStep();
    WhpStep();
    ReflectorStep();
    MinibossStep(); // Dark Cloud Expanded: minibosses' boosted weapon drops.

    // Atlamillia Insurance's notice, once the game's own break message has gone.
    if (!g_insured_notice.empty() && DngMessMan.timer <= 0) {
        ShowDungeonMessage(g_insured_notice.c_str(), 5 * kStepsPerSecond);
        g_insured_notice.clear();
    }

    // The allies' ongoing effects.
    g_step_count++;
    ConfuseStep();
    FloorClearStep();
    StrideStep();
    // Dark Cloud Enhanced: Samba's and the Mayor's floor challenges.
    quest::EnhancedDungeonStep();
    TornadoStep();
    TunaStep();
    PockleStep();
    CatStep();
    GraceStep();
    fx::Step(); // Dark Cloud Expanded: the effects' models, light and tints.
}

void SpringStep(bool in_spring) {
    const bool knife = ToanWeapon() == ITEM_WEAPON_KITCHEN_KNIFE;

    if (!knife) {
        g_knife_was_in_spring = in_spring;
        return;
    }

    // Spring's Blessing: stepping into the water mends the knife and blesses it for a minute,
    // the blade three times its length and its attack doubled; standing in it keeps it fresh.
    if (in_spring && !g_knife_was_in_spring) {
        NowWeaponHave->durability_f = NowWeaponHave->durability;

        if (g_knife_bless_steps == 0) {
            ShowDungeonMessage("The spring's blessing surges\nthrough the Kitchen Knife!", 4 * kStepsPerSecond);
        }
    }

    if (in_spring) {
        g_knife_bless_steps = kKnifeBlessSteps;
    }

    g_knife_was_in_spring = in_spring;
}

bool AilmentBlocked(int ailment) {
    const int held = ActiveEffect();
    const int chara = UserStatus != nullptr ? UserStatus->cur_chara : CHARA_TOAN;

    // Hero's Courage.
    if (held == ITEM_WEAPON_BRAVE_ARK && (ailment & (AILMENT_FREEZE | AILMENT_POISON | AILMENT_CURSE | AILMENT_GOO)) != 0) {
        return true;
    }

    // Jealous Soul: while its curse holds, nothing else takes.
    if (held == ITEM_WEAPON_EVILCISE && ailment != AILMENT_CURSE && (UserStatus->ailments[chara] & AILMENT_CURSE) != 0) {
        return true;
    }

    return false;
}

int DefenseBonus() {
    // Defensive Legacy: Aga's Sword gives Toan 15 defense.
    constexpr int kAgasDefense = 15;

    return ActiveEffect() == ITEM_WEAPON_AGA_S_SWORD ? kAgasDefense : 0;
}

float ToanDamage(float damage, const MONSTOR &monster) {
    // Sanctifier: about twice the damage against the undead.
    if (Sanctifier(ActiveEffect()) && monster.attachment_kind == MONSTER_FAMILY_UNDEAD) {
        damage *= 2.0f;
    }

    return damage;
}

bool PerfectGuard() {
    // Divine Guard: 7th Heaven's guard blocks the heavy hits retail lets through.
    return ActiveEffect() == ITEM_WEAPON_7THHEAVEN;
}

bool PassesGuard(int hit_owner, int hit) {
    // Guard Crush: Toan's hits connect even while an enemy blocks.
    if ((hit_owner == CHARA_TOAN || hit_owner == CHARA_XIAO) && GuardCrush(EffectOf(hit_owner))) {
        return true;
    }

    // A guard-breaking blast's hit, planted this step or the last.
    for (const BlastCrush &crush : g_blast_crush) {
        if (crush.hit == hit && crush.step >= g_step_count - 1 && NowColData->hit[hit].damage == crush.damage) {
            return true;
        }
    }

    // Charging Bull: a charged Matador pellet's hit, planted this step or the last.
    for (const CrushHit &crush : g_crush_hits) {
        if (crush.hit == hit && crush.step >= g_step_count - 1 && NowColData->hit[hit].damage == crush.damage) {
            return true;
        }
    }

    return false;
}

bool PassesInvincibility(const MONSTOR &monster) {
    // No Fool's Gold: a mimic waking from its box can be hit; its guard motion still blocks.
    return NoFoolsGold(ActiveEffect()) && (monster.kind == MONSTER_KIND_MIMIC_SMALL || monster.kind == MONSTER_KIND_MIMIC_LARGE);
}

int KillAbs(int exp, const MONSTOR &monster, const WEAPON_HAVE *weapon) {
    if (weapon == nullptr) {
        return exp;
    }

    // The Pocklekul's pockle gives nothing for its fall.
    if (IsPockle(monster)) {
        return 0;
    }

    const int item = EffectItem(weapon);

    // Javelin: a sea enemy gives twice its ABS.
    if (item == ITEM_WEAPON_JAVELIN && monster.attachment_kind == MONSTER_FAMILY_SEA) {
        exp *= 2;
    }

    // Jacket Hunter: a Master Jacket the Gladius kills gives four times its ABS.
    constexpr s16 kMasterJacketName = 1;

    if (item == ITEM_WEAPON_GLADIUS && monster.name_no == kMasterJacketName) {
        exp *= 4;
    }

    // Sanctifier: an undead kill gives twice its ABS.
    if (Sanctifier(item) && monster.attachment_kind == MONSTER_FAMILY_UNDEAD) {
        exp *= 2;
    }

    return exp;
}

short *MenuDescription(const ClsMes *mes, const short *buff, int line) {
    // The menus' item help windows: the weapon menu's and the shops'.
    if ((mes != &CommonMenuMes2 && mes != &CommonMenuMes3) || buff == nullptr) {
        return nullptr;
    }

    // A weapon's description entry is its message number plus 500.
    constexpr int kDescriptionOffset = 500;

    const WeaponDescription *description = nullptr;

    for (const WeaponDescription &entry : kDescriptions) {
        if (GetCommonItemDataMsg(entry.item_no) + kDescriptionOffset == line) {
            description = &entry;
            break;
        }
    }

    if (description == nullptr) {
        return nullptr;
    }

    BuiltDescription &built = g_descriptions[line];

    if (built.buff != buff) {
        built = {};

        const short *vanilla = BankMessage(buff, line);

        if (vanilla == nullptr) {
            return nullptr;
        }

        // Keep the entry's leading line breaks and its name line, as the window lays them out.
        constexpr int kNameLineMax = 64;

        int at = 0;

        while (vanilla[at] == MES_CODE_NEWLINE) {
            at++;
        }

        while (at < kNameLineMax && vanilla[at] != MES_CODE_NEWLINE && vanilla[at] != MES_CODE_END) {
            at++;
        }

        if (vanilla[at] != MES_CODE_NEWLINE) {
            return nullptr;
        }

        built.codes.assign(vanilla, vanilla + at + 1);

        std::vector<s16> text;
        text::GameTextEncode(description->text, text);
        built.codes.insert(built.codes.end(), text.begin(), text.end());
        built.buff = buff;
    }

    return built.codes.empty() ? nullptr : reinterpret_cast<short *>(built.codes.data());
}

int CircleMultiplier() {
    int multiplier = 1;

    if (Owned(ITEM_WEAPON_CRYSKNIFE)) {
        multiplier++;
    }

    if (Owned(ITEM_WEAPON_MAGICAL_HAMMER)) {
        multiplier++;
    }

    return multiplier;
}

void CircleReward(int item_no) {
    CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    if (status == nullptr) {
        return;
    }

    for (int i = 1; i < CircleMultiplier(); i++) {
        if (status->CheckItemGet(item_no) != 0) {
            return;
        }

        status->GetItem(item_no, 1);
    }
}

float StatusBreakFactor(int item_no) {
    constexpr float kVanillaFactor = 0.6f;
    constexpr float kSevenfoldFactor = 0.77f;

    return item_no == ITEM_WEAPON_7BRANCH_SWORD ? kSevenfoldFactor : kVanillaFactor;
}

namespace {

bool g_sevenfold_refused = false;

} // namespace

bool SevenfoldRefuses(const WEAPON_HAVE &weapon) {
    g_sevenfold_refused = weapon.item_no == ITEM_WEAPON_7BRANCH_SWORD && weapon.level < kSevenfoldLevel;
    return g_sevenfold_refused;
}

bool SevenfoldRefused() {
    return g_sevenfold_refused;
}

short *SevenfoldRefusalText(const ClsMes *mes, int line) {
    // The Status Break result line of the menus' effect window (CWeaponLevelUp::DrawMes).
    constexpr int kBreakResultLine = 0x191;

    if (!g_sevenfold_refused || mes != &CommonMenuMes1 || line != kBreakResultLine) {
        return nullptr;
    }

    static std::vector<s16> codes;

    if (codes.empty()) {
        text::GameTextEncode("\nStatus Break fails\nbelow level 7.", codes);
    }

    return reinterpret_cast<short *>(codes.data());
}

int AbsCap(int max_exp) {
    // Overtraining: while a Macho Sword is owned, anywhere, every weapon keeps absorbing up
    // to twice its max.
    constexpr int kRolloverFactor = 2;

    return Owned(ITEM_WEAPON_MACHO_SWORD) ? max_exp * kRolloverFactor : max_exp;
}

int LevelUpCarry(WEAPON_HAVE *weapon) {
    // Empty SynthSphere: once the weapon levels up, the levels the sphere gave are its own.
    if (weapon != nullptr) {
        SphereLevels(weapon) = 0;
    }

    // Overtraining: what was absorbed past the max carries into the next level.
    if (weapon == nullptr || !Owned(ITEM_WEAPON_MACHO_SWORD)) {
        return 0;
    }

    return std::max(0, weapon->experience - GetWeaponMaxExp(weapon));
}

void OnKill(const MONSTOR &monster, WEAPON_HAVE *weapon, float *position) {
    if (weapon == nullptr || IsPockle(monster)) {
        return;
    }

    const int item = EffectItem(weapon);

    // DeSanga: every kill heals it 5 WHP.
    constexpr float kDeSangaHeal = 5.0f;

    if (item == ITEM_WEAPON_DE_SANGA) {
        weapon->durability_f = std::min(static_cast<float>(weapon->durability), weapon->durability_f + kDeSangaHeal);
    }

    // Shooting Stars: one kill in fifty puts an empty SynthSphere straight into the bag, as
    // Expanded does; with no room for an attachment the kill does not roll.
    CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    if (item == ITEM_WEAPON_STAR_BREAKER && status != nullptr && status->CheckItemGet(ITEM_ATTACH_SYNTHESIS_SPHERE) == 0 &&
        std::rand() % 50 == 0) {
        status->GetItem(ITEM_ATTACH_SYNTHESIS_SPHERE, 1);
        ShowDungeonMessage("The Star Breaker sent\nyou a shooting star!", 3 * kStepsPerSecond);
    }

    if (RandomItem == nullptr) {
        return;
    }

    // Solar Harvest: one kill in a hundred drops a Sun attachment.
    if (SolarHarvest(item) && std::rand() % 100 == 0) {
        RandomItem->Set(position, 1, -1, ITEM_ATTACH_SUN);
    }
}

int ChestItem(int item_no) {
    // Fine Fare: water is Premium Water and food Premium Chicken; the Dusack also turns a
    // Treasure Key into Gold Bullion, and the 7 Branch Sword's line gives a Repair Powder one
    // chance in four of being Auto Repair Powder.
    const int tier = FineFareTier(ActiveEffect());

    if (tier >= 1) {
        if (item_no == ITEM_REGULAR_WATER || item_no == ITEM_TASTY_WATER) {
            return ITEM_PREMIUM_WATER;
        }

        if (item_no == ITEM_BREAD || item_no == ITEM_CHEESE) {
            return ITEM_PREMIUM_CHICKEN;
        }
    }

    if (tier >= 2 && item_no == ITEM_TREASURE_KEY) {
        return ITEM_GOLD_BULLION;
    }

    if (tier >= 3 && item_no == ITEM_REPAIR_POWDER && std::rand() % 4 == 0) {
        return ITEM_AUTO_REPAIR_POWDER;
    }

    return item_no;
}

int ScriptRandom(int bound) {
    // Gravedigger: a reviving undead's death script rolls below its revive chance to get back
    // up. The roll comes up at its top while the bone key or the Cross Hinder is out, so the
    // undead stay down.
    constexpr std::array<s16, 4> kRevivers = {1, 50, 317, 319}; // Master Jacket, Mummy, Gacious, Horn Head

    if (bound <= 0 || NowMonstorUnit == nullptr || NowWeaponHave == nullptr || !KeepsUndeadDown(NowWeaponHave->item_no)) {
        return -1;
    }

    const int index = NowMonstorUnit->current_monster;

    if (index < 0 || index >= 16) {
        return -1;
    }

    const MONSTOR &monster = NowMonstorUnit->monster[index];

    if (monster.hp > 0 || std::ranges::find(kRevivers, monster.name_no) == kRevivers.end()) {
        return -1;
    }

    return bound - 1;
}

void OnAttach(WEAPON_HAVE *weapon, int hole) {
    if (weapon == nullptr) {
        return;
    }

    EmptySphereAttach(weapon, hole);

    // Buster Boost: an anti-monster attachment is worth +4 instead of +3 in a Buster Sword. The
    // attachment's own value changes, so menus, build-up and the level-up absorb all see it.
    constexpr int kStockAnti = 3;

    if (weapon->item_no != ITEM_WEAPON_BUSTER_SWORD || hole < 0 || hole >= 6) {
        return;
    }

    ATTACH_LIST &attach = weapon->attach[hole];

    if (attach.item_no < ITEM_ATTACH_SLAYER_START || attach.item_no >= ITEM_ATTACH_SLAYER_END) {
        return;
    }

    s8 &value = attach.vs_monster[attach.item_no - ITEM_ATTACH_SLAYER_START];

    if (value == kStockAnti) {
        value = kStockAnti + 1;
    }
}

void OnDetach(WEAPON_HAVE *weapon, int hole) {
    if (weapon == nullptr) {
        return;
    }

    EmptySphereDetach(weapon, hole);

    constexpr int kBoostedAnti = 4;

    if (weapon->item_no != ITEM_WEAPON_BUSTER_SWORD || hole < 0 || hole >= 6) {
        return;
    }

    ATTACH_LIST &attach = weapon->attach[hole];

    if (attach.item_no < ITEM_ATTACH_SLAYER_START || attach.item_no >= ITEM_ATTACH_SLAYER_END) {
        return;
    }

    s8 &value = attach.vs_monster[attach.item_no - ITEM_ATTACH_SLAYER_START];

    if (value == kBoostedAnti) {
        value = kBoostedAnti - 1;
    }
}

void ConfuseRoll(MONSTOR &monster, int owner, int hit_flags) {
    if (owner < 0 || NowMonstorUnit == nullptr || monster.kind == MONSTER_KIND_NO_LOCK_ON) {
        return;
    }

    // A Babel's Spear from before the port carries no flag of its own until the menus tag it.
    if ((hit_flags & kConfuseFlag) == 0 && EffectOf(owner) != ITEM_WEAPON_BABEL_S_SPEAR) {
        return;
    }

    const int m = static_cast<int>(&monster - NowMonstorUnit->monster);

    if (m < 0 || m >= 16 || std::rand() % 100 >= kConfusePercent) {
        return;
    }

    g_confused_until[m] = g_step_count + kConfuseSteps;
}

bool MonsterTarget(int monster_no, float *target) {
    if (NowMonstorUnit == nullptr || monster_no < 0 || monster_no >= 16) {
        return false;
    }

    // The Pocklekul's pockle goes for the party's enemy, or walks beside its summoner.
    if (IsPocklePos(monster_no) && PockleUp()) {
        PockleAim(target);
        return true;
    }

    // The Mirage's decoy outranks confusion, as in Expanded.
    if (g_mirage_lure.Pulls(monster_no)) {
        sceVu0CopyVector(target, g_mirage_lure.pos);
        return true;
    }

    // Angel Gear: enemies near the standing slingshot go for it.
    if (!Confused(monster_no) && ShieldPulls(monster_no, target)) {
        return true;
    }

    const int victim = Confused(monster_no) ? ConfusedTarget(monster_no) : Provoker(monster_no);

    if (victim < 0) {
        return false;
    }

    float me[4];
    MonsterPosition(monster_no, me);
    AimAt(victim, me, target);
    return true;
}

namespace {

/**
 * A confused or provoked enemy's blow or shot at at: planted for the enemies too.
 */
void PlantFriendlyBlow(int attacker, const float *at, float radius, int damage) {
    if (NowMonstorUnit == nullptr || NowColData == nullptr || damage <= 0 || !Attacks(attacker)) {
        return;
    }

    int free_hits = 0;

    for (int i = 0; i < kHitPoolSize; i++) {
        if (NowColData->active[i] == 0) {
            free_hits++;
        }
    }

    if (free_hits <= kHitPoolReserve) {
        return;
    }

    // Nobody's hit: no weapon, no WHP cost, no ailments.
    float     centre[4] = {at[0], at[1], at[2], 1.0f};
    const int hit = NowColData->Set(centre, damage, kFriendlyHitLife, radius, 0.0f, 2, 2, 0, 0);

    if (hit < 0) {
        return;
    }

    NowColData->hit[hit].weapon_flags = 0;
    g_friendly[g_friendly_next] = {hit, attacker, g_step_count};
    g_friendly_next = (g_friendly_next + 1) % static_cast<int>(g_friendly.size());
}

// The game's explosion shots: the pumpkin bomb and the three self-destructs' blasts.
constexpr int kExplosionShots[] = {3, 16, 17, 18};

bool ExplosionImmune() {
    return UserStatus != nullptr && ActiveEffect() == ITEM_WEAPON_BIG_BANG;
}

} // namespace

void OnMonsterAttack(int attacker, const float *at, float radius, int damage) {
    ShieldBlow(attacker, at, radius);
    PlantFriendlyBlow(attacker, at, radius, damage);
}

void OnMonsterShot(int attacker, int target, const float *at, float radius, int damage) {
    if (target == kPlayerTarget || attacker < 0 || attacker >= 16 || radius <= 0.0f) {
        return;
    }

    PlantFriendlyBlow(attacker, at, radius, damage);
}

bool ShotBlocked(const float *position, int target) {
    if (target == kPlayerTarget || UserStatus == nullptr || UserStatus->cur_chara != CHARA_UNGAGA) {
        return false;
    }

    for (const Column *column : {&g_babel, &g_cactus}) {
        if (!ColumnSolid(*column)) {
            continue;
        }

        const float exposed = column == &g_babel ? kBabelExposed : kCactusExposed;
        const float top = column->ground + exposed * column->scale;
        const float dx = position[0] - column->x;
        const float dz = position[2] - column->z;

        if (dx * dx + dz * dz <= column->radius * column->radius && position[1] <= top && position[1] >= column->ground - 4.0f) {
            return true;
        }
    }

    return false;
}

bool ShotSparesPlayer(const BT_SHOT_EFFECT *shot) {
    if (shot == nullptr || shot->target == kPlayerTarget || !ExplosionImmune()) {
        return false;
    }

    for (const int index : kExplosionShots) {
        if (shot == BtEntryEffectTbl[index]) {
            return true;
        }
    }

    return false;
}

bool BombSparesPlayer(int target) {
    return target != kPlayerTarget && ExplosionImmune();
}

bool SparesPlayer(int monster_no) {
    return IsPocklePos(monster_no) && PockleUp();
}

int MonsterHitFor(int victim, int hit, const float *position, float radius) {
    while (hit != -1) {
        const FriendlyHit *friendly = FindFriendly(hit);

        if (friendly == nullptr) {
            // The party's own hits pass through the pockle.
            if (!IsPocklePos(victim)) {
                return hit;
            }

            hit = NextMonsterHit(hit + 1, position, radius);
            continue;
        }

        const int attacker = friendly->attacker;

        if (MayHit(attacker, victim) && g_step_count >= g_friendly_ready[attacker][victim]) {
            g_friendly_ready[attacker][victim] = g_step_count + kFriendlyCooldownSteps;

            if (Confused(attacker)) {
                Provoke(victim, attacker);
            }

            return hit;
        }

        hit = NextMonsterHit(hit + 1, position, radius);
    }

    return -1;
}

int AbilityMessage(int bit) {
    // The ability names are system messages 70 to 82; 83 is another line, so Confuse takes one of
    // the mod's own.
    return bit == kConfuseAbilityBit ? kConfuseNameMessage : bit + 69;
}

short *SystemText(int line) {
    if (line != kConfuseNameMessage) {
        return nullptr;
    }

    static std::vector<s16> codes;

    if (codes.empty()) {
        text::GameTextEncode("Confuse", codes);
    }

    return reinterpret_cast<short *>(codes.data());
}

bool DemonShaftOpen() {
    // Demon Shaft waits for the game to be cleared, even where a story flag shows it early.
    return GameClearFlag != 0;
}

void TagAbilities(WEAPON_HAVE *weapon) {
    if (weapon != nullptr && weapon->item_no == ITEM_WEAPON_BABEL_S_SPEAR) {
        weapon->flags = static_cast<s16>(weapon->flags | kConfuseFlag);
    }
}

int DefaultAbilities(int weapon_no, int flags) {
    if (weapon_no != ITEM_WEAPON_BABEL_S_SPEAR) {
        return flags;
    }

    // 1 means none.
    return flags == 1 ? kConfuseFlag : flags | kConfuseFlag;
}

bool UseQuickPowder() {
    // Dark Cloud Enhanced: Escape and Repair Powder work from a quick-use slot. An Escape Powder
    // asks first and goes on a second press, half a second to three seconds after the first;
    // a Repair Powder fills the weapon's WHP, and is kept while the weapon is already full.
    constexpr int kConfirmFrom = kStepsPerSecond / 2;
    constexpr int kConfirmUntil = 3 * kStepsPerSecond;
    static int    asked_at = -1;

    if (UserStatus == nullptr || itemNowSel < 1 || itemNowSel > 3) {
        return false;
    }

    s16      *slots = UserStatus->active_item;
    const int item_no = slots[itemNowSel];

    if (slots[itemNowSel + 3] <= 0) {
        return false;
    }

    if (item_no == ITEM_ESCAPE_POWDER) {
        const int since = asked_at < 0 ? -1 : g_step_count - asked_at;

        if (since >= kConfirmFrom && since <= kConfirmUntil) {
            asked_at = -1;
            DelActiveItem(itemNowSel);
            gameTask = GAME_TASK_ESCAPE_INIT;
        } else if (since < 0 || since > kConfirmUntil) {
            asked_at = g_step_count;
            ShowDungeonMessage("Are you sure you want to leave?\nPress Square again to use the Escape Powder.", kConfirmUntil);
        }

        return true;
    }

    if (item_no == ITEM_REPAIR_POWDER) {
        if (NowWeaponHave != nullptr && NowWeaponHave->durability_f < NowWeaponHave->durability) {
            DelActiveItem(itemNowSel);
            NowWeaponHave->durability_f = NowWeaponHave->durability;
            DngMessMan.message = 0xBC;
            DngMessMan.timer = 0xF0;
            DngMessMan.steev_window = false;
            SndSePlay(SE_WEAPON_REPAIR, -1, 0);
        }

        return true;
    }

    return false;
}

void ShowDungeonMessage(std::string_view text, int frames) {
    // Each line takes a new number, so the window always builds the latest one.
    const int id = g_next_message;
    g_next_message = g_next_message + 1 >= kMessageBase + 1000 ? kMessageBase : g_next_message + 1;

    if (Messages().Set(id, text) < 0) {
        return;
    }

    ClsMesSetMessageOverlay(&DngMes1, Messages().Data());
    DngMessMan.enabled = true;
    DngMessMan.steev_window = false;
    DngMessMan.message = id;
    DngMessMan.timer = frames;
}

namespace {

struct SpeciesName {
    int              name_no;
    std::string_view name;
};

/**
 * A species' name for the mod's notices, from Dark Cloud Enhanced's enemy list (Enemies.cs), by
 * MONSTOR::name_no; empty for one not listed.
 */
std::string_view SpeciesNameOf(int name_no) {
    static constexpr SpeciesName kNames[] = {
        {1, "Master Jacket"},
        {3, "Skeleton Soldier"},
        {5, "Statue"},
        {6, "Dasher"},
        {7, "Werewolf"},
        {8, "Fli Fli"},
        {9, "Hornet"},
        {10, "Halloween"},
        {11, "Cannibal Plant"},
        {12, "Earth Digger"},
        {14, "Sunday"},
        {15, "Monday"},
        {16, "Tuesday"},
        {17, "Wednesday"},
        {18, "Thursday"},
        {19, "Friday"},
        {20, "Saturday"},
        {21, "Witch Hellza"},
        {22, "Witch Illza"},
        {23, "Gunny"},
        {24, "Gyon"},
        {25, "Pirate's Chariot"},
        {26, "Auntie Medu"},
        {27, "Captain"},
        {28, "Corcea"},
        {30, "Golem"},
        {31, "Mr. Blare"},
        {32, "Dune"},
        {33, "Titan"},
        {34, "King Mimic"},
        {35, "Mimic"},
        {36, "King Mimic"},
        {37, "Mimic"},
        {38, "King Mimic"},
        {39, "Mimic"},
        {40, "Arthur"},
        {42, "Ghost"},
        {43, "Alexander"},
        {44, "Heart"},
        {45, "Club"},
        {46, "Diamond"},
        {47, "Spade"},
        {48, "Joker"},
        {49, "Bomber Head"},
        {50, "Mummy"},
        {51, "Lich"},
        {52, "Curse Dancer"},
        {54, "Killer Snake"},
        {55, "Living Armor"},
        {56, "White Fang"},
        {57, "Moon Bug"},
        {58, "Phantom"},
        {59, "Dragon"},
        {60, "Cave Bat"},
        {61, "Evil Bat"},
        {62, "Hell Pockle"},
        {63, "Rash Dasher"},
        {64, "Steel Giant"},
        {65, "Blizzard"},
        {66, "Moon Digger"},
        {67, "Dark Flower"},
        {68, "Cursed Rose"},
        {69, "Billy"},
        {70, "Vulcan"},
        {71, "Crabby Hermit"},
        {72, "Space Gyon"},
        {73, "Blue Dragon"},
        {74, "Black Dragon"},
        {75, "Mask of Prajna"},
        {76, "Crescent Baron"},
        {77, "Rockanoff"},
        {78, "King Mimic"},
        {79, "Mimic"},
        {80, "King Mimic"},
        {81, "Mimic"},
        {82, "King Mimic"},
        {83, "Mimic"},
        {121, "Wine Keg"},
        {221, "Black Knight"},
        {301, "Yammich"},
        {303, "Statue Dog"},
        {304, "Opar"},
        {305, "Haley Holey"},
        {306, "King Prickly"},
        {308, "Nikapous"},
        {309, "Mimic"},
        {310, "King Mimic"},
        {311, "Gemron"},
        {312, "Gemron"},
        {313, "Gemron"},
        {314, "Gemron"},
        {315, "Gemron"},
        {316, "Bishop Q"},
        {317, "Gacious"},
        {318, "Silver Gear"},
        {319, "Horn Head"},
    };

    for (const SpeciesName &entry : kNames) {
        if (entry.name_no == name_no) {
            return entry.name;
        }
    }

    return {};
}

} // namespace

void OnSteal(const MONSTOR &monster, int owner) {
    if (!StealsShots(owner)) {
        return;
    }

    CSHOT_EFFECT *source = Projectile(monster.shot_effect);

    if (source == nullptr) {
        source = Projectile(monster.shot_effect2);
    }

    if (source == nullptr) {
        return;
    }

    g_stolen.Build(*source);

    // As Expanded's notice does, the line names the enemy whose shot it was.
    const std::string_view name = SpeciesNameOf(monster.name_no);
    const std::string      notice = name.empty() ? std::string("Projectile stolen! Your shots\nare its shots on this floor.")
                                                 : std::format("{}'s projectile\nis now yours on this floor.", name);
    ShowDungeonMessage(notice.c_str(), 4 * kStepsPerSecond);
}

bool FireStolenShot(const float *origin, const float *direction, int damage, int owner) {
    if (!StealsShots(owner) || !g_stolen.Valid()) {
        return false;
    }

    return g_stolen.Fire(origin, direction, static_cast<int>(static_cast<float>(damage) * kStolenDamage), owner);
}

void StolenShotStep() {
    for (BorrowedShot *shot : {&g_stolen, &g_breath}) {
        if (shot->Valid()) {
            shot->effect.Step();
        }
    }
}

void StolenShotDraw() {
    for (BorrowedShot *shot : {&g_stolen, &g_breath}) {
        if (shot->Valid()) {
            shot->effect.Draw();
        }
    }
}

namespace {

/**
 * Moonlit Focus: Super Steve with a Tsukikage or Heaven's Cloud sphere fires every pellet at
 * twice its speed.
 */
void MoonlitPellet(CSHOT *shots, int slot) {
    if (shots == nullptr || slot < 0 || slot >= 12 || UserStatus == nullptr || UserStatus->cur_chara != CHARA_XIAO ||
        !MoonlitFocus(EffectOf(CHARA_XIAO))) {
        return;
    }

    for (int axis = 0; axis < 3; axis++) {
        shots->vector[slot][axis] *= kMoonlitShotSpeed;
    }
}

/**
 * Heaven's Cloud's wind burst where an empowered pellet struck, share 0 to 1 of the way from the
 * first stage to the most: every enemy within the blast takes its share of the attack in the
 * weapon's element and is thrown clear of a point pulled back toward Xiao.
 */
void CloudWind(const float *pos, float share) {
    if (NowMonstorUnit == nullptr || NowWeaponHave == nullptr) {
        return;
    }

    const float radius = kCloudWindLeast + (kCloudWindMost - kCloudWindLeast) * share;
    const float charge = static_cast<float>(kCloudFirstSteps) / kCloudFullSteps + (1.0f - static_cast<float>(kCloudFirstSteps) / kCloudFullSteps) * share;
    const int   damage = std::max(1, static_cast<int>(std::lround(static_cast<float>(NowWeaponHave->attack) * kCloudWindDamage * charge)));

    float centre[4] = {pos[0], pos[1], pos[2], 1.0f};
    const float dx = CharaMain.pos[0] - pos[0];
    const float dz = CharaMain.pos[2] - pos[2];
    const float away = std::sqrt(dx * dx + dz * dz);

    if (away > 1e-3f) {
        const float back = std::min(kCloudWindPullback, away);
        centre[0] += dx / away * back;
        centre[2] += dz / away * back;
    }

    for (int m = 0; m < 16; m++) {
        if (!ActiveMonster(m)) {
            continue;
        }

        float at[4];
        MonsterPosition(m, at);

        if (FlatDistance(at, pos) > radius) {
            continue;
        }

        NowColData->Set(at, damage, 1, NowMonstorUnit->monster[m].body_radius + 2.0f, 0.0f, 2, 2, 0, 0);

        auto &hit = NowColData->hit[NowColData->now_hit];
        hit.vs_monster = NowWeaponHave->vs_monster;
        hit.weapon_flags = NowWeaponHave->flags;
        hit.flags = GetWeaponElementAttr(NowWeaponHave->best_elem);
        hit.owner = CHARA_XIAO;
        hit.attack_no = 0;
        NowColData->SetKickBack(centre, kCloudWindKick, kCloudWindDecay, 2);
    }

    // The wind's look: the summon smoke grown with the charge, and the Wind Gem's sound.
    constexpr float kWindSmokeLeast = 1.0f;
    constexpr float kWindSmokeMost = 2.5f;

    fx::Play(fx::Prop::Smoke, 0, centre, kWindSmokeLeast + (kWindSmokeMost - kWindSmokeLeast) * share, RandomYaw());
    SndSePlay(SE_WIND_GEM, -1, 0);
}

/**
 * The steps Xiao holds her shot to charge it with the weapon (or sphere) she carries, or -1 for one
 * whose shot does not charge.
 */
int ShotChargeSteps(int held) {
    if (held == ITEM_WEAPON_MATADOR || held == ITEM_WEAPON_DRAGON_S_Y || held == ITEM_WEAPON_DIVINEBEASTTITLE) {
        return kChargeSteps;
    }

    if (held == ITEM_WEAPON_HEAVEN_S_CLOUD) {
        return kCloudFirstSteps;
    }

    if (held == ITEM_WEAPON_MOBIUS_RING) {
        return kMobiusShotCycle;
    }

    if (TornadoFormOf(held) != nullptr) {
        return kTornadoShotSteps;
    }

    return -1;
}

} // namespace

bool XiaoAimStep(bool holding) {
    const int held = EffectOf(CHARA_XIAO);
    const int need = ShotChargeSteps(held);
    bool      full = false;

    if (holding && need > 0) {
        if (!g_aim_holding) {
            g_aim_holding = true;
            g_aim_steps = 0;
            g_aim_charged = false;
        }

        g_aim_steps++;

        if (held == ITEM_WEAPON_HEAVEN_S_CLOUD) {
            // Two stages, each with the game's charge flash.
            full = g_aim_steps == kCloudFirstSteps || g_aim_steps == kCloudMostSteps;
            g_aim_charged = g_aim_steps >= kCloudFirstSteps;
        } else if (held == ITEM_WEAPON_MOBIUS_RING) {
            // A flash with each step of the ramp, as Ruby's.
            full = g_aim_steps % kMobiusShotCycle == 0;
            g_aim_charged = g_aim_steps >= kMobiusShotCycle;
        } else if (g_aim_steps >= need && !g_aim_charged) {
            g_aim_charged = true;
            full = true;
        }

        if (full) {
            setUnitAmbientAnime(15.0f, 1.0f, 0.0f, 122.0f, 208.0f);
        }
    } else if (g_aim_holding) {
        g_aim_holding = false;
        g_charge_armed = g_aim_charged ? kChargeArmSteps : 0;
        g_charge_hold = g_aim_steps;
        g_aim_charged = false;
    } else if (g_charge_armed > 0) {
        g_charge_armed--;
    }

    return full;
}

bool QuickDrawRelease() {
    return UserStatus != nullptr && UserStatus->cur_chara == CHARA_XIAO && QuickDraw(EffectOf(CHARA_XIAO));
}

bool XiaoShot(CSHOT *shots, const float *world, const float *vector, int damage) {
    if (FireStolenShot(world, vector, damage, CHARA_XIAO)) {
        return true;
    }

    const int  held = EffectOf(CHARA_XIAO);
    const bool charged = g_charge_armed > 0;
    g_charge_armed = 0;

    const int strong = static_cast<int>(static_cast<float>(damage) * kChargedDamage);

    // Spirit Beast: a charged shot leaves the slingshot as Xiao's cat form.
    if (charged && held == ITEM_WEAPON_DIVINEBEASTTITLE && CatLaunch(world, vector, strong)) {
        return true;
    }

    // Dragon's Breath: a charged shot is the element's Gemron ball, or where no monster on the
    // floor fires one, the pellet grown five times; either at 1.5 times the damage.
    if (charged && held == ITEM_WEAPON_DRAGON_S_Y) {
        if (BreathFire(world, vector, strong)) {
            return true;
        }

        const int slot = SpawnPellet(shots, world, vector, strong, kBreathPelletSize);

        if (slot >= 0) {
            g_pellets[slot].kick_back = kBreathKickBack;
        }

        return true;
    }

    // Charging Bull: a charged shot at 1.5 times the damage that passes guards and shoves the
    // enemy along its flight.
    if (charged && held == ITEM_WEAPON_MATADOR) {
        const int slot = SpawnPellet(shots, world, vector, strong, kBullPelletSize);

        if (slot >= 0) {
            g_pellets[slot].kick_back = kBullKickBack;
            g_pellets[slot].crush = true;
        }

        return true;
    }

    // Super Steve with a sphere of the Halberd line: a charged shot carries the form, flying its
    // travel faster, drawn its size larger, at 1.5 times the damage.
    if (const TornadoForm *form = TornadoFormOf(held); charged && form != nullptr) {
        const float fast[3] = {vector[0] * form->travel, vector[1] * form->travel, vector[2] * form->travel};
        SpawnPellet(shots, world, fast, strong, form->scale);
        return true;
    }

    // Super Steve with a Heaven's Cloud sphere: an empowered shot grows with the hold and bursts
    // in the wind where it strikes.
    if (charged && held == ITEM_WEAPON_HEAVEN_S_CLOUD) {
        const float share =
            std::clamp(static_cast<float>(g_charge_hold - kCloudFirstSteps) / (kCloudMostSteps - kCloudFirstSteps), 0.0f, 1.0f);
        const int damage_now = static_cast<int>(std::lround(static_cast<float>(damage) * (1.0f + (kCloudDamageMost - 1.0f) * share)));
        const int slot = SpawnPellet(shots, world, vector, damage_now, 1.0f + (kCloudPelletMost - 1.0f) * share);

        if (slot >= 0) {
            g_pellets[slot].wind = share;
            MoonlitPellet(shots, slot);
        }

        return true;
    }

    // Super Steve with a Mobius Ring sphere: x1.5 for each second and a half held, the pellet
    // growing with it.
    if (charged && held == ITEM_WEAPON_MOBIUS_RING) {
        const float times = std::pow(kMobiusShotStep, static_cast<float>(g_charge_hold / kMobiusShotCycle));
        const int   ramped = static_cast<int>(std::min(static_cast<float>(damage) * times, static_cast<float>(kMobiusShotDamageMost)));
        SpawnPellet(shots, world, vector, ramped, std::min(times, kMobiusPelletMost));
        return true;
    }

    // Super Steve with a Sun Sword, Big Bang or Sword of Zeus sphere: a charged shot carries the
    // sword's move to what it hits.
    if (charged && (held == ITEM_WEAPON_SUN_SWORD || held == ITEM_WEAPON_BIG_BANG || held == ITEM_WEAPON_SWORD_OF_ZEUS)) {
        const int slot = SpawnPellet(shots, world, vector, strong, 1.0f);

        if (slot >= 0) {
            g_pellets[slot].sword = held;
        }

        return true;
    }

    // Double Impact: two pellets side by side across the flight line.
    if (held == ITEM_WEAPON_DOUBLE_IMPACT) {
        const int twin = std::max(1, static_cast<int>(std::lround(static_cast<float>(damage) * kTwinDamage)));
        const float ground = std::sqrt(vector[0] * vector[0] + vector[2] * vector[2]);
        const float side_x = ground > 1e-3f ? vector[2] / ground : 1.0f;
        const float side_z = ground > 1e-3f ? -vector[0] / ground : 0.0f;
        const float half = kTwinWidth / 2.0f;

        for (const float sign : {-1.0f, 1.0f}) {
            const float at[3] = {world[0] + sign * side_x * half, world[1], world[2] + sign * side_z * half};
            SpawnPellet(shots, at, vector, twin, 1.0f);
        }

        return true;
    }

    return false;
}

void OnPelletSpawned(int slot) {
    if (slot >= 0 && slot < 12) {
        g_pellets[slot] = {};
        MoonlitPellet(NowShotData, slot);

        // Super Steve with a Baselard or Claymore sphere: every pellet throws its enemy as the
        // sword's hits do.
        if (HeavyHand(EffectOf(CHARA_XIAO))) {
            g_pellets[slot].kick_back = kSphereKickBack;
        }
    }
}

bool PelletDeparts(CSHOT *shots, int slot) {
    Pellet &pellet = g_pellets[slot];

    if (pellet.depart_from < 0 || NowMonstorUnit == nullptr) {
        return false;
    }

    // A ricochet is stepped clear of the monster it left before it can hit again, so it
    // cannot strike that one twice; reaching another hands it back to the game at once.
    float *pos = shots->pos[slot];
    float *vec = shots->vector[slot];

    pos[0] += vec[0];
    pos[1] += vec[1];
    pos[2] += vec[2];

    const float speed = std::sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
    const bool  clear = !InsideMonster(pellet.depart_from, pos, kPelletRadius + speed) ||
                       ++pellet.depart_steps >= kRicochetDepartSteps;
    const bool  other = MonsterAt(pos, kPelletRadius, pellet.depart_from) >= 0;

    if (clear || other) {
        pellet.depart_from = -1;
    }

    return true;
}

namespace {

/**
 * Super Steve's charged pellet lands with a sword's move: the Sun Sword's Solar Shot blinds the
 * room, Big Bang's Detonate blasts where it hits, the Sword of Zeus's Lightning strikes the enemy
 * it hits. Each costs what the sword's own move does.
 */
void SwordShotHit(int sword, const float *pos) {
    const float at[4] = {pos[0], pos[1], pos[2], 1.0f};

    if (sword == ITEM_WEAPON_SUN_SWORD) {
        SolarBlind();
        DrainWhp(ITEM_WEAPON_SUN_SWORD, kSolarWhp);
    } else if (sword == ITEM_WEAPON_BIG_BANG) {
        PlantBlast(at, 1.0f, true);
        Burst(at, kDetonateScale);
        DrainWhp(ITEM_WEAPON_BIG_BANG, kDetonateWhp);
    } else if (sword == ITEM_WEAPON_SWORD_OF_ZEUS) {
        const int hit = MonsterAt(at, kPelletRadius * 2.0f, -1);

        if (hit >= 0) {
            BoltOn(hit);
        } else {
            PlantBlast(at, kBoltDamage, true);
            BoltSeen(at);
        }

        DrainWhp(ITEM_WEAPON_SWORD_OF_ZEUS, kBoltWhp);
    }
}

} // namespace

void OnPelletHit(CSHOT *shots, int slot, bool monster) {
    const Pellet pellet = g_pellets[slot];
    const float *pos = shots->pos[slot];
    const float *vec = shots->vector[slot];

    // The charged pellets' kick, from a point behind the pellet so the shove follows its flight.
    if (pellet.kick_back > 0.0f) {
        const float ground = std::sqrt(vec[0] * vec[0] + vec[2] * vec[2]);
        float       origin[4] = {pos[0], pos[1], pos[2], 1.0f};

        if (ground > 1e-3f) {
            origin[0] -= vec[0] / ground * pellet.kick_back;
            origin[2] -= vec[2] / ground * pellet.kick_back;
        }

        NowColData->SetKickBack(origin, kKickSpeed, kKickDecay, 2);
    }

    if (pellet.crush) {
        g_crush_hits[slot] = {NowColData->now_hit, shots->damage[slot], g_step_count};
    }

    if (monster && pellet.sword >= 0) {
        SwordShotHit(pellet.sword, pos);
    }

    if (monster && pellet.wind >= 0.0f) {
        CloudWind(pos, pellet.wind);
    }

    // Ricochet: the Hardshooter's pellets, and Double Impact's, send a second pellet on from
    // the monster they hit at the nearest other one within reach, or anywhere when none is.
    const int held = EffectOf(CHARA_XIAO);

    if (!monster || pellet.bounced || NowMonstorUnit == nullptr ||
        (held != ITEM_WEAPON_HARDSHOOTER && held != ITEM_WEAPON_DOUBLE_IMPACT)) {
        return;
    }

    const int hit = MonsterAt(pos, kPelletRadius, -1);

    if (hit < 0) {
        return;
    }

    float aim[3];
    float dx;
    float dz;

    if (NextTarget(hit, pos, aim) >= 0) {
        dx = aim[0] - pos[0];
        dz = aim[2] - pos[2];
    } else {
        const float angle = static_cast<float>(std::rand()) / RAND_MAX * 6.2831853f;
        dx = std::cos(angle);
        dz = std::sin(angle);
    }

    float length = std::sqrt(dx * dx + dz * dz);

    if (length < 1e-3f) {
        dx = 1.0f;
        dz = 0.0f;
        length = 1.0f;
    }

    float speed = std::sqrt(vec[0] * vec[0] + vec[2] * vec[2]);

    if (speed < 0.5f) {
        speed = kRicochetSpeed;
    }

    const float start[3] = {pos[0], pos[1], pos[2]};
    const float along[3] = {dx / length * speed, 0.0f, dz / length * speed};
    const int   bounce = SpawnPellet(shots, start, along, shots->damage[slot], 1.0f);

    if (bounce >= 0) {
        g_pellets[bounce].bounced = true;
        g_pellets[bounce].depart_from = hit;
    }
}

void RubyChargeStep(float charge_time, int ball_slot) {
    const bool mobius = HeldBy(CHARA_RUBY) == ITEM_WEAPON_MOBIUS_RING;

    // A new charge starts from the stock damage.
    if (charge_time <= 1.0f) {
        g_mobius_mult = 1.0f;
        g_mobius_steps = 0;
    }

    if (mobius && charge_time >= kMobiusFull) {
        if (g_mobius_steps % kMobiusPeriod == 0 && MobiusDamage() < kMobiusMaxDamage) {
            g_mobius_mult *= kMobiusStep;

            const int damage = MobiusDamage();
            ShowDungeonMessage(damage > 9000 ? std::string("Total damage is over 9000") : "Total damage " + std::to_string(damage),
                               2 * kStepsPerSecond);
        }

        g_mobius_steps++;
    }

    if (NowMainEffect != nullptr && ball_slot >= 0 && ball_slot < 8) {
        SetSlotScale(NowMainEffect, ball_slot, mobius ? MobiusScale() : 1.0f);
    }
}

int RubyShotDamage(int damage) {
    if (HeldBy(CHARA_RUBY) != ITEM_WEAPON_MOBIUS_RING || g_mobius_mult <= 1.0f) {
        return damage;
    }

    return MobiusDamage();
}

void OnRubyShot(CSHOT_EFFECT *effect, int slot, bool charged) {
    if (effect == nullptr || slot < 0 || slot >= 8) {
        return;
    }

    const float scale = charged && HeldBy(CHARA_RUBY) == ITEM_WEAPON_MOBIUS_RING ? MobiusScale() : 1.0f;

    SetSlotScale(effect, slot, scale);
    RadiusOwner(effect, CHARA_RUBY);
    g_mobius_radius[slot] = scale;
}

void OnToanWhirl(CSHOT_EFFECT *effect, int slot) {
    if (effect == nullptr || slot < 0 || slot >= 8) {
        return;
    }

    // Typhoon: the whirlwind is drawn and hits as wide as the grown blade.
    const float scale = ToanWeapon() == ITEM_WEAPON_HEAVEN_S_CLOUD ? g_typhoon_factor : 1.0f;

    SetSlotScale(effect, slot, scale);
    RadiusOwner(effect, CHARA_TOAN);
    g_mobius_radius[slot] = scale;

    // Detonate: the spin is the blast at his feet, through any guard; its own hit is put out of
    // reach so nothing is struck twice.
    constexpr float kOutOfReach = -1000.0f;

    if (ToanWeapon() == ITEM_WEAPON_BIG_BANG) {
        g_mobius_radius[slot] = kOutOfReach;
        PlantBlast(CharaMain.pos, 1.0f, true);
        DrainWhp(ITEM_WEAPON_BIG_BANG, kDetonateWhp);

        // The blast is seen as the game's bomb blast in place of the whirlwind.
        constexpr float kHidden = 0.001f;
        SetSlotScale(effect, slot, kHidden);
        Burst(CharaMain.pos, kExplosionScale);
    }
}

bool WhirlwindUnlocked() {
    // The Sword of Zeus gives the whirlwind up for its own charge.
    return UserStatus->skill_owned[0] != 0 && ToanWeapon() != ITEM_WEAPON_SWORD_OF_ZEUS;
}

void UngagaStrike(const float *tip) {
    if (g_wrath != Wrath::Primed || HeldBy(CHARA_UNGAGA) != ITEM_WEAPON_HERCULES_WRATH) {
        return;
    }

    g_wrath = Wrath::Idle;

    // The blast's sparkle and a gold flash where it lands.
    const float *at = DecoyUp() ? g_mirage_lure.pos : tip;
    fx::Play(fx::Prop::Sparkle, kSparkleSwirl, at, 1.0f, 0.0f);
    fx::Flash(kHerculesLight, kHerculesLight, kHerculesFlashSteps);

    if (DecoyUp()) {
        PlantBlast(g_mirage_lure.pos, 1.0f, true, -1, kHerculesReach);
        MirageEnd();
    } else {
        PlantBlast(tip, 1.0f, true, -1, kHerculesReach);
    }

    DrainWhp(ITEM_WEAPON_HERCULES_WRATH, kHerculesWhp);
    setUnitAmbientAnime(90.0f, 1.0f, 255.0f, 255.0f, 255.0f);
}

int SkeletonKey(const int *usable) {
    // Skeleton Key: with the Bone Rapier or the Bone Slingshot out, the floor's locked door opens
    // without its key, and the key it would take stays where it is.
    if (usable == nullptr || UserStatus == nullptr) {
        return -1;
    }

    const int chara = UserStatus->cur_chara;
    const int held = EffectOf(chara);

    if (!(chara == CHARA_TOAN && held == ITEM_WEAPON_BONE_RAPIER) &&
        !(chara == CHARA_XIAO && (held == ITEM_WEAPON_BONE_SLINGSHOT || held == ITEM_WEAPON_BONE_RAPIER))) {
        return -1;
    }

    const int key = ITEM_BONE_KEY + selectMapNo;

    for (int i = 0; i < 16 && usable[i] != -1; i++) {
        if (usable[i] == key) {
            ShowDungeonMessage("You can hear an ominous voice\nlaughing 'Rattle me bones!'", 4 * kStepsPerSecond);
            return key;
        }
    }

    return -1;
}

void InsuranceLoad() {
    g_insured.clear();
    g_insured_notice.clear();

    const dc::quest::Progress *progress = dc::quest::FindProgress(kInsuranceId);

    if (progress == nullptr) {
        return;
    }

    const auto value = [&](const std::string &key) {
        const auto found = progress->vars.find(key);
        return found != progress->vars.end() ? found->second : 0;
    };

    const int count = value("count");

    for (int i = 0; i < count; i++) {
        InsuredSphere insured{};
        insured.dungeon = value(std::format("{}.dungeon", i));
        insured.floor = value(std::format("{}.floor", i));
        insured.box = -1;

        std::array<s32, kInsuranceWords> words{};

        for (int word = 0; word < kInsuranceWords; word++) {
            words[static_cast<std::size_t>(word)] = value(std::format("{}.w{}", i, word));
        }

        std::memcpy(&insured.sphere, words.data(), sizeof(ATTACH_LIST));

        if (insured.sphere.item_no == ITEM_ATTACH_SYNTHESIS_SPHERE) {
            g_insured.push_back(insured);
        }
    }

    if (!g_insured.empty()) {
        std::printf("[Insurance] %d SynthSphere chest(s) waiting from the save\n", static_cast<int>(g_insured.size()));
    }
}

void OnWeaponBroke(const WEAPON_HAVE &weapon) {
    if (!Owned(ITEM_WEAPON_ATLAMILLIA_SWORD) || weapon.item_no < 0) {
        return;
    }

    const int level = weapon.level;
    const int share = std::min(50, (level + 1) * 10);

    InsuredSphere insured{};
    insured.dungeon = selectMapNo;
    insured.floor = InsuredFloor(selectMapNo);
    insured.box = -1;

    ATTACH_LIST &sphere = insured.sphere;
    sphere.item_no = ITEM_ATTACH_SYNTHESIS_SPHERE;
    sphere.sphere_weapon_no = weapon.item_no;
    sphere.sphere_flags = static_cast<s16>(weapon.flags);
    sphere.sphere_level = static_cast<s8>(std::min(level, 127));

    const s16 stats[4] = {weapon.attack, weapon.endurance, weapon.speed, weapon.magic};

    for (int i = 0; i < 4; i++) {
        sphere.status[i] = static_cast<s16>(stats[i] * share / 100);
    }

    for (int i = 0; i < 5; i++) {
        sphere.elem[i] = static_cast<s8>(weapon.elem[i] * share / 100);
    }

    for (int i = 0; i < 10; i++) {
        sphere.vs_monster[i] = static_cast<s8>(weapon.vs_monster[i] * share / 100);
    }

    g_insured.push_back(insured);
    InsuranceSave();

    g_insured_notice = insured.floor >= 0
                           ? std::format("Atlamillia Insurance! A SynthSphere\nof it waits on floor {}.", insured.floor + 1)
                           : std::string("Atlamillia Insurance! A SynthSphere\nof it waits on the next floor.");
}

namespace {

// The floor numbers past which Enhanced swaps to a dungeon's second-half tables.
bool SecondHalf(int dungeon, int floor) {
    static constexpr std::array<int, 7> kHalfway = {8, 9, 9, 9, 8, 99, 99};
    return floor > kHalfway[dungeon];
}

// Dark Cloud Enhanced's weapon table of each dungeon, by its floors before and after the
// halfway point; the chests, the clown and the minibosses all draw from it.
const std::vector<s16> &DungeonWeapons(int dungeon, int floor) {
    static const std::array<std::vector<s16>, 7> kFirstHalf = {{
        {259, 260, 262, 290},
        {264, 270, 283, 284, 286, 291, 301, 303, 304},
        {261, 265, 282, 284, 286, 302, 305, 316, 319, 327, 328},
        {263, 272, 282, 288, 293, 306, 318, 334, 337, 344},
        {272, 288, 281, 307, 322, 334, 344, 351, 352, 353},
        {274, 281, 285, 292, 294, 308, 322, 336, 338, 339, 354, 360, 368, 375},
        {276, 273, 275, 281, 285, 309, 311, 323, 325, 340, 338, 355, 359, 370, 371},
    }};
    static const std::array<std::vector<s16>, 7> kSecondHalf = {{
        {259, 260, 262, 270, 290, 301, 304},
        {264, 270, 283, 284, 286, 291, 301, 303, 304, 316, 321, 328},
        {261, 265, 282, 284, 286, 302, 305, 319, 327, 328, 333, 335, 343},
        {263, 272, 282, 288, 293, 306, 318, 334, 337, 344, 349, 350, 359},
        {272, 288, 281, 307, 322, 334, 344, 351, 352, 353, 365, 374},
        {274, 281, 285, 292, 294, 308, 322, 336, 338, 339, 354, 360, 368, 375},
        {276, 273, 275, 281, 285, 309, 311, 323, 325, 340, 338, 355, 359, 370, 371},
    }};

    return SecondHalf(dungeon, floor) ? kSecondHalf[dungeon] : kFirstHalf[dungeon];
}

int Roll(int bound) {
    return static_cast<int>(static_cast<float>(bound) * static_cast<float>(rand()) / 2147483648.0f);
}

template <typename T>
int Pick(const T &table) {
    return table[Roll(static_cast<int>(std::size(table)))];
}

// Enhanced sends more weapons out of the chests and the clown once Chronicle 2 is in hand.
bool HasChronicle2() {
    return PlayerAllItemCheck(ITEM_WEAPON_CHRONICLE2) != 0;
}

// A Map or Magical Crystal carried in the bag stands in for the floor's own.
bool g_carried_map = false;
bool g_carried_crystal = false;

bool CarriesItem(int item_no) {
    if (SaveData == nullptr) {
        return false;
    }

    const auto &pack = SaveData->GetDngStatus()->item_pack.item;
    return std::ranges::find(pack, item_no) != std::end(pack);
}

void RandomizeChests(CDungeonMap *map, int floor_no, int main_floor) {
    // Dark Cloud Enhanced's chest randomizer: every chest of the floor is rerolled from Enhanced's
    // own tables. A small chest takes an item from the dungeon's list (the back floor's from one
    // list shared by all dungeons), a large one a weapon from the dungeon's table with a random
    // trap. Large chests come 12% of the time in the Divine Beast Cave, 1.5 points fewer each
    // dungeon after, and twice as often once Chronicle 2 is in hand. Powerup Powder is rerolled
    // 4 times in 5 (on the main floors only until Chronicle 2). The Map and Magical Crystal
    // chests are left alone unless one is carried in the bag, which then lights the floor's.
    static const std::array<std::vector<s16>, 7> kFirstHalfItems = {{
        {145, 146, 147, 148, 148, 155, 149, 159, 181, 178, 150, 151, 151, 224, 199, 197, 186, 187, 188, 189, 190, 152, 153, 177, 177, 177, 174, 224, 155,
         175, 177, 175, 174, 91, 92, 93, 94, 81, 82, 83, 84, 85, 112, 117, 114, 224, 135, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170},
        {177, 177, 175, 176, 174, 181, 178, 148, 155, 149, 145, 146, 147, 159, 199, 197, 186, 187, 188, 189, 190, 161, 162, 163, 164, 165, 91, 92, 93, 94, 81,
         82, 83, 84, 85, 160, 160, 225, 225, 150, 178, 225, 225, 225, 225, 150, 151, 151, 152, 153, 154, 132, 133, 134, 135, 235, 166, 167, 168, 169, 170, 148},
        {177, 177, 175, 176, 174, 178, 181, 235, 148, 155, 149, 145, 146, 147, 159, 150, 151, 151, 152, 153, 166, 167, 168, 169, 170, 154, 199, 197, 193,
         186, 187, 188, 189, 190, 161, 162, 163, 164, 165, 91, 92, 93, 94, 81, 82, 83, 84, 85, 226, 226, 245, 132, 133, 134, 135, 148},
        {177, 177, 175, 176, 174, 181, 178, 235, 228, 155, 149, 145, 146, 147, 159, 150, 151, 228, 152, 153, 166, 167, 168, 169, 170, 154, 199, 197, 193,
         186, 187, 188, 189, 190, 161, 162, 163, 164, 165, 91, 92, 93, 94, 81, 82, 83, 84, 85, 228, 132, 133, 134, 135},
        {177, 177, 175, 176, 174, 181, 178, 235, 229, 155, 149, 145, 146, 147, 159, 150, 151, 229, 152, 153, 166, 167, 168, 169, 170, 154, 199, 197, 193,
         186, 187, 188, 189, 190, 161, 162, 163, 164, 165, 91, 92, 93, 94, 81, 82, 83, 84, 85, 229, 132, 133, 134, 135},
        {177, 177, 175, 176, 174, 181, 178, 235, 155, 149, 145, 146, 147, 159, 150, 151, 230, 152, 153, 166, 167, 168, 169, 170, 154, 199, 197, 193, 186,
         187, 188, 189, 190, 161, 162, 163, 164, 165, 91, 92, 93, 94, 81, 82, 83, 84, 85, 230, 132, 133, 134, 135, 230},
        {177, 177, 175, 176, 174, 231, 178, 235, 155, 149, 145, 146, 147, 159, 150, 231, 151, 152, 153, 166, 167, 168, 169, 170, 154, 199, 197, 193, 186,
         187, 188, 189, 190, 161, 162, 163, 164, 165, 231, 231, 231, 94, 81, 82, 83, 84, 85, 91, 132, 133, 134, 135},
    }};
    // Only the Divine Beast Cave and Wise Owl Forest have second-half lists of their own.
    static const std::vector<s16> kOwlSecondHalf = {177, 177, 175, 176, 174, 181, 178, 148, 155, 149, 145, 146, 147, 159, 199, 197, 186, 187,
                                                    188, 189, 190, 161, 162, 163, 164, 165, 91, 92, 93, 94, 81, 82, 83, 84, 85, 150, 178, 225,
                                                    225, 225, 225, 150, 151, 151, 152, 153, 154, 132, 133, 134, 135, 235, 166, 167, 168, 169,
                                                    170, 148};
    static const std::vector<s16> kCaveSecondHalf = {145, 146, 147, 148, 148, 155, 149, 159, 181, 178, 150, 151, 151, 224, 199, 197, 186, 187, 188,
                                                     189, 190, 152, 153, 177, 177, 224, 174, 176, 155, 175, 91, 92, 93, 94, 81, 82, 83, 84, 85, 111,
                                                     119, 120, 224, 135, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170};
    static constexpr std::array<s16, 52> kBackFloorItems = {91, 92, 93, 94, 81, 82, 83, 84, 85, 95, 96, 97, 98, 99, 100, 101, 102, 103,
                                                            104, 105, 106, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 161, 162, 163,
                                                            164, 165, 150, 151, 152, 153, 154, 132, 133, 134, 135, 235, 166, 167, 168, 169,
                                                            170, 178};

    if (map == nullptr || selectMapNo < 0 || selectMapNo > 6) {
        return;
    }

    const int  dungeon = selectMapNo;
    const bool chronicle2 = HasChronicle2();
    const bool has_map = CarriesItem(ITEM_MAP);
    const bool has_crystal = CarriesItem(ITEM_MAGICAL_CRYSTAL);

    std::vector<s16> items = kFirstHalfItems[dungeon];

    if (SecondHalf(dungeon, floor_no) && dungeon <= 1) {
        items = dungeon == 0 ? kCaveSecondHalf : kOwlSecondHalf;
    }

    // Past the Demon Shaft's 50th floor its Crystal Eyeballs give way to Auto Repair Powder.
    if (dungeon == 6 && floor_no >= 49) {
        items[5] = 181;
    }

    const std::vector<s16> &weapons = DungeonWeapons(dungeon, floor_no);

    int item_chance = 880 + 15 * dungeon;

    if (chronicle2) {
        item_chance -= 1000 - item_chance;
    }

    for (TREASURE_BOX &box : map->boxes) {
        if (box.used == 0) {
            continue;
        }

        if ((box.item_no == ITEM_MAP && !has_map) || (box.item_no == ITEM_MAGICAL_CRYSTAL && !has_crystal)) {
            continue;
        }

        if (Roll(1000) < item_chance) {
            int item_no = main_floor != 0 ? Pick(items) : Pick(kBackFloorItems);

            // Powerup Powder is rerolled 4 times in 5; with Chronicle 2 on a main floor, 1 in 2 (your
            // change: Expanded keeps every roll, which hands out too many free weapon levels).
            const int reroll_chance = main_floor != 0 && chronicle2 ? 50 : 80;

            if (item_no == ITEM_POWERUP_POWDER && Roll(100) < reroll_chance) {
                item_no = main_floor != 0 ? Pick(items) : Pick(kBackFloorItems);
            }

            box.item_no = item_no;
            box.kind = TREASURE_BOX_SMALL;
            box.trap_no = 0;
        } else {
            box.item_no = Pick(weapons);
            box.kind = TREASURE_BOX_LARGE;
            box.trap_no = Roll(6);
        }
    }

    // A rumour's item (Laura's, Ro's...) waits in the back floor's first small chest when its roll
    // comes up and the player has none yet.
    if (main_floor == 0) {
        const int quest_item = quest::EnhancedBackFloorItem(dungeon);

        for (TREASURE_BOX &box : map->boxes) {
            if (quest_item >= 0 && box.used != 0 && box.kind == TREASURE_BOX_SMALL) {
                box.item_no = quest_item;
                box.trap_no = 0;
                break;
            }
        }
    }

    // The floor's build clears both flags after this, so the floor's first step sets them.
    if (main_floor != 0) {
        g_carried_map = has_map;
        g_carried_crystal = has_crystal;
    }
}

} // namespace

bool PieroItems(int map_no, int floor_no, int *item0, int *item1) {
    // Dark Cloud Enhanced's clown: each box holds one item, drawn half the time (always, once
    // Chronicle 2 is in hand) from the dungeon's clown weapons, else from the common lists. The
    // back floor's clown offers the same as its main floor's.
    static const std::array<std::array<std::vector<s16>, 2>, 7> kBig = {{
        {{{264, 270, 283, 291}, {264, 283, 291}}},
        {{{261, 265, 286}, {261, 265, 286, 319, 320, 327}}},
        {{{263, 288, 293, 318, 327}, {263, 288, 293, 318, 327}}},
        {{{272, 288, 322}, {272, 288, 322, 351, 352, 353}}},
        {{{274, 285, 292, 294, 323, 354, 360}, {274, 285, 292, 294, 323, 354, 360}}},
        {{{276, 273, 294, 285, 323, 355}, {276, 273, 294, 285, 323, 355}}},
        {{{295, 296, 297, 324, 356, 357}, {295, 296, 297, 324, 356, 357}}},
    }};
    static const std::array<std::array<std::vector<s16>, 2>, 7> kSmall = {{
        {{{}, {303, 303}}},
        {{{302, 305}, {302, 305}}},
        {{{302, 306}, {302, 306, 334, 337, 344}}},
        {{{307, 334, 344}, {307, 334, 344}}},
        {{{308, 312, 336, 338, 339}, {308, 312, 336, 338, 339, 368, 375}}},
        {{{312, 336, 338, 339, 369, 370, 371}, {312, 336, 338, 339, 369, 370, 371}}},
        {{{309, 340, 341, 372, 373}, {309, 340, 341, 372, 373}}},
    }};
    static constexpr std::array<s16, 9> kCommonBig = {148, 155, 149, 146, 147, 132, 133, 134, 135};
    // The last entry is the dungeon's own back-floor key.
    static constexpr std::array<s16, 7> kDungeonKey = {224, 225, 226, 228, 229, 230, 230};

    if (map_no < 0 || map_no > 6 || item0 == nullptr || item1 == nullptr) {
        return false;
    }

    std::array<s16, 24> common_small = {91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 186, 187, 188, 189, 190, 197, 199, 224};
    common_small[23] = kDungeonKey[map_no];

    const int  half = SecondHalf(map_no, floor_no) ? 1 : 0;
    const int  lucky_chance = HasChronicle2() ? 0 : 50;
    const auto &big = kBig[map_no][half];
    const auto &small = kSmall[map_no][half];

    *item0 = Roll(100) >= lucky_chance ? Pick(big) : Pick(kCommonBig);

    // The Divine Beast Cave's first half has no clown weapons for the small box.
    *item1 = Roll(100) >= lucky_chance && !small.empty() ? Pick(small) : Pick(common_small);
    return true;
}

void OnMonstersPlaced(CMonstorUnit *unit) {
    // Dark Cloud Expanded's minibosses (expanded_enemies.cpp), in place of Enhanced's.
    MinibossesPlaced(unit);
}

int RandomDungeonWeapon(int dungeon, int floor) {
    if (dungeon < 0 || dungeon > 6) {
        return -1;
    }

    return Pick(DungeonWeapons(dungeon, floor));
}

void OnFloorEvents(CDungeonMap *map, int floor_no, int main_floor) {
    if (map == nullptr) {
        return;
    }

    // Dark Cloud Enhanced: chest randomizer, ahead of the insured chest so that one keeps its sphere.
    RandomizeChests(map, floor_no, main_floor);

    if (main_floor == 0) {
        return;
    }

    for (InsuredSphere &insured : g_insured) {
        insured.box = -1;

        if (insured.dungeon != selectMapNo || (insured.floor != -1 && insured.floor != floor_no)) {
            continue;
        }

        for (int attempt = 0; attempt < 200; attempt++) {
            float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            SearchiDoPutArea(map->cells, 0, 0, 20, 20, pos);

            if (map->CheckTreasureBox(pos, 20.0f) == 0 || map->CheckAtra(pos, 20.0f) == 0 || map->CheckTrapCircle(pos, 20.0f) != nullptr) {
                continue;
            }

            if (map->SetTreasureBox(pos, ITEM_ATTACH_SYNTHESIS_SPHERE, TREASURE_BOX_SMALL, 0) == -1) {
                break;
            }

            for (int box = 0; box < 24; box++) {
                const TREASURE_BOX &chest = map->boxes[box];

                if (chest.used != 0 && chest.item_no == ITEM_ATTACH_SYNTHESIS_SPHERE && chest.pos[0] == pos[0] && chest.pos[2] == pos[2]) {
                    insured.box = box;
                    break;
                }
            }

            break;
        }
    }
}

void OnAttachmentAdded(int slot) {
    if (SaveData == nullptr || NowDngMap == nullptr || g_insured.empty()) {
        return;
    }

    CDngStatusData *status = SaveData->GetDngStatus();
    auto           *attach = reinterpret_cast<ATTACH_LIST *>(&status->consumable_items[slot]);

    if (attach->item_no != ITEM_ATTACH_SYNTHESIS_SPHERE) {
        return;
    }

    // The sphere comes from an insured chest of this floor that has just been opened.
    for (auto insured = g_insured.begin(); insured != g_insured.end(); ++insured) {
        if (insured->dungeon != selectMapNo || insured->box < 0 || NowDngMap->boxes[insured->box].closed != 0) {
            continue;
        }

        *attach = insured->sphere;
        g_insured.erase(insured);
        InsuranceSave();
        return;
    }
}

/**
 * Guardian Grace: Xiao guarding with the Angel Shooter or the Angel Gear, its Heal live.
 */
bool GuardianGrace() {
    const int held = EffectOf(CHARA_XIAO);

    return (held == ITEM_WEAPON_ANGEL_SHOOTER || held == ITEM_WEAPON_ANGE_GEAR) && BtActStatus.guard_mode == kGuarding &&
           NowWeaponHave != nullptr && (NowWeaponHave->flags & 0x800) != 0;
}

void GraceStep() {
    static bool guarding = false;

    if (UserStatus == nullptr || UserStatus->cur_chara != CHARA_XIAO || !GuardianGrace()) {
        g_grace_glow = 0.0f;
        guarding = false;
        return;
    }

    // Dark Cloud Expanded: the Angel Shooter's moment plays once as the guard starts, the
    // healing spring's sparkle with its chime and a gentle white flash.
    if (!guarding) {
        guarding = true;

        if (EffectOf(CHARA_XIAO) == ITEM_WEAPON_ANGEL_SHOOTER) {
            if (HealEffect.active == 0) {
                float at[4];
                sceVu0CopyVector(at, CharaMain.pos);
                HealEffect.Set(at);
            }

            SndSePlay(SE_CHARA_RECOVER, -1, 0);
            setUnitAmbientAnime(60.0f, 1.0f, 110.0f, 110.0f, 110.0f);
        }
    }

    float tint[3];
    Scaled(kGraceGlow, (0.6f + 0.4f * g_grace_glow) * Pulse(), tint);
    fx::PlayerTint(tint);
    g_grace_glow = std::max(0.0f, g_grace_glow - kGraceGlowFade);
}

int HealCounter(int counter, int threshold) {
    // The Heal tick comes every second while she guards, the game's own 1 HP each time.
    constexpr int kGraceFrames = 60;

    const bool grace = GuardianGrace();
    return grace ? std::max(counter, threshold - kGraceFrames) : counter;
}

void OnHealTick() {
    // Each heal brightens the glow a moment.
    if (GuardianGrace()) {
        g_grace_glow = 1.0f;
    }
}

void DrainWhp(int item_no, float base_whp) {
    if (NowWeaponHave == nullptr || base_whp <= 0.0f ||
        (NowWeaponHave->item_no != item_no && (UserStatus == nullptr || EffectOf(UserStatus->cur_chara) != item_no))) {
        return;
    }

    if (g_whp_item != item_no) {
        g_whp_bill = 0.0f;
    }

    g_whp_item = item_no;
    g_whp_bill += base_whp / kSwingWhp;
}

float ShotRadiusScale(const CSHOT_EFFECT *effect, int slot) {
    // Only while the character they were set for is out: another's effect reloads into the
    // same instance.
    if (effect != g_mobius_effect || slot < 0 || slot >= 8 || UserStatus == nullptr || UserStatus->cur_chara != g_radius_chara) {
        return 1.0f;
    }

    return g_mobius_radius[slot];
}

void OnMonstersLoading() {
    // A pockle's kind loading mid-floor keeps what the floor has going.
    if (g_quiet_load) {
        return;
    }

    ConfuseReset();
    g_stolen.Drop();
    g_breath.Drop();
    g_pellets = {};
}

int FavourCircle(int kind) {
    // Favoured Circles: while a Secret Armlet is owned, every bad circle turns out as its good
    // counterpart; the rage circle slows the enemies instead.
    if (!Owned(ITEM_WEAPON_SECRET_ARMLET)) {
        return kind;
    }

    switch (kind) {
        case TRAP_CIRCLE_MONEY_DOWN:
            return TRAP_CIRCLE_MONEY_UP;
        case TRAP_CIRCLE_WHP_DOWN:
            return TRAP_CIRCLE_WHP_UP;
        case TRAP_CIRCLE_WHP_QUARTER:
            return TRAP_CIRCLE_WHP_CURE;
        default:
            return kind;
    }
}

bool FavouredStatCircle(WEAPON_HAVE *weapon, int amp) {
    if (weapon == nullptr || !Owned(ITEM_WEAPON_SECRET_ARMLET)) {
        return false;
    }

    // The stat circle's own roll, landing as a gain: one stat, then one element and one entry
    // of each monster bonus group.
    static constexpr int kStatCap = 999;
    static constexpr int kBonusCap = 99;

    const int gain = std::rand() % (2 * amp + 1) + 2 * amp;
    s16      *stats[] = {&weapon->attack, &weapon->endurance, &weapon->speed, &weapon->magic};
    s16      &stat = *stats[std::rand() % 4];

    stat = static_cast<s16>(std::min(kStatCap, stat + gain));

    auto raise = [amp](auto *values, int count, int base, int range) {
        auto &value = values[std::rand() % count];
        value = static_cast<std::remove_reference_t<decltype(value)>>(std::min(kBonusCap, value + (base + std::rand() % range) * amp));
    };

    raise(weapon->elem, 5, 2, 3);
    raise(weapon->vs_monster, 3, 2, 2);
    raise(&weapon->vs_monster[3], 3, 2, 2);
    raise(&weapon->vs_monster[6], 4, 2, 2);
    return true;
}

bool FavouredRageCircle(CMonstorUnit *unit, int amp) {
    if (unit == nullptr || !Owned(ITEM_WEAPON_SECRET_ARMLET)) {
        return false;
    }

    for (MONSTOR &entry : unit->monster) {
        entry.slow_timer = 300 * amp;
    }

    return true;
}

float BaitRadius(int item_no, float radius) {
    // Bait Flamboyance: each Flamingo owned, up to three, lets fish notice any bait 10 units
    // farther.
    constexpr float kFlamingoBonus = 10.0f;
    constexpr int   kFlamingoMost = 3;

    radius += kFlamingoBonus * static_cast<float>(std::min(OwnedCount(ITEM_WEAPON_FLAMINGO), kFlamingoMost));

    // Venom Lure: with a Scorpion owned, the Poisonous Apple is noticed from twice as far.
    if (item_no == ITEM_POISONOUS_APPLE && Owned(ITEM_WEAPON_SCORPION)) {
        radius *= 2.0f;
    }

    return radius;
}

bool KeepsBait() {
    // Blessed Bait: with a Blessing Gun owned, bait is never lost to a sinking float or a fish
    // that gets away.
    return Owned(ITEM_WEAPON_BLESSING_GUN);
}

void OnFloorStart() {
    // Dark Cloud Enhanced: a Map or Magical Crystal carried in the bag lights the floor's.
    if (g_carried_map) {
        BtEquipMap = 1;
    }

    if (g_carried_crystal) {
        BtEquipMasuisyou = 1;
    }

    g_carried_map = false;
    g_carried_crystal = false;
    g_curse.drain_steps = 0;
    g_curse_xiao.drain_steps = 0;
    g_owl_nearest = -1;
    g_owl_out_of_range = true;
    g_knife_was_in_spring = false;
    g_blade = {};
    g_stride_held = false;
    TunaReset();
    g_stolen.Drop();
    g_breath.Drop();
    g_pellets = {};
    g_crush_hits = {};
    g_aim_holding = false;
    g_charge_armed = 0;
    g_mobius_mult = 1.0f;
    g_mobius_steps = 0;
    g_mobius_effect = nullptr;
    g_radius_chara = -1;
    std::ranges::fill(g_mobius_radius, 1.0f);
    g_typhoon_factor = 1.0f;
    g_typhoon_steps = -1;
    g_solar = Solar::Idle;
    g_solar_blind = 0;
    g_cower = {};
    g_blast_crush = {};
    g_zeus_chain = false;
    g_zeus_charged = false;
    g_zeus_lunging = false;
    g_zeus_camera_held = false;
    g_zeus_charge_steps = 0;
    g_wrath = Wrath::Idle;
    g_whp_bill = 0.0f;
    g_whp_item = -1;
    ConfuseReset();
    PockleReset();
    CatReset();
    FloorClearReset();
    MirageReset();
    ColumnsReset();
    ShieldReset();
    JudgementEnd();
    fx::Reset(); // Dark Cloud Expanded: the effects' models, light and tints.
    quest::EnhancedFloorStart();
}

void DecoyDraw(const float *scene_ambient) {
    if (!g_mirage_lure.active || !MirageWielder()) {
        return;
    }

    // Dark Cloud Expanded: the copy a new cast replaced dissolves where it stood.
    if (g_mirage_old_steps > 0 && g_mirage_old.Valid()) {
        g_mirage_old.Draw(scene_ambient, kMirageAlpha * static_cast<float>(g_mirage_old_steps) / kMirageHandoffSteps);
    }

    // Fades in over its first half second (after the old copy's dissolve) and out over its last.
    const int   shown = g_mirage_life - g_mirage_steps - g_mirage_delay;
    const float in = static_cast<float>(shown) / kMirageFadeSteps;
    const float out = static_cast<float>(g_mirage_steps) / kMirageFadeSteps;
    g_mirage_copy.Draw(scene_ambient, kMirageAlpha * std::clamp(std::min(in, out), 0.0f, 1.0f));
}

void DecoyHaze() {
    // Dark Cloud Expanded: the decoy stands in a heat shimmer, the game's own haze over a torch
    // (CFireOmni::DrawRaster), which needs the floor's torch textures.
    if (!g_mirage_lure.active || !MirageWielder() || NowDngMap == nullptr) {
        return;
    }

    static char kBlender[] = "blender";
    static char kMask[] = "alpha01";

    if (TexManager.GetTexture(kBlender, -1) == nullptr || TexManager.GetTexture(kMask, -1) == nullptr) {
        return;
    }

    // Two rasters, at his waist and his shoulders, as one torch's covers only a flame's height.
    constexpr float kHazeHeights[2] = {4.0f, 14.0f};

    CFireOmni  &fire = NowDngMap->fire;
    float       saved[4];
    std::memcpy(saved, fire.pos, sizeof(saved));

    for (const float height : kHazeHeights) {
        fire.pos[0] = g_mirage_lure.pos[0];
        fire.pos[1] = g_mirage_lure.pos[1] + height;
        fire.pos[2] = g_mirage_lure.pos[2];
        fire.pos[3] = 1.0f;
        fire.DrawRaster();
    }

    std::memcpy(fire.pos, saved, sizeof(saved));
}

void CowerMotion(int m, CCharacter &chara) {
    if (m < 0 || m >= 16 || !g_cower[static_cast<std::size_t>(m)] || NowMonstorUnit == nullptr) {
        return;
    }

    g_cower[static_cast<std::size_t>(m)] = false;
    const int motion = GuardLoopMotion(NowMonstorUnit->monster[m]);

    // Snapped to the guard's first key and held there while the blindness lasts.
    if (motion >= 0) {
        chara.SetMotion(motion, 1 | 4);
    }
}

void ColumnDraw() {
    ColumnsDraw();
    JudgementDraw(); // Dark Cloud Expanded: Big Bang's and the Sword of Zeus's judgement blade.
    ShieldDraw();
    CatDraw(); // Dark Cloud Expanded: the Divine Beast Title's cat.
}

void OnHealCycle() {
    // Angel Gear: each turn of the Heal ability's clock mends the whole party by 1 HP, the
    // knocked out excepted, Xiao herself only when the weapon's own Heal is not live.
    if (UserStatus == nullptr || UserStatus->cur_chara != CHARA_XIAO || EffectOf(CHARA_XIAO) != ITEM_WEAPON_ANGE_GEAR ||
        NowWeaponHave == nullptr) {
        return;
    }

    const bool heal_live = (NowWeaponHave->flags & 0x800) != 0;

    for (int chara = 0; chara < 6; chara++) {
        if (chara == CHARA_XIAO && heal_live) {
            continue;
        }

        if (UserStatus->hp[chara] > 0 && UserStatus->hp[chara] < UserStatus->max_hp[chara]) {
            UserStatus->hp[chara]++;
        }
    }
}

bool MonsterBlocked(int monster_no) {
    return NowMonstorUnit != nullptr && monster_no >= 0 && monster_no < 16 && ColumnBlocksMonster(monster_no);
}

} // namespace dc::game::expanded

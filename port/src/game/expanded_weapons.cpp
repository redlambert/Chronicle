#include "expanded_weapons.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "btactstatus.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "collisiondata.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "dungeonmap.hpp"
#include "dun/gameloop.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "menu_inventory.hpp"
#include "mds.hpp"
#include "menuetc.hpp"
#include "monstorunit.hpp"
#include "randomitem.hpp"
#include "savedata.hpp"
#include "shot_effect_pack.hpp"
#include "shot_effect.hpp"
#include "shot_freefuncs.hpp"
#include "stockitem.hpp"
#include "text/gametext.hpp"
#include "userstatus.hpp"

namespace dc::game::expanded {

namespace {

constexpr int kStepsPerSecond = 60;

bool Is(int item_no, std::initializer_list<int> items) {
    return std::ranges::find(items, item_no) != items.end();
}

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
    {ITEM_WEAPON_BASELARD, "Heavy Hand: every hit\nknocks enemies far\nback."},
    {ITEM_WEAPON_GLADIUS, "Jacket Hunter: Master\nJackets give 4x ABS."},
    {ITEM_WEAPON_WISE_OWL_SWORD, "In Wise Owl Forest,\nwarns you when a key\nmonster is near."},
    {ITEM_WEAPON_CRYSKNIFE, "Thrown gems do 2x.\nOwned: magic circles\nwork twice as hard."},
    {ITEM_WEAPON_BUSTER_SWORD, "Buster Boost: makes\nAnti attachments\nstronger."},
    {ITEM_WEAPON_KITCHEN_KNIFE, "Enter a spring: bigger\nblade, 2x attack and\nfull WHP for 60 sec."},
    {ITEM_WEAPON_TSUKIKAGE, "Quick first swing.\nMoonlit Focus: charge\nattacks fill faster."},
    {ITEM_WEAPON_SUN_SWORD, "Solar Harvest: rare\nchance an enemy drops\na Sun."},
    {ITEM_WEAPON_MACHO_SWORD, "Owned: every weapon\nabsorbs ABS to 2x max.\nExtra carries over."},
    {ITEM_WEAPON_SHAMSHIR, "Swift Strikes:\na faster combo."},
    {ITEM_WEAPON_DARK_CLOUD, "Guard Crush: cuts\nthrough any enemy's\nguard."},
    {ITEM_WEAPON_BRAVE_ARK, "Blocks Freeze, Poison,\nCurse and Goo. Hits\nmimics at any time."},
    {ITEM_WEAPON_AGA_S_SWORD, "Defensive Legacy:\n+15 defense."},
    {ITEM_WEAPON_EVILCISE, "Cursed each floor; no\nother ailments stick.\nCleansed: 1 HP, poison."},
    {ITEM_WEAPON_SMALL_SWORD, "Quick Draw: the first\nswing comes out fast."},
    {ITEM_WEAPON_CLAYMORE, "Greatsword: huge blade\nwith long reach. Hits\nknock enemies back."},
    {ITEM_WEAPON_MANEATER, "Cursed each floor. Low\nWHP: trades 1 HP for\n1 WHP each second."},
    {ITEM_WEAPON_BONE_RAPIER, "Gravedigger: undead\nyou slay stay down."},
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
 * The curse a held Evilcise or Maneater keeps on Toan.
 */
void CurseStep(int held) {
    const int chara = CHARA_TOAN;
    const bool cursing = held == ITEM_WEAPON_EVILCISE || held == ITEM_WEAPON_MANEATER;

    if (!cursing) {
        // Put away: the sword takes its curse with it.
        if (g_curse.item_no != -1) {
            Cure(chara, AILMENT_CURSE);
            g_curse = {};
        }

        return;
    }

    const bool knocked_out = (UserStatus->ailments[chara] & AILMENT_KNOCKED_OUT) != 0;

    if (g_curse.item_no != held) {
        g_curse = {};
        g_curse.item_no = held;
        g_curse.floor = UserStatus->cur_floor;
        g_curse.was_knocked_out = knocked_out;

        if (!knocked_out) {
            Curse(chara);
        }

        return;
    }

    if (g_curse.floor != UserStatus->cur_floor) {
        g_curse.floor = UserStatus->cur_floor;
        g_curse.resolved = false;
        g_curse.was_knocked_out = false;
        Curse(chara);
    }

    if (knocked_out) {
        g_curse.was_knocked_out = true;
    } else if (g_curse.was_knocked_out) {
        g_curse.was_knocked_out = false;

        if (!g_curse.resolved) {
            Curse(chara);
        }
    } else if (!g_curse.resolved) {
        const bool cursed = (UserStatus->ailments[chara] & AILMENT_CURSE) != 0;

        if (!cursed) {
            g_curse.resolved = true;

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
        g_curse.drain_steps++;

        if (g_curse.drain_steps >= kStepsPerSecond) {
            g_curse.drain_steps = 0;
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

    if (held == ITEM_WEAPON_CLAYMORE) {
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
    const bool grants = Is(HeldBy(CHARA_XIAO), {ITEM_WEAPON_DRAGON_S_Y, ITEM_WEAPON_DIVINEBEASTTITLE,
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
int   g_step_count = 0;

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
    if (NowShotEffect == nullptr || entry < 0 || entry >= 5) {
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
    const int held = HeldBy(owner);

    return (owner == CHARA_XIAO && held == ITEM_WEAPON_BANDIT_SLINGSHOT) ||
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

/**
 * Whether a monster is one of the ice kind the Frozen Tuna's frost leaves alone: Blizzard,
 * Sam and the Ice Gemron.
 */
bool IceMonster(const MONSTOR &monster) {
    return monster.name_no == 65 || monster.name_no == 85 || monster.name_no == 312;
}

} // namespace

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

int ToanAttack(int attack) {
    // Spring's Blessing: the blessed Kitchen Knife hits twice as hard.
    if (ToanWeapon() == ITEM_WEAPON_KITCHEN_KNIFE && g_knife_bless_steps > 0) {
        return attack * 2;
    }

    return attack;
}

float LockOnRate() {
    // Sanctifier's reach, which Big Bang and the Sword of Zeus carry on.
    if (LongLockOn(ToanWeapon())) {
        return 2.0f;
    }

    // Flamingo: Xiao locks on from twice as far, and so do the Dragon's Y's line.
    if (Is(HeldBy(CHARA_XIAO), {ITEM_WEAPON_FLAMINGO, ITEM_WEAPON_DRAGON_S_Y, ITEM_WEAPON_DIVINEBEASTTITLE,
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
    if (owner >= 0 && HeldBy(owner) == ITEM_WEAPON_JAVELIN && monster.attachment_kind == MONSTER_FAMILY_SEA) {
        return 0.0f;
    }

    return defense;
}

void OnEnemyHit(MONSTOR &monster, int owner, bool poisoned) {
    if (owner < 0) {
        return;
    }

    const int held = HeldBy(owner);

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

float WearAmount(float amount) {
    // Steel Slingshot: once its WHP is down to a tenth, each shot wears it half as much.
    constexpr float kLowShare = 0.1f;

    const int held = HeldBy(CHARA_XIAO);

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
}

float ToanHitSphere(float *hit_pos, float radius) {
    // Greatsword: the grown blade carries its hit bone out to 1.8 times as far from the hand,
    // which would sail over anything close. One sphere spanning the stock reach to the grown
    // one keeps both: its centre midway along that span, its radius widened by half of it.
    if (ToanWeapon() != ITEM_WEAPON_CLAYMORE || g_blade.frame == nullptr) {
        return radius;
    }

    sceVu0FVECTOR hand;
    sceVu0FVECTOR origin = {0.0f, 0.0f, 0.0f, 1.0f};
    g_blade.frame->GetWorldPosition(hand, origin);

    const float toward_hand = (kClaymoreScale - 1.0f) / (2.0f * kClaymoreScale);

    for (int axis = 0; axis < 3; axis++) {
        hit_pos[axis] -= (hit_pos[axis] - hand[axis]) * toward_hand;
    }

    return radius + kClaymoreHitBone * (kClaymoreScale - 1.0f) / 2.0f;
}

// ── Every step on a floor ───────────────────────────────────────────────────────────────────

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

    // Hero's Courage: Freeze, Poison, Curse and Goo slide off Toan.
    if (held == ITEM_WEAPON_BRAVE_ARK) {
        Cure(CHARA_TOAN, AILMENT_FREEZE | AILMENT_POISON | AILMENT_CURSE | AILMENT_GOO);
    }

    // The curses follow Toan's own weapon whoever is out, so an ally's turn does not lift them.
    if (UserStatus->cur_chara == CHARA_TOAN) {
        CurseStep(held);
    }

    BladeStep(held);
    WiseOwlStep();

    // The allies' ongoing effects.
    g_step_count++;
    StrideStep();
    TornadoStep();
    TunaStep();
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
    const int held = ToanWeapon();

    // Hero's Courage.
    if (held == ITEM_WEAPON_BRAVE_ARK && (ailment & (AILMENT_FREEZE | AILMENT_POISON | AILMENT_CURSE | AILMENT_GOO)) != 0) {
        return true;
    }

    // Jealous Soul: while its curse holds, nothing else takes.
    if (held == ITEM_WEAPON_EVILCISE && ailment != AILMENT_CURSE && (UserStatus->ailments[CHARA_TOAN] & AILMENT_CURSE) != 0) {
        return true;
    }

    return false;
}

int DefenseBonus() {
    // Defensive Legacy: Aga's Sword gives Toan 15 defense.
    constexpr int kAgasDefense = 15;

    return ToanWeapon() == ITEM_WEAPON_AGA_S_SWORD ? kAgasDefense : 0;
}

float ToanDamage(float damage, const MONSTOR &monster) {
    // Sanctifier: about twice the damage against the undead.
    if (Sanctifier(ToanWeapon()) && monster.attachment_kind == MONSTER_FAMILY_UNDEAD) {
        damage *= 2.0f;
    }

    return damage;
}

bool PerfectGuard() {
    // Divine Guard: 7th Heaven's guard blocks the heavy hits retail lets through.
    return ToanWeapon() == ITEM_WEAPON_7THHEAVEN;
}

bool PassesGuard(int hit_owner, int hit) {
    // Guard Crush: Toan's hits connect even while an enemy blocks.
    if (hit_owner == CHARA_TOAN && GuardCrush(ToanWeapon())) {
        return true;
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
    return NoFoolsGold(ToanWeapon()) && (monster.kind == MONSTER_KIND_MIMIC_SMALL || monster.kind == MONSTER_KIND_MIMIC_LARGE);
}

int KillAbs(int exp, const MONSTOR &monster, const WEAPON_HAVE *weapon) {
    if (weapon == nullptr) {
        return exp;
    }

    // Javelin: a sea enemy gives twice its ABS.
    if (weapon->item_no == ITEM_WEAPON_JAVELIN && monster.attachment_kind == MONSTER_FAMILY_SEA) {
        exp *= 2;
    }

    // Jacket Hunter: a Master Jacket the Gladius kills gives four times its ABS.
    constexpr s16 kMasterJacketName = 1;

    if (weapon->item_no == ITEM_WEAPON_GLADIUS && monster.name_no == kMasterJacketName) {
        exp *= 4;
    }

    // Sanctifier: an undead kill gives twice its ABS.
    if (Sanctifier(weapon->item_no) && monster.attachment_kind == MONSTER_FAMILY_UNDEAD) {
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

int AbsCap(int max_exp) {
    // Overtraining: while a Macho Sword is owned, anywhere, every weapon keeps absorbing up
    // to twice its max.
    constexpr int kRolloverFactor = 2;

    return Owned(ITEM_WEAPON_MACHO_SWORD) ? max_exp * kRolloverFactor : max_exp;
}

int LevelUpCarry(WEAPON_HAVE *weapon) {
    // Overtraining: what was absorbed past the max carries into the next level.
    if (weapon == nullptr || !Owned(ITEM_WEAPON_MACHO_SWORD)) {
        return 0;
    }

    return std::max(0, weapon->experience - GetWeaponMaxExp(weapon));
}

void OnKill(const MONSTOR & /*monster*/, WEAPON_HAVE *weapon, float *position) {
    if (weapon == nullptr) {
        return;
    }

    // DeSanga: every kill heals it 5 WHP.
    constexpr float kDeSangaHeal = 5.0f;

    if (weapon->item_no == ITEM_WEAPON_DE_SANGA) {
        weapon->durability_f = std::min(static_cast<float>(weapon->durability), weapon->durability_f + kDeSangaHeal);
    }

    if (RandomItem == nullptr) {
        return;
    }

    // Shooting Stars: one kill in fifty drops an empty SynthSphere.
    if (weapon->item_no == ITEM_WEAPON_STAR_BREAKER && std::rand() % 50 == 0) {
        RandomItem->Set(position, 1, -1, ITEM_ATTACH_SYNTHESIS_SPHERE);
        ShowDungeonMessage("The Star Breaker sent\nyou a shooting star!", 3 * kStepsPerSecond);
    }

    // Solar Harvest: one kill in a hundred drops a Sun attachment.
    if (SolarHarvest(weapon->item_no) && std::rand() % 100 == 0) {
        RandomItem->Set(position, 1, -1, ITEM_ATTACH_SUN);
    }
}

int ChestItem(int item_no) {
    // Fine Fare: water is Premium Water and food Premium Chicken; the Dusack also turns a
    // Treasure Key into Gold Bullion, and the 7 Branch Sword's line gives a Repair Powder one
    // chance in four of being Auto Repair Powder.
    const int tier = FineFareTier(ToanWeapon());

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
    // Buster Boost: an anti-monster attachment is worth +4 instead of +3 in a Buster Sword. The
    // attachment's own value changes, so menus, build-up and the level-up absorb all see it.
    constexpr int kStockAnti = 3;

    if (weapon == nullptr || weapon->item_no != ITEM_WEAPON_BUSTER_SWORD || hole < 0 || hole >= 6) {
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
    constexpr int kBoostedAnti = 4;

    if (weapon == nullptr || weapon->item_no != ITEM_WEAPON_BUSTER_SWORD || hole < 0 || hole >= 6) {
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
    ShowDungeonMessage("Projectile stolen! Your shots\nare its shots on this floor.", 4 * kStepsPerSecond);
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

bool XiaoAimStep(bool holding) {
    const int  held = HeldBy(CHARA_XIAO);
    const bool charges = held == ITEM_WEAPON_MATADOR || held == ITEM_WEAPON_DRAGON_S_Y;
    bool       full = false;

    if (holding && charges) {
        if (!g_aim_holding) {
            g_aim_holding = true;
            g_aim_steps = 0;
            g_aim_charged = false;
        }

        if (++g_aim_steps >= kChargeSteps && !g_aim_charged) {
            g_aim_charged = true;
            full = true;
        }
    } else if (g_aim_holding) {
        g_aim_holding = false;
        g_charge_armed = g_aim_charged ? kChargeArmSteps : 0;
        g_aim_charged = false;
    } else if (g_charge_armed > 0) {
        g_charge_armed--;
    }

    return full;
}

bool XiaoShot(CSHOT *shots, const float *world, const float *vector, int damage) {
    if (FireStolenShot(world, vector, damage, CHARA_XIAO)) {
        return true;
    }

    const int  held = HeldBy(CHARA_XIAO);
    const bool charged = g_charge_armed > 0;
    g_charge_armed = 0;

    const int strong = static_cast<int>(static_cast<float>(damage) * kChargedDamage);

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

    // Ricochet: the Hardshooter's pellets, and Double Impact's, send a second pellet on from
    // the monster they hit at the nearest other one within reach, or anywhere when none is.
    const int held = HeldBy(CHARA_XIAO);

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
    g_mobius_effect = effect;
    g_mobius_radius[slot] = scale;
}

float ShotRadiusScale(const CSHOT_EFFECT *effect, int slot) {
    // Only while Ruby is out: another character's effect reloads into the same instance.
    if (effect != g_mobius_effect || slot < 0 || slot >= 8 || UserStatus == nullptr || UserStatus->cur_chara != CHARA_RUBY) {
        return 1.0f;
    }

    return g_mobius_radius[slot];
}

void OnMonstersLoading() {
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
    g_curse.drain_steps = 0;
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
    std::ranges::fill(g_mobius_radius, 1.0f);
}

} // namespace dc::game::expanded

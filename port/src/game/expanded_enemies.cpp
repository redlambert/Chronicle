#include "expanded_enemies.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "character.hpp"
#include "dataalloc.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "expanded_weapons.hpp"
#include "itemdata.hpp"
#include "monstorunit.hpp"
#include "platform/config.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

namespace dc::game::expanded {

namespace {

// ---------------------------------------------------------------------------------------------
// Minibosses (Enemies/MiniBoss.cs, Enemies/MiniBossLootTables.cs)
// ---------------------------------------------------------------------------------------------

constexpr float kScale = 1.5f;           // model and body spheres
constexpr float kWeaponSphere = 1.6f;    // attack spheres: more than the model, so they still reach short characters
constexpr float kReach = 1.3f;           // distance a miniboss commits to its attack from
constexpr float kReachWide = 1.5f;       // the long-reach plants and the chariot
constexpr float kHp = 3.0f;              // life
constexpr float kDefense = 1.5f;         // defense and hardness
constexpr float kAttack = 1.5f;          // melee hits and projectiles that carry their own damage
constexpr float kWalkSync = 1.3f;        // walk clip slowed so the longer strides plant the feet
constexpr float kReticle = 1.5f;         // lock-on cursor
constexpr int   kAbs = 3;                // experience
constexpr int   kItemResist = 10;        // percentage of thrown-item damage taken
constexpr int   kMoney = 3;              // money
constexpr int   kLootChance = 100;       // chance of dropping money
constexpr int   kSlots = 15;             // slots the roll looks at, as Expanded's does
constexpr int   kBoostTimeout = 60 * 300; // steps to wait for a boosted drop's miniboss to fall

// Bosses and their companions: never promoted.
constexpr std::array<s16, 16> kBosses = {84, 112, 113, 114, 115, 116, 117, 119, 120, 121, 221, 240, 241, 242, 243, 244};

// Cursed Rose, Dark Flower, Cannibal Plant, Opar, King Prickly, Rockanoff, Pirate's Chariot.
constexpr std::array<s16, 7> kWideReach = {68, 67, 11, 304, 306, 77, 25};

// Cave Bat and Evil Bat: motion 1 is a hover, not a walk.
constexpr std::array<s16, 2> kNoWalkSync = {60, 61};

constexpr std::array<s16, 7> kBackFloorKeys = {ITEM_TRAM_OIL,        ITEM_SUN_DEW,        ITEM_FLAPPING_FISH,  ITEM_SECRET_PATH_KEY,
                                               ITEM_BRAVERY_LAUNCH, ITEM_FLAPPING_DUSTER, ITEM_CRYSTAL_EYEBALL};

// Garnet to Turquoise.
constexpr std::array<s16, 12> kGems = {95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106};

// The elements, the stats and the slayers.
constexpr std::array<s16, 19> kAttachments = {81, 82, 83, 84, 85, 91, 92, 93, 94, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120};

constexpr std::array<s16, 2> kLuckyItems = {ITEM_POWERUP_POWDER, ITEM_AUTO_REPAIR_POWDER};
constexpr std::array<s16, 2> kItems = {ITEM_STAMINA_DRINK, ITEM_DRAN_S_FEATHER};

struct Pool {
    s16              species;
    std::vector<s16> items;
};

/**
 * What a boosted weapon drop is given once it reaches the bag. Zero leaves a value as it is.
 */
struct Boost {
    s16 weapon = 0;
    s16 attack = 0;
    s16 endurance = 0;
    s16 speed = 0;
    s16 magic = 0;
    s16 whp = 0;
    s8  fire = 0;
    s8  ice = 0;
    s8  thunder = 0;
    s8  wind = 0;
    s8  holy = 0;
    s8  anti_dragon = 0;
    s8  anti_undead = 0;
    s8  anti_marine = 0;
    s8  anti_rock = 0;
    s8  anti_plant = 0;
    s8  anti_beast = 0;
    s8  anti_sky = 0;
    s8  anti_mimic = 0;
    s8  anti_mage = 0;
    u8  special1 = 0; // low byte of the weapon's flags
    u8  special2 = 0; // high byte of the weapon's flags
};

struct DungeonLoot {
    std::vector<Pool>  rare;          // 5%
    std::vector<Pool>  flavor;        // 30%
    std::vector<Boost> rare_boosts;   // given to a rare drop
    std::vector<Boost> flavor_boosts; // given to a flavor drop
};

#define ANY_GEM 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106
#define GOLD_OR_GEM 192, ANY_GEM

const std::array<DungeonLoot, 7> &Loot() {
    static const std::array<DungeonLoot, 7> kLoot = {{
        // Divine Beast Cave
        {
            {{1, {260}}, {303, {303}}, {301, {282}}},
            {{77, {98}},
             {59, {101, 95}},
             {3, {290, 304}},
             {1, {260}},
             {5, {301}},
             {303, {106}},
             {6, {105}},
             {304, {104}},
             {301, {96}},
             {34, {GOLD_OR_GEM}},
             {35, {ANY_GEM}}},
            {{.weapon = 260, .attack = 15, .holy = 50, .anti_undead = 50}},
            {},
        },
        // Wise Owl Forest
        {
            {{12, {328}}, {19, {302}}, {10, {303}}, {15, {302}}, {20, {302}}, {14, {302}}, {18, {302}}, {16, {302}}, {17, {302}}, {7, {272}}},
            {{12, {327}},
             {19, {183}},
             {305, {103, 310}},
             {306, {316}},
             {15, {183}},
             {20, {183}},
             {14, {183}},
             {18, {183}},
             {16, {183}},
             {17, {183}},
             {78, {GOLD_OR_GEM}},
             {79, {ANY_GEM}}},
            {{.weapon = 302, .attack = 35, .wind = 40, .anti_mimic = 40}, {.weapon = 272, .attack = 60, .magic = 30, .whp = 99}},
            {},
        },
        // Shipwreck
        {
            {{26, {268}}, {27, {192}}, {28, {293}}, {75, {283}}},
            {{26, {106, 95}},
             {27, {310}},
             {28, {286}},
             {68, {344}},
             {23, {100}},
             {24, {321, 97}},
             {75, {96}},
             {25, {316, 178}},
             {85, {334}},
             {80, {GOLD_OR_GEM}},
             {81, {ANY_GEM}}},
            {{.weapon = 293, .special1 = 0x80}, {.weapon = 283, .attack = 40, .thunder = 40, .anti_mage = 40}},
            {},
        },
        // Sun and Moon Temple
        {
            {{73, {307}}, {32, {360}}, {30, {267, 266}}, {50, {288}}, {64, {335}}},
            {{73, {96, 97, 106}},
             {49, {178}},
             {71, {104}},
             {32, {178}},
             {30, {183}},
             {31, {368}},
             {50, {176, 102}},
             {64, {104, 98}},
             {36, {GOLD_OR_GEM}},
             {37, {ANY_GEM}}},
            {{.weapon = 335, .attack = 30, .ice = 40, .anti_rock = 40, .special2 = 0x02}},
            {},
        },
        // Moon Sea
        {
            {{76, {266, 354}}, {62, {339}}, {66, {328}}, {72, {321}}, {33, {322}}, {70, {368}}, {56, {351}}},
            {{40, {375, 359}},
             {76, {100}},
             {62, {343}},
             {57, {178}},
             {66, {317}},
             {72, {349}},
             {33, {ANY_GEM}},
             {70, {102, 95, 105, 98}},
             {56, {105}},
             {38, {GOLD_OR_GEM}},
             {39, {ANY_GEM}}},
            {{.weapon = 321, .endurance = 99, .whp = 99, .ice = 99, .anti_marine = 99, .special1 = 0x02, .special2 = 0x01},
             {.weapon = 368, .attack = 50, .magic = 30, .fire = 40, .anti_dragon = 40, .anti_plant = 20},
             {.weapon = 351, .special2 = 0x04}},
            {{.weapon = 343, .special1 = 0x80}},
        },
        // Gallery of Time
        {
            {{74, {307}}, {45, {360}}, {67, {344}}, {46, {275}}, {44, {309}}, {48, {312}}, {47, {273}}},
            {{43, {285}},
             {69, {328}},
             {74, {102}},
             {65, {97, 96, 106}},
             {45, {316}},
             {52, {289, 294}},
             {67, {344, 292}},
             {46, {98}},
             {44, {336}},
             {48, {312}},
             {47, {274}},
             {82, {GOLD_OR_GEM}},
             {83, {ANY_GEM}}},
            {{.weapon = 307, .thunder = 40, .anti_beast = 40, .anti_sky = 40},
             {.weapon = 360, .endurance = 99, .whp = 99, .wind = 50, .anti_marine = 40, .anti_plant = 40, .special2 = 0x10},
             {.weapon = 344, .special2 = 0x20},
             {.weapon = 275, .whp = 99, .special2 = 0x02},
             {.weapon = 309, .whp = 99, .fire = 40, .thunder = 40, .holy = 40},
             {.weapon = 312, .attack = 80, .magic = 50, .whp = 99, .special1 = 0x80, .special2 = 0x20},
             {.weapon = 273, .whp = 99, .fire = 40, .holy = 40, .anti_mage = 30, .special1 = 0x40, .special2 = 0x04}},
            {{.weapon = 328, .thunder = 50}},
        },
        // Demon Shaft
        {
            {{311, {339}}, {308, {269}}, {319, {325}}, {312, {334}}, {313, {328}}, {318, {338}}, {314, {271}}, {315, {336}}},
            {{311, {107, 101}},
             {319, {311, 290}},
             {312, {107, 97}},
             {316, {310}},
             {313, {107, 100}},
             {318, {304}},
             {314, {107, 103}},
             {315, {107, 102}},
             {310, {GOLD_OR_GEM}},
             {309, {ANY_GEM}}},
            {{.weapon = 334, .ice = 50}, {.weapon = 328, .thunder = 50}, {.weapon = 271, .wind = 50}},
            {{.weapon = 310, .special2 = 0x20}},
        },
    }};

    return kLoot;
}

#undef GOLD_OR_GEM
#undef ANY_GEM

int Roll(int bound) {
    return static_cast<int>(static_cast<float>(bound) * static_cast<float>(rand()) / 2147483648.0f);
}

template <typename T>
int Pick(const T &table) {
    return table[Roll(static_cast<int>(std::size(table)))];
}

int ScaleRound(int value, float factor) {
    return value <= 0 ? value : std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * factor)));
}

template <typename T, std::size_t N>
bool Contains(const std::array<T, N> &table, int value) {
    return std::ranges::find(table, value) != table.end();
}

struct Miniboss {
    int slot;
    s16 name_no;
    bool alive;
};

struct PendingBoost {
    int                        slot;
    Boost                      boost;
    bool                       fallen = false;
    int                        steps = 0;
    std::array<s16, 6 * 11>    before{};
};

std::vector<Miniboss>                     g_minibosses;
std::vector<PendingBoost>                 g_boosts;
std::array<std::optional<std::vector<Miniboss>>, 2> g_saved; // [back floor]: what was left when the party went to the other side
int                                       g_dungeon = -1;
int                                       g_floor = -1;
int                                       g_back = -1;

const Miniboss *FindMiniboss(int monster_no) {
    for (const Miniboss &boss : g_minibosses) {
        if (boss.slot == monster_no) {
            return &boss;
        }
    }

    return nullptr;
}

bool HoldsKey(const MONSTOR &monster) {
    return monster.drop_item != -1 && monster.drop_item != 0;
}

bool Eligible(CMonstorUnit *unit, int m) {
    const MONSTOR &monster = unit->monster[m];

    return monster.state >= 1 && monster.hp > 0 && monster.kind != MONSTER_KIND_NO_LOCK_ON && !Contains(kBosses, monster.name_no) &&
           !HoldsKey(monster) && !IsPockleSlot(m);
}

void LootFor(CMonstorUnit *unit, int m, int dungeon) {
    MONSTOR           &boss = unit->monster[m];
    const DungeonLoot &loot = Loot()[dungeon];

    auto boosted = [&](const std::vector<Boost> &boosts, int item) {
        for (const Boost &boost : boosts) {
            if (boost.weapon == item) {
                g_boosts.push_back({.slot = m, .boost = boost});
                return;
            }
        }
    };

    auto pool_of = [&](const std::vector<Pool> &pools) -> const std::vector<s16> * {
        for (const Pool &pool : pools) {
            if (pool.species == boss.name_no) {
                return &pool.items;
            }
        }

        return nullptr;
    };

    // The species' own drops first: a rare one 5% of the time, then a flavour one 30%.
    if (const auto *rare = pool_of(loot.rare); rare != nullptr && Roll(100) < 5) {
        boss.drop_item = Pick(*rare);
        boosted(loot.rare_boosts, boss.drop_item);
        std::println("[Miniboss] slot {} rolled rare loot {}", m, boss.drop_item);
        return;
    }

    if (const auto *flavor = pool_of(loot.flavor); flavor != nullptr && Roll(100) < 30) {
        boss.drop_item = Pick(*flavor);
        boosted(loot.flavor_boosts, boss.drop_item);
        std::println("[Miniboss] slot {} rolled flavor loot {}", m, boss.drop_item);
        return;
    }

    if (Roll(100) < 35) {
        boss.drop_item = kBackFloorKeys[dungeon];
    } else if (Roll(100) < 15) {
        boss.drop_item = RandomDungeonWeapon(dungeon, UserStatus->cur_floor);
    } else if (Roll(100) < 80) {
        boss.drop_item = Roll(100) < 60 ? Pick(kGems) : Pick(kAttachments);
    } else {
        boss.drop_item = Roll(100) < 60 ? Pick(kLuckyItems) : Pick(kItems);
    }

    std::println("[Miniboss] slot {} rolled loot {}", m, boss.drop_item);
}

void Promote(CMonstorUnit *unit, int m, int dungeon) {
    MONSTOR &boss = unit->monster[m];

    for (int i = 0; i <= boss.attachment_count && i < 3; i++) {
        CCharacter &part = unit->chara[m][i];
        part.scale[0] *= kScale;
        part.scale[1] *= kScale;
        part.scale[2] *= kScale;
    }

    // Spheres the script has already set up; the ones it sets up later go through the hooks.
    for (int i = 0; i < 16; i++) {
        if (unit->effect[m].timer[i] != 0 && unit->effect[m].radius[i] > 0.0f) {
            unit->effect[m].radius[i] *= kScale;
        }

        if (unit->effect2[m].active[i] != 0) {
            const float radius = unit->effect2[m].radius[i];

            if (radius > 0.0f && radius < 200.0f) {
                unit->effect2[m].radius[i] = radius * kWeaponSphere;
            }

            unit->effect2[m].damage[i] = ScaleRound(unit->effect2[m].damage[i], kAttack);
        }
    }

    if (boss.lockon_scale_x > 0.0f) {
        boss.lockon_scale_x *= kReticle;
    }

    if (boss.lockon_scale_y > 0.0f) {
        boss.lockon_scale_y *= kReticle;
    }

    if (boss.max_hp > 0) {
        const int max_hp = ScaleRound(boss.max_hp, kHp);
        boss.hp = std::max(1, static_cast<int>(std::lround(static_cast<double>(boss.hp) * max_hp / boss.max_hp)));
        boss.max_hp = max_hp;
    }

    boss.defense = static_cast<s16>(ScaleRound(boss.defense, kDefense));
    boss.hardness = static_cast<s16>(ScaleRound(boss.hardness, kDefense));
    boss.exp *= kAbs;
    boss.money *= kMoney;
    boss.money_chance = kLootChance;
    boss.drops_items = 1;

    // Expanded writes the resistance as a word, which also clears the status chance beside it, so
    // its minibosses shrug off every status hit. That side effect is left out here: a miniboss takes
    // status hits as its species does.
    boss.item_damage_rate = kItemResist;

    LootFor(unit, m, dungeon);
    g_minibosses.push_back({.slot = m, .name_no = boss.name_no, .alive = true});
    std::println("[Miniboss] slot {} (species {}) is a miniboss: hp {}", m, boss.name_no, boss.max_hp);
}

void RollMinibosses(CMonstorUnit *unit, int dungeon) {
    // Each eligible enemy has a 1-in-(15 minus the ineligible) chance; every winner is promoted.
    int ineligible = 0;

    for (int m = 0; m < kSlots; m++) {
        if (!Eligible(unit, m)) {
            ineligible++;
        }
    }

    const int denominator = std::max(1, kSlots - ineligible);
    int       winners = 0;

    for (int m = 0; m < kSlots; m++) {
        if (Eligible(unit, m) && Roll(denominator) == 0) {
            Promote(unit, m, dungeon);
            winners++;
        }
    }

    std::println("[Miniboss] floor roll: 1 in {}, {} miniboss(es)", denominator, winners);
}

void RestoreMinibosses(CMonstorUnit *unit, int dungeon, const std::vector<Miniboss> &saved) {
    std::vector<int> restored;

    for (const Miniboss &boss : saved) {
        const MONSTOR &monster = unit->monster[boss.slot];

        if (boss.alive && monster.state >= 1 && monster.hp > 0 && monster.name_no == boss.name_no) {
            restored.push_back(boss.slot);
        }
    }

    for (const int slot : restored) {
        MONSTOR &monster = unit->monster[slot];

        // The floor's key went to a slot being restored: it moves to another enemy.
        if (HoldsKey(monster)) {
            for (int other = 0; other < kSlots; other++) {
                MONSTOR &candidate = unit->monster[other];

                if (std::ranges::find(restored, other) != restored.end() || candidate.state < 1 || candidate.hp <= 0 || HoldsKey(candidate)) {
                    continue;
                }

                candidate.drop_item = monster.drop_item;
                std::println("[Miniboss] key moved from slot {} to slot {}", slot, other);
                break;
            }

            monster.drop_item = -1;
        }

        Promote(unit, slot, dungeon);
    }
}

void ApplyBoost(WEAPON_HAVE &weapon, const Boost &boost) {
    auto set = [](auto &field, auto value) {
        if (value > 0) {
            field = value;
        }
    };

    set(weapon.attack, boost.attack);
    set(weapon.endurance, boost.endurance);
    set(weapon.speed, boost.speed);
    set(weapon.magic, boost.magic);

    if (boost.whp > 0) {
        weapon.durability = boost.whp;
        weapon.durability_f = static_cast<float>(boost.whp);
    }

    set(weapon.elem[0], boost.fire);
    set(weapon.elem[1], boost.ice);
    set(weapon.elem[2], boost.thunder);
    set(weapon.elem[3], boost.wind);
    set(weapon.elem[4], boost.holy);
    set(weapon.vs_monster[0], static_cast<char>(boost.anti_dragon));
    set(weapon.vs_monster[1], static_cast<char>(boost.anti_undead));
    set(weapon.vs_monster[2], static_cast<char>(boost.anti_marine));
    set(weapon.vs_monster[3], static_cast<char>(boost.anti_rock));
    set(weapon.vs_monster[4], static_cast<char>(boost.anti_plant));
    set(weapon.vs_monster[5], static_cast<char>(boost.anti_beast));
    set(weapon.vs_monster[6], static_cast<char>(boost.anti_sky));
    set(weapon.vs_monster[8], static_cast<char>(boost.anti_mimic));
    set(weapon.vs_monster[9], static_cast<char>(boost.anti_mage));

    // Expanded writes the ability bytes over the weapon's own.
    auto flags = static_cast<u16>(weapon.flags);

    if (boost.special1 != 0) {
        flags = static_cast<u16>((flags & 0xFF00) | boost.special1);
    }

    if (boost.special2 != 0) {
        flags = static_cast<u16>((flags & 0x00FF) | (boost.special2 << 8));
    }

    weapon.flags = static_cast<s16>(flags);
    std::println("[Miniboss] boost applied to weapon {}", boost.weapon);
}

// ---------------------------------------------------------------------------------------------
// Harder enemy AI (Enemies/HarderEnemyAI.cs)
// ---------------------------------------------------------------------------------------------

constexpr int   kSplicedChance = 30;  // revive chance of monsters that never revived
constexpr int   kNativeChance = 45;   // revive chance of the native revivers (10 to 18% in the game)
constexpr float kReviveLife = 0.4f;   // fraction of life a revived monster stands up with
constexpr int   kGetUpMotion = 10;    // every real get-up motion

// Monsters with a real get-up motion, by model code.
constexpr std::array<std::string_view, 31> kGetUpCodes = {"e01a", "e03a", "e14a", "e15a", "e16a", "e17a", "e18a", "e19a", "e20a", "e44a", "e45a",
                                                          "e46a", "e47a", "e48a", "e50a", "e52a", "e62a", "e77a", "e106", "e124", "e129", "e131",
                                                          "e132", "e119", "e134", "e138", "e144", "e118", "e150", "e117", "e161"};

struct NativeReviver {
    std::string_view code;
    int              threshold_cell; // file offset of the push holding the revive threshold
};

// Master Jacket, Mummy, Gacious (Enhanced), Horn Head, Gacious, Master Jacket (Enhanced),
// Mummy (Enhanced), Silver Gear.
constexpr std::array<NativeReviver, 8> kNativeRevivers = {{
    {"e01a", 0x715C},
    {"e50a", 0x4A08},
    {"e117", 0x7550},
    {"e119", 0x54DC},
    {"e124", 0x7494},
    {"e129", 0x7174},
    {"e131", 0x4A08},
    {"e118", 0x7F38},
}};

struct EndFrame {
    std::string_view code;
    int              frame;
};

// The get-up motion's last frame.
constexpr std::array<EndFrame, 23> kGetUpEnd = {{
    {"e03a", 185}, {"e14a", 220}, {"e15a", 220}, {"e16a", 220}, {"e17a", 220}, {"e18a", 220}, {"e19a", 220}, {"e20a", 220},
    {"e44a", 225}, {"e45a", 225}, {"e46a", 225}, {"e47a", 225}, {"e48a", 225}, {"e52a", 340}, {"e62a", 220}, {"e77a", 160},
    {"e106", 190}, {"e132", 225}, {"e134", 160}, {"e138", 225}, {"e144", 225}, {"e150", 225}, {"e161", 225},
}};

struct FakeDeath {
    std::string_view code;
    int              motion;
    int              end_frame;
};

// Monsters whose real death throws them backwards: a revive plays the knockdown instead.
constexpr std::array<FakeDeath, 19> kFakeDeaths = {{
    {"e14a", 9, 190}, {"e15a", 9, 190}, {"e16a", 9, 190}, {"e17a", 9, 190}, {"e18a", 9, 190}, {"e19a", 9, 190}, {"e20a", 9, 190},
    {"e62a", 9, 190}, {"e52a", 9, 310}, {"e44a", 9, 200}, {"e45a", 9, 200}, {"e46a", 9, 200}, {"e47a", 9, 200}, {"e48a", 9, 200},
    {"e132", 9, 200}, {"e138", 9, 200}, {"e144", 9, 200}, {"e150", 9, 200}, {"e161", 9, 200},
}};

using Cell = std::array<u32, 3>;

struct Script {
    const char *data;
    int         size;

    u32 operator()(int offset) const {
        u32 value = 0;

        if (offset >= 0 && offset + 4 <= size) {
            std::memcpy(&value, data + offset, 4);
        }

        return value;
    }

    Cell CellAt(int offset) const { return {(*this)(offset), (*this)(offset + 4), (*this)(offset + 8)}; }
};

u32 FloatBits(float value) {
    u32 bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

bool FindLabel120(const Script &file, int &label_off, int &exec_off) {
    const int table = static_cast<int>(file(0xC));
    const int count = static_cast<int>(file(0x10));
    const int code_base = static_cast<int>(file(8));

    for (int i = 0; i < count && table + i * 8 + 8 <= file.size; i++) {
        if (static_cast<int>(file(table + i * 8)) == 120) {
            label_off = static_cast<int>(file(table + i * 8 + 4));
            exec_off = code_base + static_cast<int>(file(label_off));
            return label_off > 0 && exec_off > 0 && exec_off + 12 <= file.size;
        }
    }

    return false;
}

// The death motion's call in label 120: the cells from `push 200` (_SET_MOTION) to its EXT.
std::optional<std::vector<Cell>> DeathMotionRun(const Script &file, int exec_off, int &after_run) {
    static constexpr std::array<u32, 9> kAllowed = {1, 2, 3, 5, 6, 7, 11, 24, 25};

    for (int c = exec_off; c + 12 <= file.size && c < exec_off + 80 * 12; c += 12) {
        if (file(c) == 15) {
            return std::nullopt;
        }

        if (file(c) != 3 || file(c + 4) != 1 || file(c + 8) != 200) {
            continue;
        }

        std::vector<Cell> run;

        for (int c2 = c; c2 + 12 <= file.size && run.size() < 12; c2 += 12) {
            const u32 op = file(c2);
            run.push_back(file.CellAt(c2));

            if (op == 21) {
                after_run = c2 + 12;
                return run;
            }

            if (!Contains(kAllowed, static_cast<int>(op))) {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    return std::nullopt;
}

// The wait-for-motion cluster after the collapse: argument pushes, the CALL and the drop.
std::optional<std::vector<Cell>> WaitCluster(const Script &file, int offset) {
    std::vector<Cell> cluster;
    bool              saw_call = false;

    for (int c = offset; c + 12 <= file.size && cluster.size() < 10; c += 12) {
        const u32 op = file(c);

        if (op != 3 && op != 6 && op != 19 && op != 4) {
            return std::nullopt;
        }

        cluster.push_back(file.CellAt(c));

        if (op == 19) {
            saw_call = true;
        }

        if (op == 4 && saw_call) {
            return cluster;
        }
    }

    return std::nullopt;
}

template <typename T>
const T *ByCode(const T *begin, const T *end, std::string_view code) {
    for (const T *entry = begin; entry != end; ++entry) {
        if (entry->code == code) {
            return entry;
        }
    }

    return nullptr;
}

void BuffNativeReviver(const NativeReviver &reviver, char *script, int size) {
    Script    file{script, size};
    const int cell = reviver.threshold_cell;

    if (file(cell) != 3 || file(cell + 4) != 1) {
        std::println("[HarderAI] {}: threshold cell not found", reviver.code);
        return;
    }

    const u32 current = file(cell + 8);

    if (current == 0 || current > 100) {
        return;
    }

    const u32 chance = kNativeChance;
    std::memcpy(script + cell + 8, &chance, 4);
    std::println("[HarderAI] {}: revive chance {} -> {}", reviver.code, current, chance);
}

void SpliceRevive(std::string_view code, char *script, int size, CDataAlloc2<1> *alloc) {
    Script file{script, size};
    int    label_off = -1;
    int    exec_off = -1;

    if (!FindLabel120(file, label_off, exec_off)) {
        std::println("[HarderAI] {}: no death label", code);
        return;
    }

    // Patch point: the push of 101 right after the intro's `push 34; EXT(1)`.
    int patch = -1;

    for (int k = 0; k < 10; k++) {
        const int c = exec_off + k * 12;

        if (c + 36 > size) {
            break;
        }

        if (file(c) == 3 && file(c + 4) == 1 && file(c + 8) == 34 && file(c + 12) == 21 && file(c + 16) == 1 && file(c + 20) == 0) {
            patch = c + 24;
            break;
        }
    }

    if (patch < 0 || file(patch) != 3 || file(patch + 4) != 1 || file(patch + 8) != 101) {
        std::println("[HarderAI] {}: death label shape unrecognized", code);
        return;
    }

    const int code_base = static_cast<int>(file(8));
    int       after_run = -1;

    auto                             motion = DeathMotionRun(file, exec_off, after_run);
    std::optional<std::vector<Cell>> wait;
    std::optional<std::vector<Cell>> get_up_template;
    const FakeDeath                 *fake = ByCode(kFakeDeaths.data(), kFakeDeaths.data() + kFakeDeaths.size(), code);

    if (motion) {
        wait = WaitCluster(file, after_run);

        if (!wait) {
            std::println("[HarderAI] {}: no wait after the collapse", code);
            return;
        }

        get_up_template = wait;
    } else {
        // The label calls an argless subroutine that holds the collapse and its wait.
        for (int c = patch; c + 24 <= size && c < patch + 30 * 12 && !motion; c += 12) {
            if (file(c) == 15) {
                break;
            }

            if (file(c) != 19 || file(c + 12) != 4) {
                continue;
            }

            const int callee = code_base + static_cast<int>(file(c + 8));

            if (callee + 16 > size || file(callee + 12) != 0) {
                continue;
            }

            int  sub_after = -1;
            auto sub_run = DeathMotionRun(file, code_base + static_cast<int>(file(callee)), sub_after);

            if (!sub_run) {
                continue;
            }

            auto sub_wait = WaitCluster(file, sub_after);

            if (!sub_wait) {
                continue;
            }

            if (fake != nullptr) {
                motion = sub_run;
                wait = sub_wait;
            } else {
                motion = std::vector<Cell>{file.CellAt(c), Cell{4, 0, 0}};
                wait = std::vector<Cell>{};
            }

            get_up_template = sub_wait;
        }

        if (!motion) {
            std::println("[HarderAI] {}: no death motion", code);
            return;
        }
    }

    // The get-up's wait: the motion index before the ADD becomes the get-up's, the end frame its own.
    std::vector<Cell> wait_get_up = *get_up_template;
    bool              need_frame = false;

    for (std::size_t i = 0; i < wait_get_up.size(); i++) {
        if (wait_get_up[i][0] == 3 && wait_get_up[i][1] == 1 && i + 1 < wait_get_up.size() && wait_get_up[i + 1][0] == 6) {
            wait_get_up[i][2] = kGetUpMotion;
        }

        if (wait_get_up[i][0] == 3 && wait_get_up[i][1] == 2) {
            need_frame = true;
        }
    }

    if (need_frame) {
        const EndFrame *end = ByCode(kGetUpEnd.data(), kGetUpEnd.data() + kGetUpEnd.size(), code);

        if (end == nullptr) {
            std::println("[HarderAI] {}: get-up end frame unknown", code);
            return;
        }

        for (Cell &cell : wait_get_up) {
            if (cell[0] == 3 && cell[1] == 2) {
                cell[2] = FloatBits(static_cast<float>(end->frame - 5));
            }
        }
    }

    if (fake != nullptr) {
        (*motion)[1] = Cell{3, 1, static_cast<u32>(fake->motion)};

        for (std::size_t i = 0; i < wait->size(); i++) {
            Cell &cell = (*wait)[i];

            if (cell[0] == 3 && cell[1] == 1 && i + 1 < wait->size() && (*wait)[i + 1][0] == 6) {
                cell[2] = static_cast<u32>(fake->motion);
            }

            if (cell[0] == 3 && cell[1] == 2) {
                cell[2] = FloatBits(static_cast<float>(fake->end_frame - 5));
            }
        }
    }

    // The stub sits right after the script; branch targets count from the code section.
    constexpr int kStubBytes = 85 * 12;
    const int     quads = (kStubBytes + 15) / 16;

    if (alloc == nullptr || alloc->used + quads >= alloc->limit) {
        std::println("[HarderAI] {}: no room for the revive code", code);
        return;
    }

    char          *stub = reinterpret_cast<char *>(alloc->Alloc(quads));
    const char    *origin = script + code_base;
    const std::ptrdiff_t stub_off = stub - origin;

    if (stub_off <= 0 || stub_off > 0x7FFF0000) {
        std::println("[HarderAI] {}: revive code out of reach", code);
        return;
    }

    auto at = [&](std::size_t index) { return static_cast<u32>(stub_off + static_cast<std::ptrdiff_t>(index) * 12); };

    std::vector<Cell> cells = {
        {3, 1, 180},                                // _GET_RAND
        {3, 1, 100},                                //   100
        {2, 0, 1},                                  //   into var0
        {21, 3, 0},                                 //
        {1, 0, 1},                                  // var0
        {3, 1, static_cast<u32>(kSplicedChance)},   //
        {14, 0x2A, 0},                              // <
        {17, 0, 0},                                 // false: death (target set below)
    };
    const std::size_t to_death = cells.size() - 1;

    cells.push_back({3, 1, 101}); // _STATUS_SET_MUTEKI(120): guards the knockdown briefly
    cells.push_back({3, 1, 120});
    cells.push_back({21, 2, 0});
    cells.insert(cells.end(), motion->begin(), motion->end());
    cells.insert(cells.end(), wait->begin(), wait->end());

    // Lies still for rand(40) + 30 steps.
    cells.push_back({3, 1, 180});
    cells.push_back({3, 1, 40});
    cells.push_back({2, 0, 1});
    cells.push_back({21, 3, 0});
    cells.push_back({2, 0, 1});
    cells.push_back({1, 0, 1});
    cells.push_back({3, 1, 30});
    cells.push_back({6, 0, 0});
    cells.push_back({5, 0, 0});
    cells.push_back({4, 0, 0});
    const std::size_t loop_top = cells.size();
    cells.push_back({22, 0, 0});
    cells.push_back({1, 0, 1});
    cells.push_back({3, 1, 0});
    cells.push_back({14, 0x2C, 0});
    cells.push_back({17, at(loop_top + 13), 0});
    cells.push_back({2, 0, 1});
    cells.push_back({1, 0, 1});
    cells.push_back({3, 1, 1});
    cells.push_back({7, 0, 0});
    cells.push_back({5, 0, 0});
    cells.push_back({4, 0, 0});
    cells.push_back({23, 0, 0});
    cells.push_back({16, at(loop_top), 0});
    cells.push_back({22, 1, 0});

    cells.push_back({3, 1, 228}); // _STATUS_SET_LIFE
    cells.push_back({3, 2, FloatBits(kReviveLife)});
    cells.push_back({21, 2, 0});
    cells.push_back({3, 1, 200}); // _SET_MOTION(get-up)
    cells.push_back({3, 1, static_cast<u32>(kGetUpMotion)});
    cells.push_back({21, 2, 0});
    cells.insert(cells.end(), wait_get_up.begin(), wait_get_up.end());
    cells.push_back({3, 1, 200}); // _SET_MOTION(idle)
    cells.push_back({3, 1, 0});
    cells.push_back({21, 2, 0});
    cells.push_back({3, 1, 0});   // return value
    cells.push_back({15, 0, 0});  // RET: the monster's own AI resumes

    const std::size_t death = cells.size();
    cells.push_back(file.CellAt(patch)); // the push of 101 the jump displaced
    cells.push_back({16, static_cast<u32>(patch + 12 - code_base), 0});
    cells[to_death] = {17, at(death), 0};

    if (cells.size() * 12 > static_cast<std::size_t>(kStubBytes)) {
        std::println("[HarderAI] {}: revive code too large ({} cells)", code, cells.size());
        return;
    }

    std::memcpy(stub, cells.data(), cells.size() * sizeof(Cell));

    // The roll needs a local; the label has none.
    if (static_cast<int>(file(label_off + 8)) < 1) {
        const u32 locals = 1;
        std::memcpy(script + label_off + 8, &locals, 4);
    }

    const Cell jump = {16, static_cast<u32>(stub_off), 0};
    std::memcpy(script + patch, jump.data(), sizeof(Cell));
    std::println("[HarderAI] {}: revive roll spliced ({} cells, {}%)", code, cells.size(), kSplicedChance);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Minibosses
// ---------------------------------------------------------------------------------------------

void MinibossesPlaced(CMonstorUnit *unit) {
    if (unit == nullptr || UserStatus == nullptr || selectMapNo < 0 || selectMapNo > 6) {
        return;
    }

    const int dungeon = selectMapNo;
    const int floor = UserStatus->cur_floor;
    const int back = unit->back_dungeon != 0 ? 1 : 0;

    if (dungeon != g_dungeon || floor != g_floor) {
        g_saved = {};
    } else if (g_back != -1 && back != g_back) {
        // Crossing to the other side keeps what is left of this side's minibosses.
        std::vector<Miniboss> left;

        for (const Miniboss &boss : g_minibosses) {
            if (boss.alive) {
                left.push_back(boss);
            }
        }

        g_saved[g_back] = left;
    }

    g_dungeon = dungeon;
    g_floor = floor;
    g_back = back;
    g_minibosses.clear();
    g_boosts.clear();

    if (g_saved[back]) {
        RestoreMinibosses(unit, dungeon, *g_saved[back]);
    } else {
        RollMinibosses(unit, dungeon);
    }
}

bool IsMiniboss(int monster_no) {
    return FindMiniboss(monster_no) != nullptr;
}

float MinibossBodyRadius(int monster_no, float radius) {
    return IsMiniboss(monster_no) && radius > 0.0f ? radius * kScale : radius;
}

float MinibossAttackRadius(int monster_no, float radius) {
    return IsMiniboss(monster_no) && radius > 0.0f && radius < 200.0f ? radius * kWeaponSphere : radius;
}

int MinibossMelee(int monster_no, int damage) {
    return IsMiniboss(monster_no) ? ScaleRound(damage, kAttack) : damage;
}

int MinibossShot(int monster_no, int damage) {
    return IsMiniboss(monster_no) ? ScaleRound(damage, kAttack) : damage;
}

float MinibossDistance(int monster_no, float distance) {
    const Miniboss *boss = FindMiniboss(monster_no);

    if (boss == nullptr) {
        return distance;
    }

    return distance / (Contains(kWideReach, boss->name_no) ? kReachWide : kReach);
}

float MinibossMotionSpeed(int monster_no, int motion_id, float speed) {
    const Miniboss *boss = FindMiniboss(monster_no);

    if (boss == nullptr || motion_id != 1 || Contains(kNoWalkSync, boss->name_no)) {
        return speed;
    }

    return speed / kWalkSync;
}

void MinibossStep() {
    if (NowMonstorUnit == nullptr || SaveData == nullptr) {
        return;
    }

    // Dark Cloud Enhanced (Dungeon.cs CheckMiniBossStamina): a miniboss stays in the stamina
    // state while it lives, dealing twice the damage and taking half, topped up before it runs out.
    constexpr int kAngerLeast = 60;
    constexpr int kAngerTopUp = 60000;

    for (Miniboss &boss : g_minibosses) {
        MONSTOR &monster = NowMonstorUnit->monster[boss.slot];
        boss.alive = monster.state >= 1 && monster.hp > 0;

        if (boss.alive && monster.anger_timer < kAngerLeast) {
            monster.anger_timer = kAngerTopUp;
        }
    }

    CDngStatusData *status = SaveData->GetDngStatus();

    for (auto boost = g_boosts.begin(); boost != g_boosts.end();) {
        auto *weapons = &status->chara_weapons[0][0];

        if (!boost->fallen) {
            // Wait for the miniboss to fall, then note what the bag already holds.
            if (NowMonstorUnit->monster[boost->slot].hp > 0) {
                if (++boost->steps > kBoostTimeout) {
                    boost = g_boosts.erase(boost);
                    continue;
                }

                ++boost;
                continue;
            }

            for (std::size_t i = 0; i < boost->before.size(); i++) {
                boost->before[i] = weapons[i].item_no;
            }

            boost->fallen = true;
            ++boost;
            continue;
        }

        bool applied = false;

        for (std::size_t i = 0; i < boost->before.size(); i++) {
            if (weapons[i].item_no == boost->boost.weapon && boost->before[i] != boost->boost.weapon) {
                ApplyBoost(weapons[i], boost->boost);
                applied = true;
                break;
            }
        }

        boost = applied ? g_boosts.erase(boost) : boost + 1;
    }
}

// ---------------------------------------------------------------------------------------------
// Ungaga's doors (Dungeon/UngagaDoors.cs)
// ---------------------------------------------------------------------------------------------

void PatchUngagaDoors(int dungeon, char *script, int size) {
    // Each door block holds two 150.0 trigger distances 0x2B8 apart, as Expanded found them in the
    // Sun and Moon Temple, Moon Sea and Gallery of Time scripts; both go to 50 and three bytes
    // around them to 30.
    static constexpr std::array<int, 3> kExpected = {0x189B0, 0x1FD48, 0x147EC};

    if (dungeon < 3 || dungeon > 5 || script == nullptr) {
        return;
    }

    auto read_float = [&](int offset) {
        float value = 0.0f;

        if (offset >= 0 && offset + 4 <= size) {
            std::memcpy(&value, script + offset, 4);
        }

        return value;
    };

    auto is_door = [&](int at) { return at >= 0x90 && at + 0x4A4 < size && read_float(at) == 150.0f && read_float(at + 0x2B8) == 150.0f; };

    int at = kExpected[dungeon - 3];

    if (!is_door(at)) {
        at = -1;

        for (int offset = 0x90; offset + 0x4A4 < size; offset += 4) {
            if (is_door(offset)) {
                at = offset;
                break;
            }
        }
    }

    if (at < 0) {
        std::println("[UngagaDoors] d0{}: no door distances found", dungeon + 1);
        return;
    }

    const float near = 50.0f;
    script[at - 0x90] = 30;
    std::memcpy(script + at, &near, 4);
    std::memcpy(script + at + 0x2B8, &near, 4);
    script[at + 0x4A4] = 30;
    script[at + 0x474] = 30;
    std::println("[UngagaDoors] d0{}: doors fixed at 0x{:X}", dungeon + 1, at);
}

// ---------------------------------------------------------------------------------------------
// Ungaga's throw speed (Dark Cloud Enhanced's pnach "change throw speed"; Expanded's
// Dungeon/UngagaDoors.cs CheckSwap does the same)
// ---------------------------------------------------------------------------------------------

void UngagaThrowSpeed(int chara, CCharacter &model) {
    // Both mods turn the "0.22" of the throw's KEY line in c10b.chr's config into "0.44". That
    // line is motion 27, frames 186 to 206 (Expanded's docs/character-motion-table.md).
    static constexpr int   kThrowMotion = 27;
    static constexpr int   kThrowStart = 186;
    static constexpr int   kThrowEnd = 206;
    static constexpr float kVanillaSpeed = 0.22f;
    static constexpr float kFastSpeed = 0.44f;

    if (chara != CHARA_UNGAGA || model.motion_type.motion_info == nullptr) {
        return;
    }

    MOTION_INFO &info = model.motion_type.motion_info[kThrowMotion];

    if (info.start != kThrowStart || info.end != kThrowEnd || std::fabs(info.speed - kVanillaSpeed) > 0.001f) {
        std::println("[UngagaThrow] motion {} is {}-{} at {}, left alone", kThrowMotion, info.start, info.end, info.speed);
        return;
    }

    info.speed = kFastSpeed;
    std::println("[UngagaThrow] throw speed {} -> {}", kVanillaSpeed, kFastSpeed);
}

// ---------------------------------------------------------------------------------------------
// Harder enemy AI
// ---------------------------------------------------------------------------------------------

void HarderAiScript(const char *model_code, char *script, int size, CDataAlloc2<1> *alloc) {
    if (!platform::ConfigGet().harder_enemy_ai || model_code == nullptr || script == nullptr || QuietModelLoad()) {
        return;
    }

    // Expanded names the scripts by four characters: "e131a" is its "e131".
    std::string code(model_code);

    if (code.size() == 5 && code[4] == 'a') {
        code.resize(4);
    }

    for (const NativeReviver &reviver : kNativeRevivers) {
        if (reviver.code == code) {
            BuffNativeReviver(reviver, script, size);
            return;
        }
    }

    if (std::ranges::find(kGetUpCodes, std::string_view(code)) != kGetUpCodes.end()) {
        SpliceRevive(code, script, size, alloc);
    }
}

} // namespace dc::game::expanded

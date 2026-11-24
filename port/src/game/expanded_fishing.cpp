#include "expanded_fishing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <print>
#include <utility>
#include <vector>

#include "editloop3.hpp"
#include "expanded_weapons.hpp"
#include "fish.hpp"
#include "fishing.hpp"
#include "itemdata.hpp"
#include "mathutil.hpp"
#include "savedata.hpp"

namespace dc::game::expanded {

namespace {

// ---------------------------------------------------------------------------------------------
// The Mardan swords (Fishing.cs CheckMardanSword, ActivateMardanSwordAbilities)
// ---------------------------------------------------------------------------------------------

constexpr int kMardanGarayan = 5;
constexpr int kUmadakara = 9;
constexpr int kBaronGarayan = 17;

constexpr int kSpotWaterfall = 2;
constexpr int kSpotEastHarbor = 3;

constexpr int kStepsPerSecond = 60;
constexpr int kFishSlots = 6;

int g_spot = -1;
int g_mardan = 0; // the best Mardan sword owned: Arise Mardan over Mardan Twei over Mardan Eins

std::array<int, kFishSlots> g_passive_wait{};
std::array<int, kFishSlots> g_mardan_wait{};

bool IsGarayan(int kind) {
    return kind == kMardanGarayan || kind == kBaronGarayan;
}

float FpMultiplier() {
    switch (g_mardan) {
        case ITEM_WEAPON_ARISE_MARDAN:
            return 2.0f;
        case ITEM_WEAPON_MARDAN_TWEI:
            return 1.5f;
        case ITEM_WEAPON_MARDAN_EINS:
            return 1.2f;
        default:
            return 1.0f;
    }
}

float RandUnit() {
    return static_cast<float>(std::rand()) / 2147483648.0f;
}

// The same chance the spot's own rare fish have at this time of day: 2% by day, 5% in the evening,
// 3% at night and 4% in the morning.
int RareChance() {
    if (SaveData == nullptr) {
        return 2;
    }

    switch (EdGetTime(SaveData->GetNowTime())) {
        case ED_TIME_EVENING:
            return 5;
        case ED_TIME_NIGHT:
            return 3;
        case ED_TIME_MORNING:
            return 4;
        default:
            return 2;
    }
}

// ---------------------------------------------------------------------------------------------
// Arise Mardan's sizes (Fishing.cs AriseTransform). The size CFish::SetScale gives has already been
// smoothed into the kind's largest (fish.cpp), which is the first step of Expanded's transform.
// ---------------------------------------------------------------------------------------------

constexpr float kAriseFactor = 2.0f;
constexpr float kAriseSmooth = 0.72f;
constexpr float kAriseExponent = 2.0f;

// From 1 times at min to factor times at max.
float ScaleCore(float size, float min, float max, float factor) {
    if (factor <= 1.0f || max <= min) {
        return size;
    }

    const float t = (size - min) / (max - min);
    return size * (1.0f + (factor - 1.0f) * t);
}

// Lifts sizes in [base, max], none at either end, most in the upper range.
float SmoothCore(float size, float base, float max, float strength, float exponent) {
    if (strength <= 0.0f || max <= base || size <= base) {
        return size;
    }

    const float t = std::min(1.0f, (size - base) / (max - base));
    return size + strength * (max - base) * std::pow(t, exponent) * (1.0f - t);
}

float AriseSize(float size, float base, float max) {
    const float min = 0.5f * base;
    const float scaled = ScaleCore(std::min(size, max), min, max, kAriseFactor);
    const float scaled_base = ScaleCore(base, min, max, kAriseFactor);
    return SmoothCore(scaled, scaled_base, kAriseFactor * max, kAriseSmooth, kAriseExponent);
}

// ---------------------------------------------------------------------------------------------
// Fishing records and Arise Mardan's Max Magic (Fishing.cs UpdateFishRecordsAndAriseBonus)
// ---------------------------------------------------------------------------------------------

constexpr double kCurveGamma = 0.954;
constexpr double kCurveCoeff = 116.0;
constexpr int    kGarayanCap = 117;
constexpr int    kNormalCap = 43;
constexpr int    kMagicMost = 999;
constexpr int    kFishKinds = 18;

int g_arise_base = -1; // Arise Mardan's Max Magic before any record counts, read once from the table

// +1 at the kind's largest size, the kind's cap at twice it, on Expanded's curve in between.
int RecordBonus(int kind, float record) {
    const FishInfo *info = GetFishInfo(kind);

    if (info == nullptr) {
        return 0;
    }

    const int largest = static_cast<int>(std::floor(info->max_size * 10.0f));

    if (largest <= 0) {
        return 0;
    }

    const int cap = IsGarayan(kind) ? kGarayanCap : kNormalCap;
    const int top = largest * 2;
    const int cm = static_cast<int>(std::floor(record));

    if (cm < largest) {
        return 0;
    }

    if (cm >= top) {
        return cap;
    }

    const double d = static_cast<double>(top - cm) / static_cast<double>(largest);
    const double share = 1.0 / (1.0 + kCurveCoeff * std::pow(d, kCurveGamma));
    return std::max(1, static_cast<int>(std::lround(cap * share)));
}

} // namespace

void FishingSessionStart(int spot) {
    g_spot = spot;
    g_mardan = 0;
    g_passive_wait.fill(0);
    g_mardan_wait.fill(0);

    for (int sword : {ITEM_WEAPON_ARISE_MARDAN, ITEM_WEAPON_MARDAN_TWEI, ITEM_WEAPON_MARDAN_EINS}) {
        if (OwnedCount(sword) > 0) {
            g_mardan = sword;
            break;
        }
    }

    if (g_mardan != 0) {
        std::println("[Mardan] spot {}: sword {} owned, Fishing Points x{}", spot, g_mardan, FpMultiplier());
    }

    FishRecordsUpdate();
}

int FishKind(int kind) {
    if (g_mardan != ITEM_WEAPON_MARDAN_TWEI && g_mardan != ITEM_WEAPON_ARISE_MARDAN) {
        return kind;
    }

    if (kind < 0 || IsGarayan(kind)) {
        return kind;
    }

    if (static_cast<int>(100.0f * RandUnit()) >= RareChance()) {
        return kind;
    }

    // One in five is a Baron Garayan, as the spots' own rare fish are.
    const int rolled = RandUnit() < 0.2f ? kBaronGarayan : kMardanGarayan;
    std::println("[Mardan] fish kind {} rerolled to {}", kind, rolled);
    return rolled;
}

void FishLoaded(CFish *fish) {
    if (fish == nullptr || fish->fish_kind < 0 || g_mardan == 0) {
        return;
    }

    if (!IsGarayan(fish->fish_kind)) {
        const float multiplier = FpMultiplier();
        fish->info.min_fp = static_cast<s32>(static_cast<float>(fish->info.min_fp) * multiplier);
        fish->info.max_fp = static_cast<s32>(static_cast<float>(fish->info.max_fp) * multiplier);
    }

    if (g_mardan != ITEM_WEAPON_ARISE_MARDAN || fish->size <= 0.0f) {
        return;
    }

    const float before = fish->size;
    const float after = AriseSize(before, fish->info.min_size, fish->info.max_size);
    const float ratio = after / before;
    fish->size = after;
    fish->angle_model_scale *= ratio;
    fish->model_scale *= ratio;
    std::println("[Mardan] Arise size {} cm -> {} cm", static_cast<int>(before * 10.0f), static_cast<int>(after * 10.0f));
}

void FishingSteer(const float *angler) {
    if (Fish == nullptr || angler == nullptr || AngleFish != nullptr) {
        return;
    }

    const bool passive = g_spot == kSpotWaterfall || g_spot == kSpotEastHarbor;

    if (!passive && g_mardan == 0) {
        return;
    }

    // Mardan Eins every 4 seconds, Mardan Twei every 3, Arise Mardan every 2, each divided by how
    // much the fish likes the bait on the hook.
    float base_seconds = 4.0f;

    if (g_mardan == ITEM_WEAPON_ARISE_MARDAN) {
        base_seconds = 2.0f;
    } else if (g_mardan == ITEM_WEAPON_MARDAN_TWEI) {
        base_seconds = 3.0f;
    }

    const bool baited = esa_type >= 0 && esa_type < FISH_FOOD_KIND_COUNT && EsaFrame != nullptr;

    for (int i = 0; i < std::min(FishNum, kFishSlots); i++) {
        CFish &fish = Fish[i];

        if (fish.fish_kind < 0 || &fish == BattleFish) {
            continue;
        }

        bool turn = false;

        if (passive && --g_passive_wait[i] <= 0) {
            g_passive_wait[i] = 10 * kStepsPerSecond;
            turn = true;
        }

        const bool drawn = fish.fish_kind == kMardanGarayan || fish.fish_kind == kBaronGarayan || fish.fish_kind == kUmadakara;

        if (g_mardan != 0 && drawn && baited) {
            const float affinity = fish.info.food_affinity[esa_type];

            if (affinity > 0.0f && --g_mardan_wait[i] <= 0) {
                g_mardan_wait[i] = std::max(1, static_cast<int>(base_seconds / affinity * kStepsPerSecond));
                turn = true;
            }
        }

        if (!turn) {
            continue;
        }

        float position[4];
        fish.GetPosition(position);
        const float dx = angler[0] - position[0];
        const float dz = angler[2] - position[2];

        if (dx != 0.0f || dz != 0.0f) {
            fish.target_yaw = AngleLimit(std::atan2(dx, dz));
        }
    }
}

void FishRecordsUpdate() {
    if (SaveData == nullptr) {
        return;
    }

    auto ranks = SaveData->FishingRanks();

    // Each species' largest catch, as the records screen shows them, largest first.
    std::array<int, kFishKinds> best;
    best.fill(-1);

    for (int i = 0; i < static_cast<int>(ranks.size()); i++) {
        const SV_FISH_DATA &entry = ranks[i];

        if (entry.rank <= 0.0f || entry.fish_id < 0 || entry.fish_id >= kFishKinds) {
            continue;
        }

        if (best[entry.fish_id] < 0 || entry.rank > ranks[best[entry.fish_id]].rank) {
            best[entry.fish_id] = i;
        }
    }

    std::vector<SV_FISH_DATA> kept;

    for (int index : best) {
        if (index >= 0) {
            kept.push_back(ranks[index]);
        }
    }

    std::ranges::stable_sort(kept, [](const SV_FISH_DATA &a, const SV_FISH_DATA &b) { return a.rank > b.rank; });

    bool changed = false;

    for (int i = 0; i < static_cast<int>(ranks.size()); i++) {
        SV_FISH_DATA entry{};
        entry.fish_id = -1;

        if (i < static_cast<int>(kept.size())) {
            entry = kept[i];
        }

        SV_FISH_DATA &slot = ranks[i];

        if (slot.fish_id != entry.fish_id || slot.rank != entry.rank || slot.unk_8 != entry.unk_8 || slot.unk_C != entry.unk_C) {
            slot = entry;
            changed = true;
        }
    }

    if (changed) {
        std::println("[FishRecords] records kept to {} species", kept.size());
    }

    WEAPON_DATA *arise = GetWeaponData(ITEM_WEAPON_ARISE_MARDAN);

    if (arise == nullptr) {
        return;
    }

    if (g_arise_base < 0) {
        g_arise_base = arise->magic_max;
    }

    int bonus = 0;

    for (const SV_FISH_DATA &entry : kept) {
        bonus += RecordBonus(entry.fish_id, entry.rank);
    }

    const int magic = std::min(kMagicMost, g_arise_base + bonus);

    if (arise->magic_max != magic) {
        std::println("[AriseMagic] records give +{}: Arise Mardan Max Magic {} -> {}", bonus, arise->magic_max, magic);
        arise->magic_max = static_cast<s16>(magic);
    }
}

} // namespace dc::game::expanded

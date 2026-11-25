#include "quest/custom_shop.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "clsmes.hpp"
#include "quest/enhanced.hpp"
#include "data/data_read.hpp"
#include "itemdata.hpp"
#include "menuetc.hpp"
#include "savedata.hpp"
#include "shop.hpp"
#include "text/gametext.hpp"

namespace dc::quest {

using namespace dc::game;
using namespace dc::data;
using namespace dc::text;

namespace {

constexpr int kRetailShops = 18;
constexpr int kShopGoods = 20;
constexpr int kPricedItems = 296;

// The screen reads its shopkeeper into its half of the menu buffer (EdMenuBuffer, halved: 1.8 MB)
// after itemshop.pak (646 KB). Retail's largest town model is 641 KB, its largest shopkeeper 453 KB.
constexpr std::uintmax_t kLargestKeeper = std::uintmax_t{1024} * 1024;
// Sizes of the buffers ShopPersonReadStart and ShopPersonBuild pass to ItemShopGetPacFileName and
// ItemShopGetImgFileName.
constexpr std::size_t kPacNameSize = 76;
constexpr std::size_t kImageNameSize = 64;

// GetNowMasterMsgNo2's item shop lines: 4000 + shop * 100, plus an offset for the moment.
constexpr int kLinesBase = 4000;

// ItemShopList2: what each retail item shop sells, ended by -1 where short.
// clang-format off
std::array<std::array<s16, kShopGoods>, kRetailShops> kRetailGoods = {{
    // Dark Cloud Expanded: Gaffer's shop no longer sells the Endurance attachment.
    {ITEM_BREAD, ITEM_REGULAR_WATER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_ESCAPE_POWDER, ITEM_STAND_IN_POWDER, ITEM_REVIVAL_POWDER, ITEM_ANTIDOTE_DRINK, ITEM_SOAP, ITEM_BOMB, ITEM_DRAN_S_FEATHER, ITEM_ATTACH_ATTACK, ITEM_ATTACH_FIRE, ITEM_ATTACH_ICE, ITEM_ATTACH_THUNDER, ITEM_ATTACH_WIND, ITEM_ATTACH_HOLY, ITEM_ATTACH_DINOSLAYER, ITEM_GOLD_BULLION, -1},
    {ITEM_BREAD, ITEM_CHEESE, ITEM_TASTY_WATER, ITEM_REVIVAL_POWDER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_STAND_IN_POWDER, ITEM_ESCAPE_POWDER, ITEM_ATTACH_PLANT_BUSTER, ITEM_ANTIDOTE_DRINK, ITEM_SOAP, ITEM_DRAN_S_FEATHER, ITEM_GOLD_BULLION, ITEM_WEAPON_WISE_OWL_SWORD, ITEM_MIMI, ITEM_PRICKLY, -1},
    {ITEM_FLAPPING_FISH, ITEM_FLAPPING_FISH, ITEM_FLAPPING_FISH, ITEM_EVY, ITEM_EVY, ITEM_PETITE_FISH, ITEM_PETITE_FISH, ITEM_ICE_BLOCK, ITEM_ICE_BLOCK, ITEM_ICE_BLOCK, ITEM_REGULAR_WATER, ITEM_TASTY_WATER, ITEM_PREMIUM_WATER, ITEM_STAND_IN_POWDER, -1},
    {ITEM_REGULAR_WATER, ITEM_TASTY_WATER, ITEM_PREMIUM_WATER, ITEM_STAMINA_DRINK, ITEM_ANTIDOTE_DRINK, ITEM_HOLY_WATER, -1},
    {ITEM_REGULAR_WATER, ITEM_TASTY_WATER, ITEM_PREMIUM_WATER, ITEM_BREAD, ITEM_STAMINA_DRINK, ITEM_ATTACH_ATTACK, ITEM_ATTACH_ENDURANCE, ITEM_ATTACH_SPEED, ITEM_ATTACH_MAGICAL_POWER, -1},
    {ITEM_REGULAR_WATER, ITEM_TASTY_WATER, ITEM_PREMIUM_WATER, ITEM_STAMINA_DRINK, ITEM_CHEESE, ITEM_ATTACH_FIRE, ITEM_ATTACH_ICE, ITEM_ATTACH_THUNDER, ITEM_ATTACH_WIND, ITEM_ATTACH_HOLY, -1},
    {ITEM_REGULAR_WATER, ITEM_TASTY_WATER, ITEM_PREMIUM_WATER, ITEM_STAMINA_DRINK, ITEM_PREMIUM_CHICKEN, ITEM_BOMB, ITEM_ATTACH_SEA_KILLER, ITEM_ATTACH_MAGE_SLAYER, ITEM_ATTACH_UNDEAD_BUSTER, -1},
    // Dark Cloud Expanded: the Queens fruit stand also sells the Fruit of Eden.
    {ITEM_THROBBING_CHERRY, ITEM_GOOEY_PEACH, ITEM_BOMB_NUTS, ITEM_POISONOUS_APPLE, ITEM_MELLOW_BANANA, ITEM_REPAIR_POWDER, ITEM_REVIVAL_POWDER, ITEM_STAND_IN_POWDER, ITEM_FRUIT_OF_EDEN, -1},
    {ITEM_WEAPON_KITCHEN_KNIFE, ITEM_WEAPON_GLADIUS, ITEM_WEAPON_BONE_SLINGSHOT, ITEM_WEAPON_BIG_BUCKS_HAMMER, ITEM_WEAPON_PLATINUM_RING, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_STAND_IN_POWDER, ITEM_REVIVAL_POWDER, ITEM_ESCAPE_POWDER, ITEM_DRAN_S_FEATHER, ITEM_GOLD_BULLION, -1},
    {ITEM_PREMIUM_WATER, ITEM_CHEESE, ITEM_PREMIUM_CHICKEN, ITEM_MIGHTY_HEALING, ITEM_BOMB, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_STAND_IN_POWDER, ITEM_REVIVAL_POWDER, ITEM_ESCAPE_POWDER, ITEM_ATTACH_WIND, ITEM_ATTACH_STONE_BREAKER, ITEM_ATTACH_SKY_HUNTER, ITEM_DRAN_S_FEATHER, ITEM_GOLD_BULLION, ITEM_POTATO_CAKE, -1},
    {ITEM_REGULAR_WATER, ITEM_TASTY_WATER, ITEM_PREMIUM_WATER, ITEM_PREMIUM_CHICKEN, ITEM_BOMB, ITEM_ATTACH_BEAST_BUSTER, ITEM_STAND_IN_POWDER, ITEM_REVIVAL_POWDER, ITEM_ESCAPE_POWDER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_DRAN_S_FEATHER, ITEM_WEAPON_JACKAL, ITEM_WEAPON_SNAIL, ITEM_GOLD_BULLION, -1},
    {ITEM_PREMIUM_WATER, ITEM_PREMIUM_CHICKEN, ITEM_MIGHTY_HEALING, ITEM_STAND_IN_POWDER, ITEM_REVIVAL_POWDER, ITEM_ESCAPE_POWDER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_DRAN_S_FEATHER, ITEM_GOLD_BULLION, -1},
    {ITEM_FIRE_GEM, ITEM_ICE_GEM, ITEM_THUNDER_GEM, ITEM_WIND_GEM, ITEM_HOLY_GEM, ITEM_ATTACH_FIRE, ITEM_ATTACH_ICE, ITEM_ATTACH_THUNDER, ITEM_ATTACH_WIND, ITEM_ATTACH_HOLY, ITEM_ATTACH_SEA_KILLER, ITEM_ATTACH_MAGE_SLAYER, ITEM_GOLD_BULLION, -1},
    {ITEM_BREAD, ITEM_REGULAR_WATER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_REVIVAL_POWDER, ITEM_STAND_IN_POWDER, ITEM_ESCAPE_POWDER, ITEM_ANTIDOTE_DRINK, ITEM_GOLD_BULLION, -1},
    {ITEM_BREAD, ITEM_CHEESE, ITEM_TASTY_WATER, ITEM_REVIVAL_POWDER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_STAND_IN_POWDER, ITEM_ESCAPE_POWDER, ITEM_ATTACH_PLANT_BUSTER, ITEM_ANTIDOTE_DRINK, ITEM_SOAP, ITEM_DRAN_S_FEATHER, ITEM_GOLD_BULLION, -1},
    {ITEM_PREMIUM_WATER, ITEM_CHEESE, ITEM_HOLY_WATER, ITEM_SOAP, ITEM_MIGHTY_HEALING, ITEM_BOMB, ITEM_STAND_IN_POWDER, ITEM_REVIVAL_POWDER, ITEM_ESCAPE_POWDER, ITEM_REPAIR_POWDER, ITEM_AUTO_REPAIR_POWDER, ITEM_DRAN_S_FEATHER, ITEM_GOLD_BULLION, -1},
    {ITEM_ATTACH_GARNET, ITEM_ATTACH_AMETHYST, ITEM_ATTACH_AQUAMARINE, ITEM_ATTACH_DIAMOND, ITEM_ATTACH_EMERALD, ITEM_ATTACH_PEARL, ITEM_ATTACH_RUBY, ITEM_ATTACH_PERIDOT, ITEM_ATTACH_SAPPHIRE, ITEM_ATTACH_OPAL, ITEM_ATTACH_TOPAZ, ITEM_ATTACH_TURQUOISE, -1},
    // Dark Cloud Expanded: the Fairy King also sells Metal Breaker and Mimic Breaker.
    {ITEM_ATTACH_ATTACK, ITEM_ATTACH_ENDURANCE, ITEM_ATTACH_SPEED, ITEM_ATTACH_MAGICAL_POWER, ITEM_ATTACH_DINOSLAYER, ITEM_ATTACH_UNDEAD_BUSTER, ITEM_ATTACH_SEA_KILLER, ITEM_ATTACH_STONE_BREAKER, ITEM_ATTACH_PLANT_BUSTER, ITEM_ATTACH_BEAST_BUSTER, ITEM_ATTACH_SKY_HUNTER, ITEM_ATTACH_MAGE_SLAYER, ITEM_ATTACH_METALBREAKER, ITEM_ATTACH_MIMIC_BREAKER, -1},
}};

// ItemShopGetPacFileName's and ItemShopGetImgFileName's shopkeepers, by shop kind and number.
constexpr std::array<std::array<const char *, kRetailShops>, 2> kRetailKeeperPacs = {{
    {"p13", "p31", "p39", "p54", "p74", "c03", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"p02", "p32", "p35", "p36", "p36", "p36", "p36", "p37", "p49", "p55", "p75", "c03", "p41", "p02", "p32", "p38", "c03", "c03"},
}};

constexpr std::array<std::array<const char *, kRetailShops>, 2> kRetailKeeperImages = {{
    {"p13a", "p31a", "p39a", "p54a", "p74a", "c03c", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"p02a", "p32a", "p35a", "p36a", "p36a", "p36a", "p36a", "p37a", "p49a", "p55a", "p75a", "c03c", "p41a", "p02a", "p32a", "p38a", "c03c", "c03c"},
}};
// clang-format on

struct Open {
    CustomShop                       shop;
    std::array<s16, kShopGoods>      goods{};
    std::vector<std::pair<int, s16>> prices; // Retail's buying prices laid over, to put back.
    std::string                      keeper_image;
    GameTextFile                     lines;
};

std::optional<Open> g_open;

std::vector<int> LineNumbers(CustomShopLine line, int look) {
    const int base = kLinesBase + look * 100;
    switch (line) {
        case CustomShopLine::Greeting:
            return {base};
        case CustomShopLine::Browsing:
            return {base + 0x14};
        case CustomShopLine::Selling:
            return {base + 0x1E};
        case CustomShopLine::NotEnoughMoney:
            return {base + 0x34};
        case CustomShopLine::NoRoom:
            return {base + 0x37};
        case CustomShopLine::Confirm:
            return {base + 70, base + 71, base + 72};
        case CustomShopLine::Thanks:
            return {base + 0x4B};
        case CustomShopLine::Goodbye:
            return {base + 0x5B};
    }
    return {};
}

// "gedit/e01/chara/p13a.chr" draws with "p13a01.img", as every retail town model and shopkeeper does.
std::string KeeperImage(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    const std::size_t start = slash == std::string_view::npos ? 0 : slash + 1;
    const std::size_t dot = path.find_last_of('.');
    const std::size_t end = dot == std::string_view::npos || dot < start ? path.size() : dot;
    return std::format("{}01.img", path.substr(start, end - start));
}

// Writes name and its terminator into a game buffer the caller has sized for it.
void CopyName(std::string_view name, char *buffer) {
    *std::ranges::copy(name, buffer).out = '\0';
}

int RetailKind(int shop_kind) {
    return shop_kind >= 0 && shop_kind < 2 ? shop_kind : 0;
}

int RetailShop(int shop_no) {
    return shop_no >= 0 && shop_no < kRetailShops ? shop_no : 0;
}

} // namespace

int CustomShopBegin(CustomShop shop) {
    CustomShopEnd();
    if (shop.look < 0 || shop.look >= kRetailShops) {
        shop.look = 0;
    }
    Open &open = g_open.emplace();

    // The goods take the list's first places; the rest end it.
    open.goods.fill(-1);
    int count = 0;
    for (const auto &[item, price] : shop.goods) {
        const int index = item - ITEM_ATTACH_START;
        if (count >= kShopGoods - 1 || index < 0 || index >= kPricedItems) {
            continue;
        }
        open.prices.emplace_back(item, PriceList[index].buy_price);
        PriceList[index].buy_price = static_cast<s16>(std::clamp(price, 0, 0x7FFF));
        open.goods[count++] = static_cast<s16>(item);
    }

    if (!shop.keeper.empty()) {
        const std::uintmax_t size = DataFileSize(shop.keeper);
        std::string          image = KeeperImage(shop.keeper);
        if (size == 0 || size > kLargestKeeper || shop.keeper.size() >= kPacNameSize || image.size() >= kImageNameSize) {
            std::println(stderr, "custom shop: cannot show {}; shop {}'s shopkeeper stands in", shop.keeper, shop.look);
            shop.keeper.clear();
        } else {
            open.keeper_image = std::move(image);
        }
    }

    bool any_line_set = false;
    for (const auto &[line, text] : shop.lines) {
        for (const int number : LineNumbers(line, shop.look)) {
            any_line_set = open.lines.Set(number, text) >= 0 || any_line_set;
        }
    }
    if (any_line_set) {
        ClsMesSetMessageOverlay(&CommonMenuMes3, open.lines.Data());
    }

    open.shop = std::move(shop);
    return open.shop.look;
}

void CustomShopEnd() {
    if (!g_open) {
        return;
    }
    // Backwards, so an item listed twice ends with the price it had first.
    for (const auto &[item, price] : g_open->prices | std::views::reverse) {
        PriceList[item - ITEM_ATTACH_START].buy_price = price;
    }
    ClsMesSetMessageOverlay(nullptr, nullptr);
    g_open.reset();
}

bool CustomShopActive() {
    return g_open.has_value();
}

namespace {

// Dark Cloud Enhanced's daily shop slot: seven shops sell one extra item that changes each
// in-game day, picked from a five-day rotation of tables. Enhanced rolls it at random and keeps
// it in the save; here it is worked out from the day itself, so it needs no save space and
// stays the same all day. The item goes where the shop's list ended.
using DailyTable = std::vector<s16>;

const DailyTable kGems = {95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106};
const DailyTable kUseful = {132, 133, 134, 135, 150, 154}; // the four amulets, Stamina Drink, Mighty Healing
const DailyTable kDbcWeapons = {259, 260, 262, 270, 290, 301, 304};
const DailyTable kWiseOwlWeapons = {264, 270, 283, 284, 286, 291, 301, 303, 304, 316, 321, 328};
const DailyTable kShipwreckWeapons = {261, 265, 282, 284, 286, 302, 305, 319, 327, 328, 333, 335, 343};
const DailyTable kSunMoonWeapons = {263, 272, 282, 288, 293, 306, 318, 334, 337, 344, 349, 350, 359};
const DailyTable kMoonSeaWeapons = {272, 288, 281, 307, 322, 334, 344, 351, 352, 353};
const DailyTable kGalleryWeapons = {274, 281, 285, 292, 294, 308, 322, 336, 338, 339, 354, 360, 368, 375};

struct DailyShop {
    int                              shop; // index into kRetailGoods
    int                              slot; // where its list ended
    std::array<const DailyTable *, 5> rotation;
};

const DailyTable kGafferDay1 = {181, 181, 224};               // Treasure Chest Key x2, Tram Oil
const DailyTable kGafferDay3 = {197, 199};                    // Mimi, Prickly
const DailyTable kWiseOwlDay1 = {186, 188, 189};              // Carrot, Minon, Battan
const DailyTable kWiseOwlDay4 = {181, 181, 225};              // Treasure Chest Key x2, Sun Dew
const DailyTable kJokerDay2 = {186, 188, 189, 197, 199};      // Carrot, Minon, Battan, Mimi, Prickly
const DailyTable kJokerDay4 = {111, 112, 113, 114, 115, 116, 117, 118, 119}; // the buster and slayer attachments
const DailyTable kJokerDay5 = {181};                          // Treasure Chest Key
const DailyTable kBrookeDay1 = {132, 133, 134, 135, 150};     // the four amulets, Stamina Drink
const DailyTable kBrookeDay3 = {181, 181, 228};               // Treasure Chest Key x2, Secret Path Key
const DailyTable kBrookeDay5 = {169, 186, 188, 189, 193, 197, 199}; // fishing baits
const DailyTable kLedanDay1 = {181, 181, 229};                // Treasure Chest Key x2, Bravery Launch
const DailyTable kLedanDay3 = {118, 119};                     // Metal Breaker, Mimic Breaker
const DailyTable kFairyKingDay2 = {181, 181, 230};            // Treasure Chest Key x2, Flapping Duster
const DailyTable kFairyKingDay3 = {169, 186, 187, 188, 189, 193, 197, 199}; // fishing baits, Potato Cake

constexpr int kFairyKingShop = 11;

const std::array<DailyShop, 7> kDailyShops = {{
    {0, 19, {&kGafferDay1, &kGems, &kGafferDay3, &kUseful, &kDbcWeapons}},                   // Gaffer
    {1, 16, {&kWiseOwlDay1, &kUseful, &kWiseOwlWeapons, &kWiseOwlDay4, &kGems}},            // Wise Owl Forest
    {8, 12, {&kShipwreckWeapons, &kShipwreckWeapons, &kShipwreckWeapons, &kShipwreckWeapons, &kShipwreckWeapons}}, // Jack
    {12, 13, {&kGems, &kJokerDay2, &kUseful, &kJokerDay4, &kJokerDay5}},                    // Joker
    {9, 16, {&kBrookeDay1, &kSunMoonWeapons, &kBrookeDay3, &kGems, &kBrookeDay5}},          // Brooke
    {10, 15, {&kLedanDay1, &kGems, &kLedanDay3, &kUseful, &kMoonSeaWeapons}},               // Ledan
    {11, 10, {&kGalleryWeapons, &kFairyKingDay2, &kFairyKingDay3, &kUseful, &kFairyKingDay2}}, // Fairy King
}};

/**
 * A fixed pick for a day and shop, spread so neighbouring days and shops differ.
 */
int DailyPick(int day, int shop, int size) {
    std::uint32_t h = static_cast<std::uint32_t>(day) * 0x9E3779B1u ^ static_cast<std::uint32_t>(shop + 1) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 13;
    return static_cast<int>(h % static_cast<std::uint32_t>(size));
}

void SetDailyGoods(int shop) {
    if (SaveData == nullptr) {
        return;
    }

    const int day = SaveData->GetDay();

    for (const DailyShop &daily : kDailyShops) {
        if (daily.shop != shop) {
            continue;
        }

        const DailyTable &table = *daily.rotation[((day % 5) + 5) % 5];
        auto             &goods = kRetailGoods[shop];
        goods[daily.slot] = table[DailyPick(day, shop, static_cast<int>(table.size()))];

        int end = daily.slot + 1;

        // Enhanced: once the Mayor's bonus quests are open, the Fairy King also sells the
        // Crystal Eyeball, after his daily item.
        if (shop == kFairyKingShop && EnhancedMayorOpen() && end < kShopGoods) {
            goods[end] = ITEM_CRYSTAL_EYEBALL;
            end++;
        }

        if (end < kShopGoods) {
            goods[end] = -1;
        }
    }
}

} // namespace

s16 *PortGetItemShopList(int shop_no) {
    if (g_open) {
        return g_open->goods.data();
    }
    // ShopNoInput lets numbers up to 20 through; retail read past its eighteen lists for them.
    const int shop = RetailShop(shop_no);
    SetDailyGoods(shop); // Dark Cloud Enhanced: the daily shop slot.
    return kRetailGoods[shop].data();
}

void PortItemShopGetPacFileName(int shop_kind, int shop_no, char *file_name) {
    if (shop_kind == SHOP_KIND_ITEM && g_open && !g_open->shop.keeper.empty()) {
        CopyName(g_open->shop.keeper, file_name);
        return;
    }
    const char *keeper = kRetailKeeperPacs[RetailKind(shop_kind)][RetailShop(shop_no)];
    CopyName(std::format("commenu/shopchara/{}a.pac", keeper), file_name);
}

void PortItemShopGetImgFileName(int shop_kind, int shop_no, char *file_name) {
    if (shop_kind == SHOP_KIND_ITEM && g_open && !g_open->shop.keeper.empty()) {
        CopyName(g_open->keeper_image, file_name);
        return;
    }
    const char *keeper = kRetailKeeperImages[RetailKind(shop_kind)][RetailShop(shop_no)];
    CopyName(std::format("{}01.img", keeper), file_name);
}

} // namespace dc::quest

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "btactstatus.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "expanded_weapons.hpp"
#include "itemdata.hpp"
#include "localize/localize.hpp"
#include "monstorunit.hpp"
#include "quest/enhanced.hpp"
#include "quest/quests.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

// Dark Cloud Enhanced's quests (SideQuestManager.cs, Dialogues.cs, Dungeon.cs, CustomChests.cs), as
// quests of the port's framework: each villager who gives one has a choice in their menu, and the
// quest's state is kept in quests.json instead of Enhanced's bytes at 0x21CE4402 on. Enhanced knows
// its villagers by the two digits of their town model ("p01a" is Macho), and so do these, except in
// Brownboo and Yellow Drops, where Enhanced goes by the villager number as the port does.
//
//   Monster hunts    Macho, Gob, Jake, Chief Bonka: defeat 8 to 18 of a monster in a dungeon reached
//   Fishing quests   Pike, Pao, Sam, Devia: land some of one fish, or one fish of a size
//   Rumours          Laura, Ro, Phil, Zabo, a Brownboo villager: an item on a dungeon's back floors
//   Samba            Moon Sea floor 7's back floor with Toan and his Dagger alone: the Map
//   Yellow Drops     A puzzle of four items in the bag's first slots: the Magical Crystal
//   Pickle           Counts the collection; all of it gives the Flame Key and opens the Mayor's quests
//   Mayor            Clear a Demon Shaft back floor with one ally: a stat item, until all are maxed
//   Mint             Dark Cloud Expanded: catch all 17 kinds of fish: the Saving Book
//
// Dark Cloud Expanded's quest talk menus: while a hunt, a fishing quest, a rumour or the Mayor's
// bonus quests are on offer, the villager's "Talk" starts it and the menu has no line for it;
// while one is under way the menu gains "About the quest." instead.

namespace dc::quest {

using namespace dc::game;

namespace {

constexpr int kStepsPerSecond = 60;

// Enhanced's rnd.Next(lo, hi): lo up to hi - 1.
int Rand(int lo, int hi) {
    if (hi <= lo) {
        return lo;
    }

    return lo + static_cast<int>(static_cast<float>(hi - lo) * static_cast<float>(std::rand()) / 2147483648.0f);
}

CDngStatusData *Status() {
    return SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;
}

// The shared flags: the first monster hunt done, and the whole collection shown to Pickle.
constexpr std::string_view kFlags = "enhanced";

int Flag(std::string_view name) {
    const Progress *progress = FindProgress(kFlags);

    if (progress == nullptr) {
        return 0;
    }

    const auto found = progress->vars.find(std::string(name));
    return found != progress->vars.end() ? found->second : 0;
}

void SetFlag(std::string_view name, int value) {
    if (Progress *progress = EditProgress(kFlags)) {
        progress->vars[std::string(name)] = value;
    }
}

int VarOf(const Progress &progress, std::string_view name) {
    const auto found = progress.vars.find(std::string(name));
    return found != progress.vars.end() ? found->second : 0;
}

Condition Always() {
    return [](const Context &) { return true; };
}

NpcMatch Villager(int map_no, std::string_view model_code) {
    return NpcMatch{.map_no = map_no, .npc_id = kAny, .mes_file_key = {}, .model_code = std::string(model_code)};
}

NpcMatch VillagerNo(int map_no, int npc_id) {
    return NpcMatch{.map_no = map_no, .npc_id = npc_id, .mes_file_key = {}, .model_code = {}};
}

constexpr int kNorune = 0;
constexpr int kMatataki = 1;
constexpr int kQueens = 2;
constexpr int kMuska = 3;
constexpr int kBrownboo = 14;
constexpr int kYellowDrops = 23;

constexpr int kMoonSea = 4;
constexpr int kDemonShaft = 6;

constexpr std::array<std::string_view, 7> kDungeonNames = {"Divine Beast Cave", "Wise Owl Forest", "Shipwreck",
                                                           "Sun & Moon Temple", "Moon Sea", "Gallery of Time",
                                                           "Demon Shaft"};

constexpr Text kNoRoom = {"quest.enhanced.no_room", "Your bag is full. Make some room and come back for your reward."};
constexpr Text kAboutQuest = {"quest.expanded.about", "About the quest."};

// Dark Cloud Expanded's quest talk menus: on offer, "Talk" starts the quest.
Condition OnOffer(std::initializer_list<std::string_view> stages) {
    return AtStages(stages);
}

// Under way: the menu gains "About the quest.".
Condition UnderWay(std::initializer_list<std::string_view> stages) {
    return AtStages(stages);
}

// ── Monster hunts ────────────────────────────────────────────────────────────────────────────

struct Monster {
    std::string_view name;
    int              name_no; /**< MONSTOR::name_no, as Enhanced reads it. */
};

constexpr std::array<std::array<Monster, 4>, 6> kHuntMonsters = {{
    {{{"Master Jackets", 1}, {"Dashers", 6}, {"Mimics", 35}, {"Dragons", 59}}},
    {{{"Fliflis", 8}, {"Earth Diggers", 12}, {"Mimics", 79}, {"Werewolves", 7}}},
    {{{"Gunnys", 23}, {"Gyons", 24}, {"Mimics", 81}, {"Pirate's Chariots", 25}}},
    {{{"Golems", 30}, {"Dunes", 32}, {"Mimics", 37}, {"Blue Dragons", 73}}},
    {{{"Moon Diggers", 66}, {"Space Gyons", 72}, {"Mimics", 39}, {"Crescent Barons", 76}}},
    {{{"Rash Dashers", 63}, {"Jokers", 48}, {"Mimics", 83}, {"Alexanders", 43}}},
}};

struct Hunter {
    std::string_view id;
    std::string_view name;
    int              map_no;
    std::string_view model_code;
    Text             intro;
};

constexpr Text kMachoIntro = {"quest.enhanced.hunt.macho.intro",
                              "Prove me your true strength! I can assign you some Monster Slaying quests.{page}"
                              "The Monster Slayer quests are tasks to kill a random amount of random enemies in any "
                              "dungeon you have unlocked.{page}"
                              "Talk to me again to get your first quest!"};
constexpr Text kGobIntro = {"quest.enhanced.hunt.gob.intro",
                            "I might be a cook, but I'm also a hunter, and I respect those who can fight.{page}"
                            "That being said, I can assign you some Monster Slaying quests."};
constexpr Text kJakeIntro = {"quest.enhanced.hunt.jake.intro",
                             "A message from King: to gain more reputation for the elections, we need to show that we "
                             "can be strong.{page}"
                             "How about you kill some evil monsters and let us take the credit?{page}"
                             "I can assign you Monster Slaying quests."};
constexpr Text kBonkaIntro = {"quest.enhanced.hunt.bonka.intro",
                              "To survive against the fearful enemies in your travels, you can always use some "
                              "training.{page}"
                              "I can assign you some Monster Slaying quests."};

constexpr std::array<Hunter, 4> kHunters = {{
    {"enhanced_hunt_macho", "Macho", kNorune, "01", kMachoIntro},
    {"enhanced_hunt_gob", "Gob", kMatataki, "25", kGobIntro},
    {"enhanced_hunt_jake", "Jake", kQueens, "43", kJakeIntro},
    {"enhanced_hunt_bonka", "Chief Bonka", kMuska, "48", kBonkaIntro},
}};

constexpr Text kHuntLabel = {"quest.enhanced.hunt.label", "Monster Slaying quests"};
constexpr Text kHuntGiven = {"quest.enhanced.hunt.given", "Your quest is to defeat at least %1 %2 in the %3. Good luck!"};
constexpr Text kHuntGoing = {"quest.enhanced.hunt.going",
                             "You're still on the quest to defeat %1 in the %2, just %3 left!"};
constexpr Text kHuntReward = {"quest.enhanced.hunt.reward", "Well done, you completed it! Here's your reward: a Powerup Powder!"};

// Enhanced counts the dungeons reached in order, up to the Gallery of Time.
int HuntDungeons(const IGame &game) {
    int count = 0;

    for (int dungeon = 0; dungeon < 6 && game.DeepestFloor(dungeon) >= 0; dungeon++) {
        count++;
    }

    return std::max(count, 1);
}

bool HuntTaken(int name_no) {
    for (const Hunter &hunter : kHunters) {
        const Progress *progress = FindProgress(hunter.id);

        if (progress != nullptr && progress->stage == "active" && VarOf(*progress, "name_no") == name_no) {
            return true;
        }
    }

    return false;
}

void GiveHunt(Scene &scene) {
    const int dungeons = HuntDungeons(scene.Game());
    int       dungeon = 0;
    int       monster = 0;

    // A monster another hunter already asks for is rolled again, 50 times at most.
    for (int tries = 0; tries <= 50; tries++) {
        dungeon = Rand(0, dungeons);
        monster = Rand(0, 4);

        // The very first hunt is Dashers in the Divine Beast Cave.
        if (dungeon == 0 && Flag("first_hunt_done") == 0) {
            monster = 1;
        }

        if (!HuntTaken(kHuntMonsters[dungeon][monster].name_no)) {
            break;
        }
    }

    const int kills = Rand(8, 19);
    scene.SetVar("dungeon", dungeon);
    scene.SetVar("monster", monster);
    scene.SetVar("name_no", kHuntMonsters[dungeon][monster].name_no);
    scene.SetVar("left", kills);
    scene.SetStage("active");
    scene.Say(kHuntGiven, {std::to_string(kills), std::string(kHuntMonsters[dungeon][monster].name),
                           std::string(kDungeonNames[dungeon])});
}

void TalkHunt(Scene &scene, const Hunter &hunter) {
    if (scene.AtStage("new")) {
        scene.Say(hunter.intro);
        scene.SetStage("idle");
    } else if (scene.AtStage("idle")) {
        GiveHunt(scene);
    } else if (scene.AtStage("active")) {
        const int dungeon = std::clamp(scene.Var("dungeon"), 0, 5);
        const int monster = std::clamp(scene.Var("monster"), 0, 3);
        scene.Say(kHuntGoing, {std::string(kHuntMonsters[dungeon][monster].name), std::string(kDungeonNames[dungeon]),
                               std::to_string(scene.Var("left"))});
    } else if (!scene.Game().GiveItem(ITEM_POWERUP_POWDER)) {
        scene.Say(kNoRoom);
    } else {
        SetFlag("first_hunt_done", 1);
        scene.SetStage("idle");
        scene.Say(kHuntReward);
    }
}

Definition HuntQuest(const Hunter &hunter) {
    const NpcMatch npc = Villager(hunter.map_no, hunter.model_code);
    const SceneFn  talk = [&hunter](Scene &scene) { TalkHunt(scene, hunter); };

    return Builder(std::string(hunter.id))
        .Stages({"new", "idle", "active", "done"})
        .TalkOption(npc, OnOffer({"new", "idle"}), talk)
        .Option(npc, UnderWay({"active", "done"}), kAboutQuest, talk)
        .Build();
}

// ── Fishing quests ───────────────────────────────────────────────────────────────────────────

struct Fish {
    std::string_view name;
    int              kind; /**< As the fishing tables number it: 0 Bobo, 1 Gobbler, 2 Nonky... */
};

constexpr std::array<Fish, 4> kNoruneFish = {{{"Nilers", 7}, {"Gummies", 6}, {"Nonkies", 2}, {"Gobblers", 1}}};
constexpr std::array<Fish, 4> kMatatakiPondFish = {{{"Baku Bakus", 4}, {"Gobblers", 1}, {"Tartons", 10}, {"Umadakaras", 9}}};
constexpr std::array<Fish, 5> kMatatakiFallsFish = {
    {{"Baku Bakus", 4}, {"Nonkies", 2}, {"Gummies", 6}, {"Mardan Garayan", 5}, {"Baron Garayan", 17}}};
constexpr std::array<Fish, 5> kQueensFish = {{{"Bobos", 0}, {"Kajis", 3}, {"Piccolys", 11}, {"Bons", 12}, {"Hamahamas", 13}}};
constexpr std::array<Fish, 5> kMuskaFish = {
    {{"Negies", 14}, {"Dens", 15}, {"Heelas", 16}, {"Mardan Garayans", 5}, {"Baron Garayan", 17}}};

struct Angler {
    std::string_view id;
    int              town;
    std::string_view model_code;
    Text             intro;
};

constexpr Text kPikeIntro = {"quest.enhanced.fish.pike.intro",
                             "Oh, I bet you can already guess what I have in store for you.{page}"
                             "That's right, Fishing Quests!{page}"
                             "There are 2 types of fishing quests, and whenever you talk to me, I can assign you one "
                             "of them.{page}"
                             "I hope you like fishing!"};
constexpr Text kPaoIntro = {"quest.enhanced.fish.pao.intro",
                            "Show me your hunting skills!{page}"
                            "Wait, you don't hunt anything?{page}"
                            "Well, I guess fishing could count as hunting. I can give you some Fishing Quests.{page}"
                            "Keep in mind that there are two fishing spots here!"};
constexpr Text kSamIntro = {"quest.enhanced.fish.sam.intro",
                            "Fighting monsters in the dungeons must be exhausting.{page}"
                            "Why don't you relax with some Fishing Quests?"};
constexpr Text kDeviaIntro = {"quest.enhanced.fish.devia.intro",
                              "I heard some tourists like to fish at the Oasis, since you can apparently find rare "
                              "fish in there.{page}"
                              "And speaking of fishing... would you like some Fishing Quests?"};

constexpr std::array<Angler, 4> kAnglers = {{
    {"enhanced_fish_pike", kNorune, "06", kPikeIntro},
    {"enhanced_fish_pao", kMatataki, "24", kPaoIntro},
    {"enhanced_fish_sam", kQueens, "34", kSamIntro},
    {"enhanced_fish_devia", kMuska, "53", kDeviaIntro},
}};

constexpr Text kFishLabel = {"quest.enhanced.fish.label", "Fishing quests"};
constexpr Text kFishCatch = {"quest.enhanced.fish.catch", "Your quest is to catch %1 %2 %3. Good luck!"};
constexpr Text kFishSize = {"quest.enhanced.fish.size",
                            "Your quest is to catch any fish of a size from %1 cm to %2 cm %3. Good luck!"};
constexpr Text kFishCatchGoing = {"quest.enhanced.fish.catch_going",
                                  "You're still on the quest to catch %1 %2, just %3 left!"};
constexpr Text kFishSizeGoing = {"quest.enhanced.fish.size_going",
                                 "You're still on the quest to catch any fish of a size from %1 cm to %2 cm %3. Good luck!"};
constexpr Text kFishReward = {"quest.enhanced.fish.reward", "Nicely done! Here's your reward: %1 Fishing Points!"};

// Sam's special reward (Enhanced and Expanded): three of his quests done, the fish of the Queens Sea
// can be seen while fishing there.
constexpr int  kSamQuestsForReward = 3;
constexpr Text kSamMore = {"quest.enhanced.fish.sam.more",
                           "Additionally, if you can complete %4 more quests for me, I'll give you a special reward!"};
constexpr Text kSamRewardMore = {"quest.enhanced.fish.sam.reward_more",
                                 "Nicely done! Here's your reward: %1 Fishing Points!{page}"
                                 "If you can complete %2 more quests for me, I'll give you a special reward!"};
constexpr Text kSamSpecial = {"quest.enhanced.fish.sam.special",
                              "Awesome, you completed 3 quests for me!{page}"
                              "As a special reward, you can now see the fish at the Queens Sea! Cool, right?{page}"
                              "I'll also give the regular quest reward: here's %1 Fishing Points!"};

// Where the quest's fish swim: spot 0 is Matataki's pond, 1 its waterfall, 2 anywhere in the village.
std::string_view FishPlace(int town, int spot) {
    switch (town) {
        case kNorune:
            return "at the Norune Pond";
        case kMatataki:
            return spot == 0 ? "at the Matataki Pond" : spot == 1 ? "at the Matataki Waterfall" : "anywhere in Matataki Village";
        case kQueens:
            return "at the Queens Sea";
        default:
            return "at the Oasis";
    }
}

std::string_view FishName(int town, int spot, int fish) {
    switch (town) {
        case kNorune:
            return kNoruneFish[std::clamp(fish, 0, 3)].name;
        case kMatataki:
            return spot == 1 ? kMatatakiFallsFish[std::clamp(fish, 0, 4)].name : kMatatakiPondFish[std::clamp(fish, 0, 3)].name;
        case kQueens:
            return kQueensFish[std::clamp(fish, 0, 4)].name;
        default:
            return kMuskaFish[std::clamp(fish, 0, 4)].name;
    }
}

void SayFishing(Scene &scene, const Angler &angler, Text text, std::vector<std::string> args);

void GiveCatchQuest(Scene &scene, const Angler &angler) {
    const int town = angler.town;
    int       fish = 0;
    int kind = 0;
    int needed = 2;
    int spot = 2;

    switch (town) {
        case kNorune:
            fish = Rand(0, 4);
            kind = kNoruneFish[fish].kind;
            break;
        case kMatataki:
            if (Rand(0, 100) < 50) {
                fish = Rand(0, 4);
                kind = kMatatakiPondFish[fish].kind;
                spot = fish == 0 ? 2 : 0;
            } else {
                // The Garayans are rolled again once, and one of them is enough.
                fish = Rand(0, 5);

                if (fish >= 3) {
                    fish = Rand(0, 5);
                }

                kind = kMatatakiFallsFish[fish].kind;
                needed = fish >= 3 ? 1 : 2;
                spot = fish == 0 ? 2 : 1;
            }
            break;
        case kQueens:
            fish = Rand(0, 5);
            kind = kQueensFish[fish].kind;
            break;
        default:
            fish = Rand(0, 5);

            if (fish >= 3) {
                fish = Rand(0, 5);
            }

            kind = kMuskaFish[fish].kind;
            needed = fish == 3 ? Rand(1, 3) : fish == 4 ? 1 : 2;
            break;
    }

    scene.SetVar("type", 0);
    scene.SetVar("fish", fish);
    scene.SetVar("kind", kind);
    scene.SetVar("spot", spot);
    scene.SetVar("needed", needed);
    scene.SetVar("left", needed);
    scene.SetStage("active");
    SayFishing(scene, angler, kFishCatch,
               {std::to_string(needed), std::string(FishName(town, spot, fish)), std::string(FishPlace(town, spot))});
}

void GiveSizeQuest(Scene &scene, const Angler &angler) {
    const int                                          town = angler.town;
    static constexpr std::array<std::array<int, 3>, 4> kSizes = {{{80, 141, 5}, {80, 141, 5}, {90, 161, 10}, {100, 181, 5}}};
    const auto &range = kSizes[town];
    const int   least = Rand(range[0], range[1]);

    scene.SetVar("type", 1);
    scene.SetVar("min", least);
    scene.SetVar("max", least + range[2]);
    scene.SetStage("active");
    SayFishing(scene, angler, kFishSize, {std::to_string(least), std::to_string(least + range[2]), std::string(FishPlace(town, 2))});
}

int FishReward(const Scene &scene, int town) {
    if (scene.Var("type") == 1) {
        return Rand(scene.Var("max") - 30, scene.Var("max")) + 10 * town;
    }

    static constexpr std::array<std::array<int, 2>, 4> kPoints = {{{35, 66}, {43, 74}, {51, 82}, {60, 91}}};
    int points = Rand(kPoints[town][0], kPoints[town][1]) * scene.Var("needed");

    // Matataki's Garayans pay double.
    if (town == kMatataki && (scene.Var("kind") == 5 || scene.Var("kind") == 17)) {
        points *= 2;
    }

    return points;
}

// Sam's quests still to do for his special reward, 0 once it is given.
int SamQuestsLeft(const Scene &scene, const Angler &angler) {
    if (angler.town != kQueens) {
        return 0;
    }

    return std::max(0, kSamQuestsForReward - scene.Var("completed"));
}

// A fishing quest's own line, with Sam's reminder of his special reward on a page after it.
void SayFishing(Scene &scene, const Angler &angler, Text text, std::vector<std::string> args) {
    const int left = SamQuestsLeft(scene, angler);

    if (left <= 0) {
        scene.Say(text, std::move(args));
        return;
    }

    args.resize(3);
    args.push_back(std::to_string(left));
    const std::string line = localize::LocalizeText(text.key, text.english) + "{page}" +
                             localize::LocalizeText(kSamMore.key, kSamMore.english);
    scene.Session().PushRawMessage(line, std::move(args));
}

void TalkFishing(Scene &scene, const Angler &angler) {
    const int town = angler.town;

    if (scene.AtStage("new")) {
        scene.Say(angler.intro);
        scene.SetStage("idle");
    } else if (scene.AtStage("idle")) {
        if (Rand(0, 100) < 50) {
            GiveCatchQuest(scene, angler);
        } else {
            GiveSizeQuest(scene, angler);
        }

    } else if (scene.AtStage("active")) {
        const int spot = scene.Var("spot");

        if (scene.Var("type") == 0) {
            SayFishing(scene, angler, kFishCatchGoing,
                       {std::string(FishName(town, spot, scene.Var("fish"))), std::string(FishPlace(town, spot)),
                        std::to_string(scene.Var("left"))});
        } else {
            SayFishing(scene, angler, kFishSizeGoing,
                       {std::to_string(scene.Var("min")), std::to_string(scene.Var("max")), std::string(FishPlace(town, 2))});
        }
    } else {
        const int points = FishReward(scene, town);

        if (SaveData != nullptr) {
            SaveData->AddFishingPoint(points);
        }

        scene.SetStage("idle");

        if (angler.town != kQueens || scene.Var("completed") >= kSamQuestsForReward) {
            scene.Say(kFishReward, {std::to_string(points)});
            return;
        }

        const int completed = scene.Var("completed") + 1;
        scene.SetVar("completed", completed);

        if (completed >= kSamQuestsForReward) {
            SetFlag("queens_fish_seen", 1);
            scene.Say(kSamSpecial, {std::to_string(points)});
        } else {
            scene.Say(kSamRewardMore, {std::to_string(points), std::to_string(kSamQuestsForReward - completed)});
        }
    }
}

Definition FishingQuest(const Angler &angler) {
    const NpcMatch npc = Villager(angler.town, angler.model_code);
    const SceneFn  talk = [&angler](Scene &scene) { TalkFishing(scene, angler); };

    return Builder(std::string(angler.id))
        .Stages({"new", "idle", "active", "done"})
        .TalkOption(npc, OnOffer({"new", "idle"}), talk)
        .Option(npc, UnderWay({"active", "done"}), kAboutQuest, talk)
        .Build();
}

// ── Rumours: the back-floor items ────────────────────────────────────────────────────────────

struct Rumour {
    std::string_view id;
    NpcMatch         npc;
    int              dungeon;
    int              item;
    int              odds; /**< The item is hidden when a roll of 0 to 99 comes out above this. */
    Text             tell;
    Text             found;
    Text             waiting;
};

constexpr Text kRumourLabel = {"quest.enhanced.rumour.label", "Heard any rumours?"};

const std::vector<Rumour> &Rumours() {
    static const std::vector<Rumour> rumours = {
        {"enhanced_rumour_laura", Villager(kNorune, "04"), 0, ITEM_MEDUSA_POWDER, 66,
         {"quest.enhanced.rumour.laura.tell",
          "I heard some whispers that if you venture to the backfloors in the Divine Beast Cave, you might come across a "
          "rare item.{page}"
          "Apparently it's some kind of powder."},
         {"quest.enhanced.rumour.laura.found",
          "Oh, you found it? Great! It's a Medusa Powder?{page}"
          "You ask me how to use it? Sorry, I don't know what it is."},
         {"quest.enhanced.rumour.laura.waiting",
          "Did you find that rare item yet? Apparently it can be found somewhere in the backfloors of the Divine Beast "
          "Cave."}},
        {"enhanced_rumour_ro", Villager(kMatataki, "21"), 1, ITEM_WARP_POWDER, 66,
         {"quest.enhanced.rumour.ro.tell",
          "One of the Wise Owl Forest's owls told me that somewhere deep at the backsides of the Forest, you can find "
          "some mysterious item.{page}"
          "It's said to be an ancient item, whose purpose has never been fulfilled."},
         {"quest.enhanced.rumour.ro.found",
          "You have acquired the ancient item? You say it's a Warp Powder?{page}"
          "Hmmm, something like that sounds too powerful, maybe that's why it hasn't been able to show its potential."},
         {"quest.enhanced.rumour.ro.waiting",
          "Have you found the ancient item yet? It's said to be somewhere at the backfloors of Wise Owl Forest."}},
        {"enhanced_rumour_phil", Villager(kQueens, "42"), 2, ITEM_SHELL_RING, 66,
         {"quest.enhanced.rumour.phil.tell",
          "There are some more details I didn't tell you about the Queen.{page}"
          "It's rumoured that when she sank to the bottom of the Shipwreck, her wedding ring got lost.{page}"
          "The ring might still be somewhere deep in the Shipwreck. Do you think you can find it?"},
         {"quest.enhanced.rumour.phil.found",
          "You found the Queen's wedding ring? I see, so the rumours were true! Interesting.{page}"
          "You can keep the ring, who knows where you might need it in your adventure."},
         {"quest.enhanced.rumour.phil.waiting",
          "The Queen's wedding ring could be hiding somewhere at the backside of the Shipwreck. Good luck searching!"}},
        {"enhanced_rumour_zabo", Villager(kMuska, "50"), 3, ITEM_HARDENING_POWDER, 66,
         {"quest.enhanced.rumour.zabo.tell",
          "A long time ago, there used to be this rare and mysterious yellow powder, and no, I'm not talking about "
          "Repair Powder.{page}"
          "The true details of this powder are unknown, but the ancients might have left one at the dark side of the "
          "Sun & Moon Temple."},
         {"quest.enhanced.rumour.zabo.found",
          "I see that you have found the mysterious powder. I believe the ancients used to call it a Hardening "
          "Powder.{page}"
          "I wish we knew more about it."},
         {"quest.enhanced.rumour.zabo.waiting",
          "Did you find the powder yet? There might still be one somewhere at the dark side of the Sun & Moon Temple."}},
        {"enhanced_rumour_shaft", VillagerNo(kBrownboo, 3), kDemonShaft, ITEM_MAGICAL_LAMP, 85,
         {"quest.enhanced.rumour.shaft.tell",
          "According to a legend, there is a rare artifact hidden somewhere in the darkness of the Demon Shaft.{page}"
          "I heard it's so valuable that not even money can buy it."},
         {"quest.enhanced.rumour.shaft.found",
          "What's that? You found the rare artifact?{page}"
          "Magical Lamp... hmmm....{page}"
          "If you don't know what to do with it, you should try speaking to someone in Queens, there's plenty of "
          "artifact merchandise."},
         {"quest.enhanced.rumour.shaft.waiting",
          "Have you found the Demon Shaft's legendary artifact yet? Try searching behind every stone and WALL."}},
    };
    return rumours;
}

constexpr Text kShaftTower = {"quest.enhanced.rumour.shaft.tower",
                              "Have you heard about this?{page}"
                              "There is a legend that somewhere north-east of the Matataki Village, you can find a very "
                              "long evil tower.{page}"
                              "If you happen to find it, come back to me. I might have more to tell."};

void TalkRumour(Scene &scene, const Rumour &rumour) {
    if (scene.AtStage("new")) {
        // The Demon Shaft's rumour waits until the tower has been found.
        if (rumour.dungeon == kDemonShaft && scene.Game().DeepestFloor(kDemonShaft) < 0) {
            scene.Say(kShaftTower);
            return;
        }

        scene.Say(rumour.tell);
        scene.SetStage("told");
    } else if (scene.Game().HasItemAnywhere(rumour.item)) {
        scene.Say(rumour.found);
    } else {
        scene.Say(rumour.waiting);
    }
}

Definition RumourQuest(const Rumour &rumour) {
    const SceneFn talk = [&rumour](Scene &scene) { TalkRumour(scene, rumour); };

    return Builder(std::string(rumour.id))
        .Stages({"new", "told"})
        .TalkOption(rumour.npc, OnOffer({"new"}), talk)
        .Option(rumour.npc, UnderWay({"told"}), kAboutQuest, talk)
        .Build();
}

// ── Yellow Drops: Samba's challenge and the crystal puzzle ───────────────────────────────────

constexpr int kFullParty = 6;

constexpr Text kChallengeLabel = {"quest.enhanced.samba.label", "Your challenge"};
constexpr Text kPuzzleLabel = {"quest.enhanced.puzzle.label", "Your puzzle"};
constexpr Text kNoBoss = {"quest.enhanced.yellow_drops.no_boss",
                          "Have you seen our Boss anywhere?{page}"
                          "Go search for him first. Come back to me if you find him."};

constexpr Text kSambaTell = {"quest.enhanced.samba.tell",
                             "I have a challenge to test your true strength.{page}"
                             "If you can conquer Moon Sea Floor 7's backside only with {name1} and only using the Dagger, "
                             "you can have my reward."};
constexpr Text kSambaWaiting = {"quest.enhanced.samba.waiting",
                                "If you can conquer Moon Sea Floor 7's backside only with {name1} and only using the "
                                "Dagger, you can have my reward."};
constexpr Text kSambaReward = {"quest.enhanced.samba.reward",
                               "You completed the challenge? Extraordinary!{page}"
                               "Here, you can have this special Map.{page}"
                               "While this is in your bag, it works like the Map you find in the dungeons, but it is "
                               "always activated!{page}"
                               "Also, the Map you find in dungeons gets replaced with extra loot. Pretty cool, huh?"};
constexpr Text kSambaDone = {"quest.enhanced.samba.done", "I already gave you my reward, you don't need a second map!"};

void TalkSamba(Scene &scene) {
    if (scene.AtStage("done")) {
        scene.Say(kSambaDone);
    } else if (scene.Game().PartySize() < kFullParty) {
        scene.Say(kNoBoss);
    } else if (scene.AtStage("new")) {
        scene.Say(kSambaTell);
        scene.SetStage("active");
    } else if (scene.AtStage("active")) {
        scene.Say(kSambaWaiting);
    } else if (!scene.Game().GiveItem(ITEM_MAP)) {
        scene.Say(kNoRoom);
    } else {
        scene.SetStage("done");
        scene.Say(kSambaReward);
    }
}

constexpr Text kPuzzleTell = {"quest.enhanced.puzzle.tell",
                              "I have an extremely valuable crystal.{page}"
                              "I know you are strong, but if you want my crystal, you need to prove that you are smart "
                              "as well!{page}"
                              "I have a puzzle for you. Talk to me again if you wish to know more."};
constexpr Text kPuzzleHint = {"quest.enhanced.puzzle.hint",
                              "I want you to show me certain four items in a correct order. Place those items in the "
                              "first four slots of your bag.{page}"
                              "What are the items? I'm not straight up telling them! However...{page}"
                              "In the Blue Terra, your homeworld, you might have met some people who needed "
                              "something.{page}"
                              "Perhaps they are the clue to the right answer?"};
constexpr Text kPuzzleSolved = {"quest.enhanced.puzzle.solved",
                                "That's correct, well done! You solved the puzzle!{page}"
                                "As a reward, have this ultra-rare Magical Crystal.{page}"
                                "While this is in your bag, it works like the Magical Crystal you find in the dungeons, "
                                "but it is always activated!{page}"
                                "Also, the Magical Crystal you find in dungeons gets replaced with extra loot. Worth it, "
                                "right?"};
constexpr Text kPuzzleDone = {"quest.enhanced.puzzle.done", "I already gave you my reward, you don't need a second crystal!"};

bool PuzzleSolved() {
    static constexpr std::array<int, 4> kAnswer = {ITEM_GOOEY_PEACH, ITEM_ESCAPE_POWDER, ITEM_STAMINA_DRINK, ITEM_BOMB};
    const CDngStatusData               *status = Status();

    if (status == nullptr) {
        return false;
    }

    for (std::size_t slot = 0; slot < kAnswer.size(); slot++) {
        if (status->item_pack.item[slot] != kAnswer[slot]) {
            return false;
        }
    }

    return true;
}

void TalkPuzzle(Scene &scene) {
    if (scene.AtStage("done")) {
        scene.Say(kPuzzleDone);
    } else if (scene.Game().PartySize() < kFullParty) {
        scene.Say(kNoBoss);
    } else if (scene.AtStage("new")) {
        scene.Say(kPuzzleTell);
        scene.SetStage("asked");
    } else if (!PuzzleSolved()) {
        scene.Say(kPuzzleHint);
    } else if (!scene.Game().GiveItem(ITEM_MAGICAL_CRYSTAL)) {
        scene.Say(kNoRoom);
    } else {
        scene.SetStage("done");
        scene.Say(kPuzzleSolved);
    }
}

// ── Pickle: the collection ───────────────────────────────────────────────────────────────────

constexpr std::array<int, 32> kCollectionAttachments = {81,  82,  83,  84,  85,  90,  91,  92,  93,  94,  95,
                                                        96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106,
                                                        111, 112, 113, 114, 115, 116, 117, 118, 119, 120};
constexpr std::array<int, 57> kCollectionItems = {132, 133, 134, 135, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
                                                  159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 174, 175, 176,
                                                  177, 178, 181, 183, 185, 186, 187, 188, 189, 190, 192, 193, 197, 199, 224,
                                                  225, 226, 227, 228, 229, 230, 231, 235, 245, 246, 247, 253};
constexpr std::array<int, 15> kCollectionWeapons = {280, 295, 296, 297, 298, 312, 313, 324, 329, 341, 345, 356, 357, 372, 373};
constexpr std::array<int, 8>  kCollectionSecrets = {171, 172, 173, 191, 233, 234, 241, 243};

constexpr Text kPickleLabel = {"quest.enhanced.pickle.label", "My collection"};
constexpr Text kPickleIntro = {"quest.enhanced.pickle.intro",
                               "Hey there, wanderer! Would you like to know your collection progress? Whenever you talk "
                               "to me, I'll check your obtained items and weapons.{page}"
                               "You need to be either carrying them or have them in your storage.{page}"
                               "Can you make it to the 100% collection? It won't be easy, but if you commit to it, you "
                               "can achieve anything! Good luck!"};
constexpr Text kPickleCount = {"quest.enhanced.pickle.count",
                               "You have collected: %1 / %2 obtainable items and attachments, %3 / %4 obtainable "
                               "ultimate weapons, %5 / %6 secret items."};
constexpr Text kPickleShort = {"quest.enhanced.pickle.short",
                               "Hmm, seems like you don't have 100% collection yet. Don't worry, it's a massive "
                               "achievement to reach!{page}"
                               "Keep in mind that all quest items and character stat-boosting items are not part of the "
                               "collection. Good luck!"};
constexpr Text kPickleKey = {"quest.enhanced.pickle.key",
                             "WOW! Congratulations, you've actually done it! You collected everything!{page}"
                             "As a present, have this old key. You should totally try if you can use it somewhere!"};
constexpr Text kPickleAll = {"quest.enhanced.pickle.all", "WOW! Congratulations, you've actually done it! You collected everything!"};

template <typename T>
int Collected(const IGame &game, const T &list) {
    return static_cast<int>(std::ranges::count_if(list, [&game](int item) { return game.HasItemAnywhere(item); }));
}

void TalkPickle(Scene &scene) {
    if (scene.AtStage("new")) {
        scene.Say(kPickleIntro);
        scene.SetStage("open");
        return;
    }

    const IGame &game = scene.Game();
    const int    items = Collected(game, kCollectionItems) + Collected(game, kCollectionAttachments);
    const int    all_items = static_cast<int>(kCollectionItems.size() + kCollectionAttachments.size());
    const int    weapons = Collected(game, kCollectionWeapons);
    const int    secrets = Collected(game, kCollectionSecrets);

    scene.Say(kPickleCount, {std::to_string(items), std::to_string(all_items), std::to_string(weapons),
                             std::to_string(kCollectionWeapons.size()), std::to_string(secrets),
                             std::to_string(kCollectionSecrets.size())});

    if (items < all_items || weapons < static_cast<int>(kCollectionWeapons.size()) ||
        secrets < static_cast<int>(kCollectionSecrets.size())) {
        scene.Say(kPickleShort);
        return;
    }

    // The whole collection opens the Mayor's quests; the key comes with it while the player has none.
    SetFlag("collection_done", 1);

    if (!scene.Game().HasItemAnywhere(ITEM_FLAME_KEY) && scene.Game().GiveItem(ITEM_FLAME_KEY)) {
        scene.Say(kPickleKey);
    } else {
        scene.Say(kPickleAll);
    }
}

// ── The Mayor's bonus quests ─────────────────────────────────────────────────────────────────

constexpr int   kBonusLife = 250;
constexpr int   kBonusDefense = 99;
constexpr float kBonusWater = 120.0f;

constexpr Text kMayorLabel = {"quest.enhanced.mayor.label", "Any quests for me?"};
constexpr Text kMayorEarly = {"quest.enhanced.mayor.early",
                              "I might have some quests for you later.{page}"
                              "But for now, come back when you're more experienced."};
constexpr Text kMayorJourney = {"quest.enhanced.mayor.journey",
                                "I see that your journey brings you many travels around the world.{page}"
                                "While you are at it, I'm working on something else that might help you later.{page}"
                                "Return to me when you are near the end of your journey."};
constexpr Text kMayorGenie = {"quest.enhanced.mayor.genie",
                              "I knew you could do it! Now that the Dark Genie has been defeated, we can live in "
                              "peace!{page}"
                              "What's that? You want to do more sidequests?{page}"
                              "Hmm... don't you have one more dungeon to complete?"};
constexpr Text kMayorShaft = {"quest.enhanced.mayor.shaft",
                              "You conquered the Demon Shaft? Well done!{page}"
                              "Unfortunately you're still not ready for my quests.{page}"
                              "Come back when you've obtained everything in this world.{page}"
                              "Perhaps someone at the Brownboo Village can give more details?"};
constexpr Text kMayorOpen = {"quest.enhanced.mayor.open",
                             "Woah, you truly are something!{page}"
                             "You know what? Since you proved your determination to collect everything, how about doing "
                             "some bonus sidequests?{page}"
                             "After you left Norune, I happened to collect a bunch of Fruit of Edens, Gourds and Defense "
                             "items.{page}"
                             "I'll assign you some slightly challenging Dungeon quests, and for the reward you'll get "
                             "one of the boost items, as long as you're not maxed on them.{page}"
                             "All your party members' maximum health has increased to 250, maximum thirst to 12 and "
                             "maximum defense to 99.{page}"
                             "Do you have what it takes to max your abilities?"};
constexpr Text kMayorTask = {"quest.enhanced.mayor.task",
                             "Clear the backside of floor %1 in the Demon Shaft, using only %2.{page}"
                             "You can buy backfloor keys from the Fairy King."};
constexpr Text kMayorReward = {"quest.enhanced.mayor.reward", "Nicely done! Here's your reward: %1."};
constexpr Text kMayorMaxed = {"quest.enhanced.mayor.maxed",
                              "I can't believe it!{page}"
                              "You actually went and maxed all the stats!{page}"
                              "Thank you for completing all my quests, and also...{page}"
                              "Thank you for playing the Enhanced Mod!"};
constexpr Text kMayorFinished = {"quest.enhanced.mayor.finished",
                                 "I have no more quests to give. Perhaps you've finally achieved everything?"};

constexpr std::string_view kMayorId = "enhanced_mayor";

struct Boost {
    int              item;
    std::string_view name;
};

// Each character's favourite food raises their defense.
constexpr std::array<Boost, 6> kDefenseFoods = {{{ITEM_FLUFFY_DOUGHNUT, "Fluffy Doughnut"},
                                                 {ITEM_FISH_CANDY, "Fish Candy"},
                                                 {ITEM_GRASS_CAKE, "Grass Cake"},
                                                 {ITEM_WITCH_PARFAIT, "Witch Parfait"},
                                                 {ITEM_SCORPION_JERKY, "Scorpion Jerky"},
                                                 {ITEM_CARROT_COOKIE, "Carrot Cookie"}}};

// Enhanced's draw: a third of the time each of life, defense and water comes first, falling to the
// next one not yet maxed; defense goes to a character still under 99. None is left once all are maxed.
Boost MayorBoost(bool life, bool defense, bool water) {
    bool give_life = false;
    bool give_defense = false;
    bool give_water = false;
    const int kind = Rand(0, 300);

    if (kind >= 200) {
        (life ? give_life : defense ? give_defense : give_water) = true;
    } else if (kind >= 100) {
        (defense ? give_defense : water ? give_water : give_life) = true;
    } else if (water) {
        give_water = true;
    } else if (Rand(0, 100) > 50) {
        (life ? give_life : give_defense) = true;
    } else {
        (defense ? give_defense : give_life) = true;
    }

    if (give_life) {
        return {ITEM_FRUIT_OF_EDEN, "Fruit of Eden"};
    }

    if (give_water) {
        return {ITEM_GOURD, "Gourd"};
    }

    const CDngStatusData *status = Status();

    for (int tries = 0; status != nullptr && tries < 1000; tries++) {
        const int chara = Rand(0, 6);

        if (status->defense[chara] < kBonusDefense) {
            return kDefenseFoods[chara];
        }
    }

    return {-1, "Nothing!"};
}

// The task, with the ally's name tag put into the text itself so the game's renderer fills it in.
void SayMayorTask(Scene &scene) {
    std::string line = localize::LocalizeText(kMayorTask.key, kMayorTask.english);
    const auto  at = line.find("%2");

    if (at != std::string::npos) {
        line.replace(at, 2, std::format("{{name{}}}", std::clamp(scene.Var("ally"), 0, 5) + 1));
    }

    scene.Session().PushRawMessage(line, {std::to_string(scene.Var("floor"))});
}

void GiveMayorTask(Scene &scene) {
    const int floor = Rand(1, 100);
    const int ally = Rand(0, 6);
    scene.SetVar("floor", floor);
    scene.SetVar("ally", ally);
    scene.SetStage("active");
    SayMayorTask(scene);
}

void TalkMayor(Scene &scene) {
    if (scene.AtStage("locked")) {
        if (Flag("collection_done") != 0) {
            scene.Say(kMayorOpen);
            scene.SetStage("open");
        } else if (scene.Game().HasItemAnywhere(ITEM_WEAPON_CHRONICLE2)) {
            scene.Say(kMayorShaft);
        } else if (scene.Game().DeepestFloor(kDemonShaft) >= 0) {
            scene.Say(kMayorGenie);
        } else if (scene.Game().PartySize() >= 2) {
            scene.Say(kMayorJourney);
        } else {
            scene.Say(kMayorEarly);
        }
    } else if (scene.AtStage("open")) {
        GiveMayorTask(scene);
    } else if (scene.AtStage("active")) {
        SayMayorTask(scene);
    } else if (scene.AtStage("cleared")) {
        const CDngStatusData *status = Status();
        bool                  life = false;
        bool                  defense = false;
        bool                  water = false;

        for (int chara = 0; status != nullptr && chara < 6; chara++) {
            life = life || status->max_hp[chara] < kBonusLife;
            defense = defense || status->defense[chara] < kBonusDefense;
            water = water || status->water_max[chara] < kBonusWater;
        }

        if (!life && !defense && !water) {
            scene.SetStage("finished");
            scene.Say(kMayorMaxed);
            return;
        }

        const Boost boost = MayorBoost(life, defense, water);

        if (boost.item >= 0 && !scene.Game().GiveItem(boost.item)) {
            scene.Say(kNoRoom);
            return;
        }

        scene.SetStage("open");
        scene.Say(kMayorReward, {std::string(boost.name)});
    } else {
        scene.Say(kMayorFinished);
    }
}

bool MayorOpen() {
    const Progress *progress = FindProgress(kMayorId);
    return progress != nullptr && progress->stage != "locked";
}

// ── Mint: every kind of fish (Dark Cloud Expanded, Quests/MasterFishQuest.cs) ─────────────────

constexpr int              kMintVillager = 5;
constexpr std::string_view kMintId = "expanded_mint";

struct FishKind {
    int              kind;
    std::string_view name;
};

// The 17 kinds, as the fishing tables number them (8 is the unused Missing Fish).
constexpr std::array<FishKind, 17> kAllFish = {{{0, "Bobo"},
                                                {1, "Gobbler"},
                                                {2, "Nonky"},
                                                {3, "Kaiji"},
                                                {4, "Baku Baku"},
                                                {5, "Mardan Garayan"},
                                                {6, "Gummy"},
                                                {7, "Niler"},
                                                {9, "Umadakara"},
                                                {10, "Tarton"},
                                                {11, "Piccoly"},
                                                {12, "Bon"},
                                                {13, "Hamahama"},
                                                {14, "Negie"},
                                                {15, "Den"},
                                                {16, "Heela"},
                                                {17, "Baron Garayan"}}};

constexpr Text kMintIntro = {"quest.expanded.mint.intro",
                             "Do you like fishing? I have a great challenge for you.{page}"
                             "Catch all 17 different types of fish, and I will reward you with a rare item!{page}"
                             "You can talk to me again to know your current progress."};
constexpr Text kMintLeft = {"quest.expanded.mint.left",
                            "You still have to find the following fish:{page}%1{page}"
                            "Come back when you have caught them! Good luck!"};
constexpr Text kMintReward = {"quest.expanded.mint.reward",
                              "So you have completed my challenge?{page}"
                              "Cool, you truly are a Master of Fishing! Here, have this rare book that I found the other "
                              "day.{page}"
                              "I don't know what it does, but maybe some people collect them."};
constexpr Text kMintHasBook = {"quest.expanded.mint.has_book",
                               "I already gave you my reward, but if you somehow lose it you can ask for another!"};
constexpr Text kMintLost = {"quest.expanded.mint.lost",
                            "What? You lost the reward??{page}"
                            "Well, I have another one here, don't lose it again!"};

std::string FishFlag(int kind) {
    return "fish_caught_" + std::to_string(kind);
}

// A kind counts once it has been landed: since this was ported, or in the fishing records before it.
bool FishCaught(int kind) {
    if (Flag(FishFlag(kind)) != 0) {
        return true;
    }

    if (SaveData == nullptr) {
        return false;
    }

    for (const SV_FISH_DATA &entry : SaveData->FishingRanks()) {
        if (entry.rank > 0.0f && entry.fish_id == kind) {
            return true;
        }
    }

    return false;
}

int FishKindsCaught() {
    return static_cast<int>(std::ranges::count_if(kAllFish, [](const FishKind &fish) { return FishCaught(fish.kind); }));
}

// The kinds still to catch, four to a page.
std::string FishStillToCatch() {
    std::string list;
    int         listed = 0;

    for (const FishKind &fish : kAllFish) {
        if (FishCaught(fish.kind)) {
            continue;
        }

        if (listed > 0) {
            list += listed % 4 == 0 ? "{page}" : "\n";
        }

        list += fish.name;
        listed++;
    }

    return list;
}

void TalkMint(Scene &scene) {
    if (scene.AtStage("new")) {
        scene.Say(kMintIntro);
        scene.SetStage("open");
        return;
    }

    if (FishKindsCaught() < static_cast<int>(kAllFish.size())) {
        scene.Say(kMintLeft, {FishStillToCatch()});
        return;
    }

    if (scene.Game().HasItemAnywhere(ITEM_SAVING_BOOK)) {
        scene.Say(kMintHasBook);
        return;
    }

    if (!scene.Game().GiveItem(ITEM_SAVING_BOOK)) {
        scene.Say(kNoRoom);
        return;
    }

    scene.Say(scene.AtStage("rewarded") ? kMintLost : kMintReward);
    scene.SetStage("rewarded");
}

// ── The floor challenges ─────────────────────────────────────────────────────────────────────

enum class Challenge : u8 { None, Samba, Mayor };

// Throwing a throwable plays these motions (BtActStatus+4, as Enhanced reads it).
constexpr int kThrowMotionA = 26;
constexpr int kThrowMotionB = 27;

struct FloorChallenge {
    Challenge kind = Challenge::None;
    bool      checked = false;
    bool      running = false;
    bool      had_monsters = false;
    int       settle_steps = 0;
    int       empty_steps = 0;
    int       chara = -1;
};

FloorChallenge g_challenge;

constexpr std::string_view kSambaId = "enhanced_samba";

Challenge FloorChallengeKind() {
    if (UserStatus == nullptr || lightingMode != 1) {
        return Challenge::None;
    }

    const Progress *samba = FindProgress(kSambaId);

    if (selectMapNo == kMoonSea && UserStatus->cur_floor == 6 && samba != nullptr && samba->stage == "active") {
        return Challenge::Samba;
    }

    const Progress *mayor = FindProgress(kMayorId);

    if (selectMapNo == kDemonShaft && mayor != nullptr && mayor->stage == "active" &&
        UserStatus->cur_floor == VarOf(*mayor, "floor") - 1) {
        return Challenge::Mayor;
    }

    return Challenge::None;
}

bool HoldsDagger() {
    const int held = expanded::ToanWeapon();
    return held == ITEM_WEAPON_DAGGER || held == ITEM_WEAPON_DAGGER_BROKEN;
}

void StartChallenge() {
    g_challenge.checked = true;
    g_challenge.chara = UserStatus->cur_chara;

    if (g_challenge.kind == Challenge::Samba) {
        g_challenge.running = UserStatus->cur_chara == 0 && HoldsDagger();
        expanded::ShowDungeonMessage(g_challenge.running ? "Samba's quest started!\nClear all enemies using only the Dagger!\n"
                                                           "Using a throwable also\ncancels the mission."
                                                         : "Samba's quest did not start.\nRe-enter with the Dagger equipped.",
                                     (g_challenge.running ? 8 : 4) * kStepsPerSecond);
        return;
    }

    const Progress *mayor = FindProgress(kMayorId);
    g_challenge.running = mayor != nullptr && UserStatus->cur_chara == VarOf(*mayor, "ally");
    expanded::ShowDungeonMessage(g_challenge.running ? "Mayor's quest started!\nClear all enemies.\nCannot change character.\n"
                                                       "Throwables are not allowed."
                                                     : "Mayor's quest did not start.\nRe-enter with the correct ally.",
                                 (g_challenge.running ? 5 : 4) * kStepsPerSecond);
}

bool ChallengeBroken() {
    if (BtActStatus.main_motion == kThrowMotionA || BtActStatus.main_motion == kThrowMotionB) {
        return true;
    }

    if (UserStatus->cur_chara != g_challenge.chara) {
        return true;
    }

    return g_challenge.kind == Challenge::Samba && !HoldsDagger();
}

bool AnyMonsterLeft() {
    for (const MONSTOR &monster : NowMonstorUnit->monster) {
        if (monster.state != -1 && monster.hp > 0) {
            return true;
        }
    }

    return false;
}

} // namespace

// ── Hooks ────────────────────────────────────────────────────────────────────────────────────

void EnhancedMonsterDown(const MONSTOR &monster) {
    // Enhanced leaves the Demon Shaft out of the hunts.
    if (selectMapNo == kDemonShaft) {
        return;
    }

    for (const Hunter &hunter : kHunters) {
        Progress *progress = EditProgress(hunter.id);

        if (progress == nullptr || progress->stage != "active" || VarOf(*progress, "name_no") != monster.name_no) {
            continue;
        }

        const int left = VarOf(*progress, "left") - 1;
        progress->vars["left"] = left;

        if (left <= 0) {
            progress->stage = "done";
            expanded::ShowDungeonMessage(std::format("You completed {}'s quest!\nWell done!", hunter.name), 4 * kStepsPerSecond);
        }
    }
}

void EnhancedFishCaught(int town, int kind, int size) {
    // Dark Cloud Expanded: Mint's quest counts every kind landed.
    if (kind >= 0) {
        SetFlag(FishFlag(kind), 1);
    }

    for (const Angler &angler : kAnglers) {
        Progress *progress = EditProgress(angler.id);

        if (angler.town != town || progress == nullptr || progress->stage != "active") {
            continue;
        }

        if (VarOf(*progress, "type") == 0) {
            if (VarOf(*progress, "kind") != kind) {
                continue;
            }

            const int left = VarOf(*progress, "left") - 1;
            progress->vars["left"] = left;

            if (left <= 0) {
                progress->stage = "done";
            }
        } else if (VarOf(*progress, "min") <= size && size <= VarOf(*progress, "max")) {
            progress->stage = "done";
        }
    }
}

void EnhancedFloorStart() {
    g_challenge = {};
}

void EnhancedDungeonStep() {
    if (UserStatus == nullptr || NowMonstorUnit == nullptr) {
        return;
    }

    if (!g_challenge.checked) {
        g_challenge.kind = FloorChallengeKind();

        // As Enhanced waits for the HUD: the floor has a second to come in before the check.
        if (g_challenge.kind == Challenge::None || ++g_challenge.settle_steps < kStepsPerSecond) {
            return;
        }

        StartChallenge();
    }

    if (!g_challenge.running) {
        return;
    }

    if (ChallengeBroken()) {
        g_challenge.running = false;
        expanded::ShowDungeonMessage(g_challenge.kind == Challenge::Samba
                                         ? "Samba's quest has been cancelled.\nRe-enter in order to activate it."
                                         : "Mayor's quest has been cancelled.\nRe-enter in order to re-attempt it.",
                                     4 * kStepsPerSecond);
        return;
    }

    // Done once every monster is down, the floor empty for half a second so one still coming in
    // does not count as cleared.
    if (AnyMonsterLeft()) {
        g_challenge.had_monsters = true;
        g_challenge.empty_steps = 0;
        return;
    }

    if (!g_challenge.had_monsters || ++g_challenge.empty_steps < kStepsPerSecond / 2) {
        return;
    }

    g_challenge.running = false;

    if (Progress *progress = EditProgress(g_challenge.kind == Challenge::Samba ? kSambaId : kMayorId)) {
        progress->stage = "cleared";
    }

    expanded::ShowDungeonMessage(g_challenge.kind == Challenge::Samba ? "Samba's quest completed!\nWell done!"
                                                                      : "Mayor's quest completed!\nWell done!",
                                 4 * kStepsPerSecond);
}

bool EnhancedFountainsOff() {
    return g_challenge.running && g_challenge.kind == Challenge::Samba;
}

int EnhancedBackFloorItem(int dungeon) {
    const IGame *game = Game();

    if (game == nullptr) {
        return -1;
    }

    for (const Rumour &rumour : Rumours()) {
        const Progress *progress = FindProgress(rumour.id);

        if (rumour.dungeon != dungeon || progress == nullptr || progress->stage != "told") {
            continue;
        }

        if (Rand(0, 100) > rumour.odds && !game->HasItemAnywhere(rumour.item)) {
            return rumour.item;
        }
    }

    return -1;
}

bool EnhancedQueensFishSeen() {
    return Flag("queens_fish_seen") != 0;
}

// ── The quest tracker (Dark Cloud Expanded, Quests/QuestTracker.cs) ──────────────────────────

namespace {

constexpr std::array<std::string_view, 6> kAllyNames = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

std::string_view TownName(int town) {
    switch (town) {
        case kNorune:
            return "Norune";
        case kMatataki:
            return "Matataki";
        case kQueens:
            return "Queens";
        case kMuska:
            return "Muska Lacka";
        case kBrownboo:
            return "Brownboo";
        case kYellowDrops:
            return "Yellow Drops";
        default:
            return "";
    }
}

std::string_view AnglerName(const Angler &angler) {
    switch (angler.town) {
        case kNorune:
            return "Pike";
        case kMatataki:
            return "Pao";
        case kQueens:
            return "Sam";
        default:
            return "Devia";
    }
}

struct RumourInfo {
    std::string_view giver;
    int              town;
    std::string_view item;
};

RumourInfo RumourOf(const Rumour &rumour) {
    switch (rumour.dungeon) {
        case 0:
            return {"Laura", kNorune, "a powder"};
        case 1:
            return {"Ro", kMatataki, "an ancient item"};
        case 2:
            return {"Phil", kQueens, "the Queen's wedding ring"};
        case 3:
            return {"Zabo", kMuska, "a mysterious powder"};
        default:
            return {"A villager", kBrownboo, "a rare artifact"};
    }
}

TrackedQuest Tracked(std::string_view giver, int town, std::string task) {
    return TrackedQuest{.giver = std::format("{} ({})", giver, TownName(town)), .task = std::move(task)};
}

std::string ReturnTo(std::string_view giver) {
    return std::format("Done! Go back to {} for the reward", giver);
}

} // namespace

std::vector<TrackedQuest> EnhancedTrackedQuests() {
    std::vector<TrackedQuest> out;

    for (const Hunter &hunter : kHunters) {
        const Progress *progress = FindProgress(hunter.id);

        if (progress == nullptr) {
            continue;
        }

        if (progress->stage == "active") {
            const int dungeon = std::clamp(VarOf(*progress, "dungeon"), 0, 5);
            const int monster = std::clamp(VarOf(*progress, "monster"), 0, 3);
            out.push_back(Tracked(hunter.name, hunter.map_no,
                                  std::format("Defeat {} more {} in the {}", VarOf(*progress, "left"),
                                              kHuntMonsters[dungeon][monster].name, kDungeonNames[dungeon])));
        } else if (progress->stage == "done") {
            out.push_back(Tracked(hunter.name, hunter.map_no, ReturnTo(hunter.name)));
        }
    }

    for (const Angler &angler : kAnglers) {
        const Progress *progress = FindProgress(angler.id);

        if (progress == nullptr) {
            continue;
        }

        const int town = angler.town;

        if (progress->stage == "active") {
            const int spot = VarOf(*progress, "spot");

            if (VarOf(*progress, "type") == 0) {
                const int needed = VarOf(*progress, "needed");
                const int left = VarOf(*progress, "left");
                out.push_back(Tracked(AnglerName(angler), town,
                                      std::format("Catch {} {} {} ({}/{})", needed, FishName(town, spot, VarOf(*progress, "fish")),
                                                  FishPlace(town, spot), needed - left, needed)));
            } else {
                out.push_back(Tracked(AnglerName(angler), town,
                                      std::format("Catch a fish of {} to {} cm {}", VarOf(*progress, "min"),
                                                  VarOf(*progress, "max"), FishPlace(town, 2))));
            }
        } else if (progress->stage == "done") {
            out.push_back(Tracked(AnglerName(angler), town, ReturnTo(AnglerName(angler))));
        }
    }

    const IGame *game = Game();

    for (const Rumour &rumour : Rumours()) {
        const Progress *progress = FindProgress(rumour.id);

        if (progress == nullptr || progress->stage != "told" || (game != nullptr && game->HasItemAnywhere(rumour.item))) {
            continue;
        }

        const RumourInfo info = RumourOf(rumour);
        out.push_back(Tracked(info.giver, info.town,
                              std::format("Find {} on the {}'s back floors", info.item, kDungeonNames[rumour.dungeon])));
    }

    if (const Progress *samba = FindProgress(kSambaId); samba != nullptr) {
        if (samba->stage == "active") {
            out.push_back(Tracked("Samba", kYellowDrops, "Clear Moon Sea floor 7's back floor with Toan and the Dagger only"));
        } else if (samba->stage == "cleared") {
            out.push_back(Tracked("Samba", kYellowDrops, ReturnTo("Samba")));
        }
    }

    if (const Progress *mayor = FindProgress(kMayorId); mayor != nullptr) {
        if (mayor->stage == "active") {
            const int ally = std::clamp(VarOf(*mayor, "ally"), 0, 5);
            out.push_back(Tracked("Mayor", kNorune,
                                  std::format("Clear the back floor of Demon Shaft floor {} with only {}", VarOf(*mayor, "floor"),
                                              kAllyNames[ally])));
        } else if (mayor->stage == "cleared") {
            out.push_back(Tracked("Mayor", kNorune, ReturnTo("the Mayor")));
        }
    }

    if (const Progress *mint = FindProgress(kMintId); mint != nullptr && mint->stage == "open") {
        const int caught = FishKindsCaught();
        out.push_back(Tracked("Mint", kBrownboo,
                              caught < static_cast<int>(kAllFish.size())
                                  ? std::format("Catch every kind of fish ({}/{})", caught, kAllFish.size())
                                  : ReturnTo("Mint")));
    }

    return out;
}

int EnhancedEdenLimit(int limit) {
    return MayorOpen() ? kBonusLife : limit;
}

bool EnhancedMayorOpen() {
    return MayorOpen();
}

float EnhancedGourdLimit() {
    return MayorOpen() ? kBonusWater - 10.0f : 90.0f;
}

// ── Registration ─────────────────────────────────────────────────────────────────────────────

void RegisterEnhancedQuests() {
    Register(Builder(std::string(kFlags)).Stages({"open"}).Build());
    // Dark Cloud Expanded: Atlamillia Insurance keeps its waiting spheres here (expanded_weapons.cpp).
    Register(Builder("expanded_atlamillia").Stages({"open"}).Build());

    for (const Hunter &hunter : kHunters) {
        Register(HuntQuest(hunter));
    }

    for (const Angler &angler : kAnglers) {
        Register(FishingQuest(angler));
    }

    for (const Rumour &rumour : Rumours()) {
        Register(RumourQuest(rumour));
    }

    Register(Builder(std::string(kSambaId))
                 .Stages({"new", "active", "cleared", "done"})
                 .Option(VillagerNo(kYellowDrops, 1), Always(), kChallengeLabel, TalkSamba)
                 .Build());
    Register(Builder("enhanced_puzzle")
                 .Stages({"new", "asked", "done"})
                 .Option(VillagerNo(kYellowDrops, 0), Always(), kPuzzleLabel, TalkPuzzle)
                 .Build());
    Register(Builder("enhanced_pickle")
                 .Stages({"new", "open"})
                 .Option(VillagerNo(kBrownboo, 9), Always(), kPickleLabel, TalkPickle)
                 .Build());
    // Locked, the Mayor keeps his own words behind "Talk" (they hand out Norune's supplies), and his
    // quests' line with them; once open, his quest starts from "Talk".
    Register(Builder(std::string(kMayorId))
                 .Stages({"locked", "open", "active", "cleared", "finished"})
                 .Option(NoruneMayor(), AtStage("locked"), kMayorLabel, TalkMayor)
                 .TalkOption(NoruneMayor(), OnOffer({"open"}), TalkMayor)
                 .Option(NoruneMayor(), UnderWay({"active", "cleared", "finished"}), kAboutQuest, TalkMayor)
                 .Build());
    Register(Builder(std::string(kMintId))
                 .Stages({"new", "open", "rewarded"})
                 .TalkOption(VillagerNo(kBrownboo, kMintVillager), Always(), TalkMint)
                 .Build());
}

std::vector<Text> EnhancedQuestsTexts() {
    std::vector<Text> texts = {kNoRoom, kMachoIntro, kGobIntro, kJakeIntro, kBonkaIntro, kHuntLabel, kHuntGiven,
                               kHuntGoing, kHuntReward, kPikeIntro, kPaoIntro, kSamIntro, kDeviaIntro, kFishLabel,
                               kFishCatch, kFishSize, kFishCatchGoing, kFishSizeGoing, kFishReward, kRumourLabel,
                               kShaftTower, kChallengeLabel, kPuzzleLabel, kNoBoss, kSambaTell, kSambaWaiting,
                               kSambaReward, kSambaDone, kPuzzleTell, kPuzzleHint, kPuzzleSolved, kPuzzleDone,
                               kPickleLabel, kPickleIntro, kPickleCount, kPickleShort, kPickleKey, kPickleAll,
                               kMayorLabel, kMayorEarly, kMayorJourney, kMayorGenie, kMayorShaft, kMayorOpen,
                               kMayorTask, kMayorReward, kMayorMaxed, kMayorFinished, kAboutQuest, kSamMore,
                               kSamRewardMore, kSamSpecial, kMintIntro, kMintLeft, kMintReward, kMintHasBook, kMintLost};

    for (const Rumour &rumour : Rumours()) {
        texts.push_back(rumour.tell);
        texts.push_back(rumour.found);
        texts.push_back(rumour.waiting);
    }

    return texts;
}

} // namespace dc::quest

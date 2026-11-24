#include "enhanced_town.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "character.hpp"
#include "data/data_read.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "main.hpp"
#include "platform/paths.hpp"
#include "userstatus.hpp"

namespace dc::game::expanded {

namespace fs = std::filesystem;

namespace {

// ── Logging ────────────────────────────────────────────────────────────────────────────────────

fs::path LogDir() {
    std::error_code error;
    fs::path        dir = PathsSaveRoot() / "logs";
    fs::create_directories(dir, error);
    return dir;
}

void LogLine(std::string_view file, std::string_view line) {
    std::ofstream out(LogDir() / file, std::ios::app);
    out << line << '\n';
}

// Saves a file once a session: the first copy is the one the game read before anything changed it.
void SaveOnce(const std::string &name, const void *data, int size) {
    static std::set<std::string> saved;
    if (data == nullptr || size <= 0 || !saved.insert(name).second) {
        return;
    }
    std::ofstream out(LogDir() / name, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char *>(data), size);
}

// "dun/mainchara/c10b.chr" as "c10b".
std::string Stem(std::string_view path) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    const size_t dot = path.find('.');
    return std::string(path.substr(0, dot));
}

// Saves every config file in a character pack as <stem>_<name>.
void SavePackConfigs(const std::string &stem, u_int *pack) {
    std::array<u_int *, 8> files{};
    std::array<int, 8>     sizes{};
    std::array<char *, 8>  names{};
    char                   extension[] = "cfg";
    const int              count = GetPackFileExt(pack, extension, files.data(), 8, sizes.data(), names.data());
    for (int i = 0; i < count && i < 8; i++) {
        const std::string name = names[i] != nullptr ? names[i] : std::format("{}.cfg", i);
        SaveOnce(std::format("{}_{}", stem, name), files[i], sizes[i]);
    }
}

// Dark Cloud Expanded's ally town movesets and ladder climbs are clips taken from these models
// (IsoPatch/TownModelBakes.cs). Each is copied once into logs/models/, its folders kept, so the
// clips and skeletons can be read before the movesets are ported.
void DumpTownModels() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    constexpr std::array<const char *, 18> kModels = {
        "chara/c01d.chr",
        "gedit/e01/chara/c04pcat.chr",
        "gedit/e01/chara/e04c04cat.chr",
        "gedit/s86/chara/c04cat.chr",
        "dun/d01/event/e613c04cat.chr",
        "gedit/s01/chara/c06p.chr",
        "gedit/s01/chara/e101.chr",
        "gedit/s01/chara/e102.chr",
        "dun/mainchara/c06b.chr",
        "gedit/e03/chara/c05a.chr",
        "gedit/e03/chara/e223c05a.chr",
        "gedit/e03/chara/e228c05a.chr",
        "dun/mainchara/c05a.chr",
        "gedit/e04/chara/e323_2c10a.chr",
        "dun/mainchara/c10b.chr",
        "gedit/e05/chara/c18p.chr",
        "gedit/e05/chara/e403c18a.chr",
        "gedit/s13/chara/e402c18a.chr",
    };
    constexpr std::uintmax_t kSector = 2048;

    const fs::path root = LogDir() / "models";
    int            copied = 0;

    for (const char *model : kModels) {
        const std::uintmax_t size = dc::data::DataFileSize(model);
        if (size == 0) {
            LogLine("models.txt", std::format("{}: not in the game data", model));
            continue;
        }

        std::vector<char> buffer(static_cast<size_t>((size + kSector - 1) / kSector * kSector + kSector));
        int               read = 0;
        if (!dc::data::DataReadLoadFile(model, buffer.data(), &read) || read <= 0) {
            LogLine("models.txt", std::format("{}: could not be read", model));
            continue;
        }
        dc::data::DataReadForgetPackCapacities(buffer.data());

        std::error_code error;
        const fs::path  out = root / model;
        fs::create_directories(out.parent_path(), error);
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        file.write(buffer.data(), std::min<std::streamsize>(read, static_cast<std::streamsize>(size)));
        LogLine("models.txt", std::format("{}: {} bytes", model, size));
        copied++;
    }

    LogLine("models.txt", std::format("{} of {} models copied", copied, kModels.size()));
}

// ── Walking the town as an ally ────────────────────────────────────────────────────────────────

// The model each character walks the town in, as Enhanced loads them: the allies' own story
// cutscene models, which carry the town motions.
constexpr std::array<const char *, 6> kTownModels = {
    "chara/c01d.chr",
    "gedit/e01/chara/c04pcat.chr",
    "gedit/s01/chara/c06p.chr",
    "gedit/e03/chara/c05a.chr",
    "gedit/s79/chara/c10a.chr",
    "gedit/e05/chara/c18p.chr",
};

constexpr int kXiao = 1;
constexpr int kRuby = 3;

int g_ally = 0;
int g_pending = -1;

// Dark Cloud Expanded: an ally who goes fishing comes back once the session is over. The fishing
// event loads Toan's fishing model and its quit script loads Toan, so the ally is put back after.
int  g_fishing_ally = -1;
bool g_saw_fishing = false;

// Which of its two lines each villager says next, by its place in kAllyTalk.
std::set<int> g_second_line;

void LoadTownChara(int chara) {
    sceVu0FVECTOR position = {0.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR rotation = {0.0f, 0.0f, 0.0f, 0.0f};
    if (Chara != nullptr) {
        Chara->GetPosition(position);
        Chara->GetRotation(rotation);
    }

    CharaBuffer.used = 0;
    char info[] = "info.cfg";
    EdLoadMainChara(const_cast<char *>(kTownModels[static_cast<size_t>(chara)]), info, &CharaBuffer);
    Chara->SetPosition(position);
    Chara->SetRotation(rotation);
    g_ally = chara;
    g_second_line.clear();
    LogLine("town.txt", std::format("map {}: now walking as character {} ({})", MapNo, chara, kTownModels[static_cast<size_t>(chara)]));
}

// One of Enhanced's motion fixes: a numbers field of the config ("\t22,") written over where the
// mod's patch writes it, counted from the text the patch checks for. A field that is not where
// the patch expects is left alone, and every change is logged.
struct FieldFix {
    int  offset;
    char text[5];
};

void FixFields(const std::string &stem, char *cfg, int size, std::string_view anchor, std::initializer_list<FieldFix> fixes) {
    const std::string_view text(cfg, static_cast<size_t>(size));
    const size_t           at = text.find(anchor);
    if (at == std::string_view::npos) {
        LogLine("town.txt", std::format("{}: no \"{}\" in info.cfg, motion fix skipped", stem, anchor));
        return;
    }
    for (const FieldFix &fix : fixes) {
        const size_t where = at + static_cast<size_t>(fix.offset);
        if (where + 4 > text.size() || cfg[where] != '\t') {
            LogLine("town.txt", std::format("{}: field at +0x{:X} is not a field, left alone", stem, fix.offset));
            continue;
        }
        const std::string before(cfg + where, 4);
        std::memcpy(cfg + where, fix.text, 4);
        LogLine("town.txt", std::format("{}: +0x{:X} \"{}\" -> \"{}\"", stem, fix.offset, before, std::string_view(fix.text, 4)));
    }
}

} // namespace

int TownAlly() {
    return g_ally;
}

void TownAllySelect(int chara) {
    if (chara < 0 || chara >= static_cast<int>(kTownModels.size())) {
        return;
    }
    g_pending = chara;
}

void TownAllyMapLoaded() {
    // Dark Cloud Expanded: copies the moveset source models out once (see DumpTownModels).
    DumpTownModels();
    g_ally = 0;
    g_pending = -1;
    g_fishing_ally = -1;
    g_saw_fishing = false;
    g_second_line.clear();
}

void TownAllyStep() {
    LogTownEventStep();

    // Dark Cloud Expanded: allies can fish. An event that turned out to be fishing gives the ally
    // back once walking resumes; any other event leaves Toan, as before.
    if (g_fishing_ally > 0) {
        if (GameMode == ED_MODE_FISHING) {
            g_saw_fishing = true;
        } else if (GameMode == ED_MODE_WALK) {
            if (g_saw_fishing && g_pending < 0) {
                g_pending = g_fishing_ally;
                LogLine("town.txt", std::format("map {}: fishing over, character {} comes back", MapNo, g_fishing_ally));
            }

            g_fishing_ally = -1;
            g_saw_fishing = false;
        }
    }

    if (g_pending < 0 || GameMode != ED_MODE_WALK) {
        return;
    }
    const int chara = g_pending;
    g_pending = -1;
    if (chara != g_ally) {
        LoadTownChara(chara);
    }
}

void TownAllyEventStarted() {
    g_pending = -1;
    if (g_ally != 0 && GameMode == ED_MODE_EVENT) {
        g_fishing_ally = g_ally;
        g_saw_fishing = false;
        LoadTownChara(0);
    }
}

bool TownAllySkipsEventPoints() {
    if (g_ally == 0) {
        return false;
    }
    // The maps Enhanced turns event points off in while an ally walks them.
    switch (MapNo) {
        case 11:
        case 13:
        case 14:
        case 33:
        case 35:
        case 37:
            return true;
        default:
            return false;
    }
}

float TownEyeHeight() {
    return g_ally == kXiao ? 5.0f : 14.0f;
}

void TownCharaPackLoaded(const char *path, u_int *pack) {
    if (path == nullptr || pack == nullptr) {
        return;
    }
    const std::string stem = Stem(path);
    SavePackConfigs(stem, pack);

    int  size = 0;
    char info[] = "info.cfg";
    auto *cfg = reinterpret_cast<char *>(GetPackFile(pack, info, &size));
    if (cfg == nullptr || size <= 0) {
        return;
    }

    if (std::strstr(path, kTownModels[kXiao]) != nullptr) {
        // Xiao's run plays at a speed that fits her stride.
        FixFields(stem, cfg, size, "c04c", {
            {0x42, "\t0.6"},
        });
    } else if (std::strstr(path, kTownModels[kRuby]) != nullptr) {
        // Ruby's walk and run take the frames that loop cleanly.
        FixFields(stem, cfg, size, "5a01", {
            {0xEA,  "\t60,"},
            {0xEE,  "\t80,"},
            {0xF2,  "\t0.4"},
            {0x10C, "\t30,"},
            {0x110, "\t50,"},
        });
    }
}

const char *TownAllyTalk(int map_no, int villager, const std::string &model) {
    if (g_ally == 0) {
        return nullptr;
    }
    for (int i = 0; i < kAllyTalkCount; i++) {
        const AllyTalk &talk = kAllyTalk[i];
        if (talk.map_no != map_no || talk.ally != g_ally) {
            continue;
        }
        if (talk.villager >= 0 && talk.villager != villager) {
            continue;
        }
        if (talk.model_code[0] != '\0' && (model.size() < 3 || model.compare(1, 2, talk.model_code) != 0)) {
            continue;
        }
        const bool second = g_second_line.contains(i);
        if (second) {
            g_second_line.erase(i);
        } else {
            g_second_line.insert(i);
        }
        return second ? talk.second : talk.first;
    }
    return nullptr;
}

// ── Logging for the features still to come ────────────────────────────────────────────────────

void LogDungeonChara(int chara, u_int *pack) {
    static constexpr std::array<const char *, 6> kStems = {"c01d", "c04b", "c06b", "c05a", "c10b", "c18a"};
    if (chara < 0 || chara >= 6 || pack == nullptr) {
        return;
    }
    SavePackConfigs(kStems[static_cast<size_t>(chara)], pack);
}

void LogDungeonScript(int dungeon, const void *data, int size) {
    SaveOnce(std::format("d0{}_event.stb", dungeon + 1), data, size);
}

void LogDungeonTask() {
    static int last_task = -1;
    static int started = 0;
    static int frame = 0;
    frame++;

    const int  task = gameTask;
    const bool scripted = task >= GAME_TASK_SCRIPT_START && task <= GAME_TASK_SCRIPT_RESTART;
    const bool was_scripted = last_task >= GAME_TASK_SCRIPT_START && last_task <= GAME_TASK_SCRIPT_RESTART;

    if (task != last_task && (scripted || was_scripted)) {
        const int floor = UserStatus != nullptr ? UserStatus->cur_floor : -1;
        const int chara = UserStatus != nullptr ? UserStatus->cur_chara : -1;
        if (scripted && !was_scripted) {
            started = frame;
        }
        LogLine("events.txt", std::format("dungeon {} floor {} frame {}: task 0x{:X} -> 0x{:X}, script {} running {} mode {} chara {}{}",
                                          selectMapNo, floor + 1, frame, last_task, task, BtEventInfo.script_no,
                                          BtEventInfo.running_script_no, BtEventInfo.action_mode, chara,
                                          !scripted && was_scripted ? std::format(" (script ran {} frames)", frame - started) : std::string()));
    }
    last_task = task;
}

namespace {

int g_town_event = -1;
int g_town_event_frames = 0;

} // namespace

void LogTownEvent(int event_no, bool system) {
    g_town_event = event_no;
    g_town_event_frames = 0;
    LogLine("events.txt", std::format("town map {}: {} event {} started", MapNo, system ? "system" : "map", event_no));
}

void LogTownEventStep() {
    if (g_town_event < 0) {
        return;
    }
    if (GameMode == ED_MODE_EVENT) {
        g_town_event_frames++;
        return;
    }
    LogLine("events.txt", std::format("town map {}: event {} ended after {} frames", MapNo, g_town_event, g_town_event_frames));
    g_town_event = -1;
}

} // namespace dc::game::expanded

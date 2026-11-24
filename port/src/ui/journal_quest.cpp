#include "ui/journal_quest.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "gamepad.hpp"
#include "localize/localize.hpp"
#include "menu_draw.hpp"
#include "menuetc.hpp"
#include "quest/enhanced.hpp"
#include "snd.hpp"
#include "ui/framework.hpp"

namespace dc::ui::journal_quest {

using namespace dc::game;
using namespace dc::localize;

namespace {

constexpr int kListX = 92;
constexpr int kListTop = 124;
constexpr int kListWidth = 456;
constexpr int kLineSpacing = 22;
constexpr int kQuestSpacing = 50;
constexpr int kQuestsInView = 4;

constexpr const char *kControlsText = "{up} / {down} Scroll   {circle} Return";

std::vector<quest::TrackedQuest> s_quests;
int                              s_first = 0;

// A label narrowed to fit the page, so a long task never runs off its edge.
void DrawFitted(const std::string &text, u32 color, float scale, int x, int y) {
    ui::Label label(text, color, ui::Alignment::Left, true, scale);
    const int width = label.GetWidth();

    if (width > kListWidth) {
        label.SetXScale(static_cast<float>(kListWidth) / static_cast<float>(width));
    }

    label.Draw(x, y, 0x80);
}

int LastFirst() {
    return std::max(0, static_cast<int>(s_quests.size()) - kQuestsInView);
}

} // namespace

void Open() {
    s_quests = quest::EnhancedTrackedQuests();
    s_first = 0;
}

bool HandleKey() {
    if (GamePad.Down(PAD_CIRCLE)) {
        ComMenuSePlay(MENU_SOUND_REFUSE);
        return true;
    }

    if (GamePad.Down(PAD_UP) && s_first > 0) {
        s_first--;
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }

    if (GamePad.Down(PAD_DOWN) && s_first < LastFirst()) {
        s_first++;
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }

    return false;
}

void Draw() {
    constexpr float header_scale = 0.84f;
    constexpr float body_scale = 0.76f;

    ui::TrackerWindow::DrawPaper(64, 91, 512, 262, 0x80);
    ui::TrackerWindow::DrawStoneBanner(ui::Rect(150, 74, 340, 28), 0x80, 0x80,
                                       LocalizeText("quest_tracker.title", "JOURNAL: QUEST TRACKER"));

    MenuTextureReload(0x1A);

    if (s_quests.empty()) {
        ui::Label(LocalizeText("quest_tracker.none", "No quests under way."), ui::ColorWhite, ui::Alignment::Center, true,
                  header_scale)
            .Draw(320, 210, 0x80);
    }

    const int last = std::min(static_cast<int>(s_quests.size()), s_first + kQuestsInView);
    int       y = kListTop;

    for (int i = s_first; i < last; i++) {
        DrawFitted(s_quests[i].giver, ui::ColorYellow, header_scale, kListX, y);
        DrawFitted(s_quests[i].task, ui::ColorWhite, body_scale, kListX + 12, y + kLineSpacing);
        y += kQuestSpacing;
    }

    if (static_cast<int>(s_quests.size()) > kQuestsInView) {
        const std::string shown = LocalizeFormat(LocalizeText("quest_tracker.shown", "%1-%2 of %3"),
                                                 {std::to_string(s_first + 1), std::to_string(last),
                                                  std::to_string(s_quests.size())});
        ui::Label(shown, ui::ColorGold, ui::Alignment::Right, true, body_scale).Draw(548, 330, 0x80);
    }

    const std::string controls_text = LocalizeText("quest_tracker.controls", kControlsText);
    ui::Label(controls_text, ui::ColorWhite, ui::Alignment::Center).Draw(320, 418, 0x80);
}

void AppendStrings(std::vector<std::pair<std::string, std::string>> &out) {
    out.emplace_back("quest_tracker.title", "JOURNAL: QUEST TRACKER");
    out.emplace_back("quest_tracker.none", "No quests under way.");
    out.emplace_back("quest_tracker.shown", "%1-%2 of %3");
    out.emplace_back("quest_tracker.controls", kControlsText);
}

} // namespace dc::ui::journal_quest

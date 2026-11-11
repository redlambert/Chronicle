#include "expanded_text.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "clsmes.hpp"
#include "text/gametext.hpp"

namespace dc::game::expanded {

namespace {

/**
 * Whole-word swaps: a letter or digit may not touch either end of the match.
 */
constexpr std::pair<std::string_view, std::string_view> kFixes[] = {
    {"alot",                       "a lot"                     },
    {"Mikala",                     "Mikara"                    },
    {"Atlamilla",                  "Atlamillia"                },
    {"Bamirumba Hamorumba",        "Bamilumba Hamolumba"       },
    {"Skelton",                    "Skeleton"                  },
    {"auxilliary",                 "auxiliary"                 },
    {"warror",                     "warrior"                   },
    {"misson",                     "mission"                   },
    {"enternal",                   "eternal"                   },
    {"greif",                      "grief"                     },
    {"embarassment",               "embarrassment"             },
    {"whimpy",                     "wimpy"                     },
    {"whimp",                      "wimp"                      },
    {"and and",                    "and"                       },
    {"an fearsome",                "a fearsome"                },
    {"Your welcome",               "You're welcome"            },
    {"how's Tukkie is doing",      "how Tukkie is doing"       },
    {"That the best",              "That's the best"           },
    {"full.Can't",                 "full. Can't"               },
    {"another town.",              "another town?"             },
    {"the mystery of the town",    "the mystery of the town."  },
    {"for free of charge",         "free of charge"            },
    {"Mr.Moustache",               "Mr. Mustache"              },
    {"Moustache",                  "Mustache"                  },
    {"Stew",                       "Stu"                       },
    {"Marnia",                     "Mahnia"                    },
    {"Suger",                      "Sugar"                     },
    {"Yellow Drop",                "Yellow Drops"              },
    {"Sun & Moon temple",          "Sun & Moon Temple"         },
    {"Muska desert",               "Muska Desert"              },
    {"No! that's",                 "No! That's"                },
    {"Nemu",                       "Nem"                       },
    {"AncientBaron",               "Ancient Baron"             },
    {"Jibubu's Hose",              "Jibubu's House"            },
    {"3sisters' House",            "3 Sisters' House"          },
    {"LeaningTower",               "Leaning Tower"             },
    {"Kye&Momo's",                 "Kye & Momo's"              },
    {"Dike",                       "Pike"                      },
    {"Xena",                       "Gina"                      },
    {"Strage Guard",               "Storage Guard"             },
    {"Halbert",                    "Halberd"                   },
    {"Freshen Up Washery",         "Freshen Up Watery"         },
    {"Miracle Washery",            "Miracle Watery"            },
    {"Warrior'sWashery",           "Warrior's Watery"          },
    {"FreshenUp Watery",           "Freshen Up Watery"         },
    {"Magical Watery",             "Miracle Watery"            },
    {"Fighting Watery",            "Warrior's Watery"          },
};

/**
 * A message as last read, and its fixed text when that differs.
 */
struct FixedMessage {
    std::vector<s16> original;
    std::vector<s16> fixed;
};

std::unordered_map<const short *, FixedMessage> g_fixed;

bool IsWordChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0;
}

void ApplyFixes(std::string &text) {
    for (const auto &[from, to] : kFixes) {
        size_t at = 0;

        while ((at = text.find(from, at)) != std::string::npos) {
            const bool starts = at == 0 || !IsWordChar(text[at - 1]);
            const bool ends = at + from.size() >= text.size() || !IsWordChar(text[at + from.size()]);

            if (starts && ends) {
                text.replace(at, from.size(), to);
                at += to.size();
            } else {
                at += from.size();
            }
        }
    }
}

/**
 * The two spacing rules, for prose only: a double space between two glyphs becomes one unless
 * it follows a sentence's end, and a space between a letter and . , ? or ! goes, unless the
 * punctuation starts an ellipsis.
 */
void ApplySpacing(std::string &text) {
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); i++) {
        const char ch = text[i];

        if (ch == ' ' && i + 1 < text.size() && text[i + 1] == ' ' && i > 0 && text[i - 1] != ' ' &&
            i + 2 < text.size() && text[i + 2] != ' ') {
            const char before = text[i - 1];

            if (before != '.' && before != '!' && before != '?') {
                continue;
            }
        }

        if (ch == ' ' && i > 0 && std::isalpha(static_cast<unsigned char>(text[i - 1])) != 0 && i + 1 < text.size()) {
            const char next = text[i + 1];
            const bool punctuation = next == '.' || next == ',' || next == '?' || next == '!';
            const bool ellipsis = next == '.' && i + 2 < text.size() && text[i + 2] == '.';

            if (punctuation && !ellipsis) {
                continue;
            }
        }

        out += ch;
    }

    text = std::move(out);
}

} // namespace

short *FixText(short *text) {
    if (text == nullptr) {
        return nullptr;
    }

    constexpr size_t kMessageMax = 8192;

    size_t length = 0;

    while (length < kMessageMax && text[length] != MES_CODE_END) {
        length++;
    }

    if (length == kMessageMax) {
        return text;
    }

    FixedMessage &entry = g_fixed[text];

    if (entry.original.size() == length + 1 && std::equal(entry.original.begin(), entry.original.end(), text)) {
        return entry.fixed.empty() ? text : reinterpret_cast<short *>(entry.fixed.data());
    }

    entry.original.assign(text, text + length + 1);
    entry.fixed.clear();

    std::string decoded = text::GameTextDecode(entry.original.data());

    // Only a message that reads back exactly as it was written is touched.
    std::vector<s16> round_trip;

    if (text::GameTextEncode(decoded, round_trip) != 0 || round_trip != entry.original) {
        return text;
    }

    std::string fixed = decoded;
    ApplyFixes(fixed);

    // Values, icons and other codes mark tables and menus, whose spacing is alignment.
    if (fixed.find('{') == std::string::npos) {
        ApplySpacing(fixed);
    }

    if (fixed == decoded) {
        return text;
    }

    std::vector<s16> encoded;

    if (text::GameTextEncode(fixed, encoded) != 0) {
        return text;
    }

    entry.fixed = std::move(encoded);
    return reinterpret_cast<short *>(entry.fixed.data());
}

} // namespace dc::game::expanded

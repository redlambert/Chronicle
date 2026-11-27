#include "expanded_effects.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

#include "character.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "mds.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "rect.hpp"
#include "render/texture_registry.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "userstatus.hpp"

namespace dc::game::expanded::fx {

namespace {

// ── The borrowed effect models ──────────────────────────────────────────────────────────────

// Each prop is the game's own character pack, loaded as a character with its texture in a block
// of its own (0x32 to 0x37: the rock has 0x30, the cat 0x31, and no dungeon uses these). A prop
// keeps a few copies of its model, so several can play at once.
constexpr int kCopies = 6;
constexpr int kFirstBlock = 0x32;
constexpr int kFileBytes = 384 * 1024;
constexpr int kModelBytes = 768 * 1024;

struct PropSpec {
    const char *path;
    const char *config;
};

constexpr std::array<PropSpec, static_cast<int>(Prop::Count)> kSpecs = {{
    {"dun/effect/explosion.chr", "explosion.cfg"},
    {"gedit/s99/chara/lightning.chr", "lightning.cfg"},
    {"gedit/e03/chara/e228ex.chr", "e228ex.cfg"},
    {"gedit/s04/chara/e114ex.chr", "e114ex.cfg"},
    {"dun/monstor/c17_beem_s.chr", "info.cfg"},
    {"gedit/s78/chara/e508_ex.chr", "e508_ex.cfg"},
}};

struct Copy {
    CCharacter chara;
    bool       live = false;
    bool       loop = false;
    bool       fading = false;
    int        motion = -1;
    int        generation = 0;
    float      pos[3] = {};
    float      scale = 1.0f;
    float      yaw = 0.0f;
    float      rate = 1.0f;
};

struct PropModel {
    std::unique_ptr<u_long128[]> file;
    std::unique_ptr<u_long128[]> work;
    std::unique_ptr<u_long128[]> model;
    int                          file_size = 0;
    bool                         tried = false;
    int                          floor = -1; // the floor the model and texture were entered for
    bool                         ready = false;
    CDataAlloc2<1>               alloc;
    CCharacter                   source;
    std::array<Copy, kCopies>    copies;
};

std::array<PropModel, static_cast<int>(Prop::Count)> g_props;

// The prop whose pack is being entered right now, or -1. PaintTexture keys the beam's recolour on
// this rather than on the texture block alone: blocks 0x32 to 0x37 are free in the dungeons but
// not in town, where the windmill's NPC (the Mayor) and the other villagers load into 0x36 and up.
int g_entering_prop = -1;

int FloorKey() {
    return UserStatus != nullptr ? UserStatus->cur_floor + 100 * selectMapNo : -1;
}

/**
 * The prop's model for this floor: the file once a session, the model and its picture again on
 * each floor, whose texture blocks are rebuilt.
 */
bool Ready(int index) {
    PropModel      &prop = g_props[index];
    const PropSpec &spec = kSpecs[index];

    if (!prop.tried) {
        prop.tried = true;
        prop.file = std::make_unique<u_long128[]>(kFileBytes / 16);
        char path[64];
        std::snprintf(path, sizeof(path), "%s", spec.path);

        if (LoadFileBG(path, prop.file.get(), &prop.file_size) == 0 || prop.file_size <= 0 || prop.file_size > kFileBytes) {
            prop.file_size = 0;
        }

        std::printf("[Effects] %s %s (%d bytes)\n", spec.path, prop.file_size > 0 ? "loaded" : "NOT FOUND", prop.file_size);
    }

    if (prop.file_size <= 0) {
        return false;
    }

    const int floor = FloorKey();

    if (prop.floor == floor) {
        return prop.ready;
    }

    prop.floor = floor;
    prop.ready = false;

    for (Copy &copy : prop.copies) {
        copy.live = false;
    }

    if (!prop.work) {
        prop.work = std::make_unique<u_long128[]>(kFileBytes / 16);
        prop.model = std::make_unique<u_long128[]>(kModelBytes / 16);
    }

    // The pack is worked on in a copy, as the game loads every pack fresh from the disc.
    std::memcpy(prop.work.get(), prop.file.get(), static_cast<size_t>(prop.file_size));
    auto *pack = reinterpret_cast<u_int *>(prop.work.get());
    char  config[32];
    std::snprintf(config, sizeof(config), "%s", spec.config);

    const int block = kFirstBlock + index;
    TexManager.DeleteTextureBlock(block);
    prop.alloc.base = reinterpret_cast<u_char *>(prop.model.get());
    prop.alloc.buffer = prop.alloc.base;
    prop.alloc.used = 0;
    prop.alloc.limit = kModelBytes / 16;
    prop.source.Initialize();
    g_entering_prop = index;
    prop.source.LoadPackData3(pack, config, &prop.alloc, block, &prop.alloc, 1, 0x10);
    g_entering_prop = -1;

    if (prop.source.frame == nullptr) {
        std::printf("[Effects] %s model FAILED\n", spec.path);
        return false;
    }

    for (Copy &copy : prop.copies) {
        copy.chara = prop.source;
        copy.chara.motion[0] = &copy.chara.motion_type;
        copy.chara.frame = CopyFrameVu1(static_cast<CFrameVu1 *>(prop.source.frame), &prop.alloc);
    }

    prop.ready = true;
    std::printf("[Effects] %s ready (%d of %d quads)\n", spec.path, prop.alloc.used, prop.alloc.limit);
    return true;
}

/**
 * The motion number of a clip, or -1 when the pack has no such clip.
 */
int MotionOf(const PropModel &prop, int clip) {
    const int first = std::max(0, prop.source.motion_start[0]);
    const int last = prop.source.motion_end[0];
    const int motion = first + clip;

    if (clip < 0 || (last >= first && motion > last)) {
        return -1;
    }

    return motion;
}

void StartClip(PropModel &prop, Copy &copy, int clip, bool loop) {
    const int motion = MotionOf(prop, clip);

    if (motion < 0 || copy.chara.motion_type.motion_info == nullptr) {
        return;
    }

    const MOTION_INFO &info = copy.chara.motion_type.motion_info[motion];
    copy.motion = motion;
    copy.loop = loop;
    copy.chara.SetMotion(motion, loop ? 4 : 6, info.speed * copy.rate);
    copy.chara.motion_type.state.time = static_cast<float>(info.start);
}

Copy *Find(int handle) {
    if (handle < 0) {
        return nullptr;
    }

    const int index = (handle >> 4) & 0xF;
    const int slot = handle & 0xF;

    if (index >= static_cast<int>(Prop::Count) || slot >= kCopies) {
        return nullptr;
    }

    Copy &copy = g_props[index].copies[slot];
    return copy.live && copy.generation == (handle >> 8) ? &copy : nullptr;
}

// ── The scene's light ───────────────────────────────────────────────────────────────────────

constexpr float kDimKeep = 0.15f;    // of the floor's light left at full dim
constexpr float kDimFogPull = 0.35f; // the fog's range at full dim, of where it was
constexpr float kDimEase = 0.08f;    // how far a dim moves toward what is held, a step

float g_flash_light[3] = {};
float g_flash_fog[3] = {};
int   g_flash_steps = 0;
int   g_flash_length = 0;
float g_dim_held = 0.0f;
float g_dim = 0.0f;

float Flashing() {
    if (g_flash_steps <= 0 || g_flash_length <= 0) {
        return 0.0f;
    }

    // Eased back: slow at first, so the white holds a moment before the room returns.
    const float t = static_cast<float>(g_flash_steps) / static_cast<float>(g_flash_length);
    return t * t * (3.0f - 2.0f * t);
}

float Mix(float from, float to, float k) {
    return from + (to - from) * k;
}

// ── Tints ───────────────────────────────────────────────────────────────────────────────────

struct Tint {
    float rgb[3] = {};
    int   steps = 0;
    int   length = 0;
    bool  shown = false; // written to the model and not yet cleared
};

float                  g_weapon_add[3] = {};
float                  g_weapon_shown[3] = {};
float                  g_player_add[3] = {};
bool                   g_player_shown = false;
std::array<Tint, 16>   g_monster_tint;
float                  g_weapon_saved[3] = {};
bool                   g_weapon_drawn = false;

bool Any(const float *rgb) {
    return rgb[0] != 0.0f || rgb[1] != 0.0f || rgb[2] != 0.0f;
}

void SetOffset(CCharacter &chara, const float *rgb) {
    chara.ambient_offset[0] = rgb[0];
    chara.ambient_offset[1] = rgb[1];
    chara.ambient_offset[2] = rgb[2];
}


// ── Soft discs ──────────────────────────────────────────────────────────────────────────────

// A white disc whose opacity falls off smoothly to its rim, made once at run time and kept as a
// fixed texture: drawn dark it is a prop's round shadow on the floor (Expanded's GroundShadow),
// drawn bright and added it is a glow (Expanded's glow discs).
constexpr int kDiscSize = 64;
char          g_disc_name[] = "xp_softdisc";

CTexture *SoftDisc() {
    if (CTexture *texture = TexManager.GetTexture(g_disc_name, -1)) {
        return texture;
    }

    static std::array<u_char, kDiscSize * kDiscSize * 4> pixels;
    const float                                         half = kDiscSize / 2.0f;

    for (int y = 0; y < kDiscSize; y++) {
        for (int x = 0; x < kDiscSize; x++) {
            const float dx = (static_cast<float>(x) + 0.5f - half) / (half - 1.0f);
            const float dy = (static_cast<float>(y) + 0.5f - half) / (half - 1.0f);
            const float edge = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            const float soft = std::min(1.0f, edge * 2.0f);
            const float alpha = soft * soft * (3.0f - 2.0f * soft);
            u_char     *pixel = &pixels[static_cast<std::size_t>(y * kDiscSize + x) * 4];

            pixel[0] = 0xFF;
            pixel[1] = 0xFF;
            pixel[2] = 0xFF;
            pixel[3] = static_cast<u_char>(std::lround(alpha * 128.0f));
        }
    }

    // Filtered when it is drawn larger or smaller (the GS TEX1 MMAG and MMIN bits).
    constexpr u_long kLinear = (1 << 5) | (1 << 6);
    TexManager.EnterFixTexture(g_disc_name, pixels.data(), kDiscSize, kDiscSize, 4, nullptr, 0, 0, nullptr, nullptr, nullptr, kLinear,
                               0);
    return TexManager.GetTexture(g_disc_name, -1);
}

/**
 * Draws the disc with the blend set: (source - dest) x alpha + dest, or source x alpha + dest
 * when add is set, the depth tested but not written.
 */
void DiscBlend(bool add) {
    sceGsAlpha alpha = mgAlpha;
    alpha.bits.a = 0;
    alpha.bits.b = add ? 2 : 1;
    alpha.bits.c = 0;
    alpha.bits.d = 1;
    MGSetGsALPHA(&alpha);

    sceGsZbuf zbuf = mgZBuffer;
    zbuf.bits.zmsk = 1;
    MGSetGsZBUF(&zbuf);
}

void DiscRestore() {
    MGSetGsALPHA(nullptr);
    MGSetGsZBUF(nullptr);
}

// ── Painted textures ────────────────────────────────────────────────────────────────────────

// The ability icons are 20 x 20 tiles of the status windows' `charaface` sheet; Confuse takes
// the empty tile at (0xD8, 0xC8), beside Steal, where Expanded paints it (ConfuseAbilityBakes.cs).
constexpr int kIconTile = 20;
constexpr int kConfuseU = 0xD8;
constexpr int kConfuseV = 0xC8;

// Dark Cloud Expanded's Confuse icon (Resources/isoPatch/assets/confuse_icon.png): its colours,
// then each of its 20 x 20 pixels as a colour number.
constexpr std::array<std::array<u8, 4>, 28> kConfuseColours = {{
    {0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0xFF},
    {0x43, 0x39, 0x3A, 0xFF},
    {0xC5, 0xC5, 0xC4, 0xFF},
    {0xC2, 0xB9, 0xB4, 0xFF},
    {0xC7, 0xBE, 0xBB, 0xFF},
    {0xB3, 0xB0, 0xAE, 0xFF},
    {0xB1, 0xAA, 0xA8, 0xFF},
    {0xA7, 0xA7, 0xA7, 0xFF},
    {0x9B, 0x91, 0x8B, 0xFF},
    {0xBA, 0xB5, 0xB2, 0xFF},
    {0x99, 0x99, 0x9A, 0xFF},
    {0x76, 0x74, 0x76, 0xFF},
    {0x69, 0x4E, 0x95, 0xFF},
    {0x86, 0x86, 0xCC, 0xFF},
    {0x44, 0x38, 0x5C, 0xFF},
    {0x2E, 0x21, 0x40, 0xFF},
    {0xCF, 0xCC, 0xCD, 0xFF},
    {0xE3, 0xDF, 0xE4, 0xFF},
    {0xF6, 0xF5, 0xF4, 0xFF},
    {0x8F, 0x8E, 0x8F, 0xFF},
    {0x18, 0x12, 0x13, 0xFF},
    {0xAE, 0xA6, 0xA0, 0xFF},
    {0x8F, 0x8A, 0x87, 0xFF},
    {0x84, 0x79, 0x72, 0xFF},
    {0x6E, 0x62, 0x5C, 0xFF},
    {0x77, 0x6D, 0x67, 0xFF},
    {0x5F, 0x5D, 0x5F, 0xFF},
}};

// clang-format off
constexpr std::array<u8, kIconTile * kIconTile> kConfusePixels = {
     0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,
     1,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  4,  2,  1,
     1,  3,  5,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  6,  8,  9,  1,
     1, 10,  6,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8, 11, 12,  1,
     1, 10,  6,  8,  8,  8,  8, 13, 14, 14, 13, 13, 15,  8,  8,  8,  8, 11, 12,  1,
     1, 10,  6,  8,  8, 15, 13, 16, 16, 16, 16, 16, 16, 15, 15,  8,  8, 11, 12,  1,
     1, 10, 15,  8, 15, 16, 16, 17, 17, 17, 17, 17, 17, 16, 16, 15,  8, 15, 12,  1,
     1, 10, 16, 15, 16, 17, 18, 18, 19, 14, 13, 18, 18, 18, 17, 16, 15, 16, 12,  1,
     1, 10, 20, 16, 18, 18, 18, 18, 19, 14, 16, 18, 18, 18, 18, 18, 16, 12, 12,  1,
     1, 10,  6, 21, 17, 17, 18, 18, 18, 16, 16, 18, 18, 17, 17, 22, 21, 11, 12,  1,
     1, 10, 16, 15, 16, 22, 17, 17, 17, 13, 13, 17, 17, 17, 22, 16, 15, 16, 12,  1,
     1, 10, 15, 20, 15, 16, 16, 22, 22, 22, 22, 22, 22, 16, 16, 15, 12, 15, 12,  1,
     1, 10,  6,  8, 20, 15, 15, 16, 16, 16, 16, 16, 16, 15, 15, 12, 20, 11, 12,  1,
     1, 10,  6,  8,  8, 20, 12, 15, 13, 13, 13, 15, 15, 12, 12, 20,  8, 11, 12,  1,
     1, 10,  6,  8,  8,  8,  8, 20, 20, 12, 12, 12, 12, 20,  8,  8,  8, 11, 12,  1,
     1, 10,  6,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8, 11, 12,  1,
     1, 10,  6,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8, 11, 12,  1,
     1,  8, 11, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 23, 24, 25,  1,
     1,  2, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,  2,  1,
     0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,
};
// clang-format on

/**
 * The palette entry nearest a colour, for a sheet kept as colour numbers.
 */
int NearestColour(const render::PortDecodedTexture &texture, const std::array<u8, 4> &rgba) {
    const int colours = texture.four_bit ? 16 : 256;
    int       best = 0;
    long      best_distance = -1;

    for (int i = 0; i < colours; i++) {
        const std::uint32_t entry = texture.palette[static_cast<std::size_t>(i)];
        const long          dr = static_cast<long>(entry & 0xFF) - rgba[0];
        const long          dg = static_cast<long>((entry >> 8) & 0xFF) - rgba[1];
        const long          db = static_cast<long>((entry >> 16) & 0xFF) - rgba[2];
        const long          da = static_cast<long>((entry >> 24) & 0xFF) - rgba[3];
        const long          distance = dr * dr + dg * dg + db * db + da * da * 4;

        if (best_distance < 0 || distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }

    return best;
}

void PaintConfuseIcon(render::PortDecodedTexture &texture) {
    if (texture.levels.empty() || texture.width < kConfuseU + kIconTile || texture.height < kConfuseV + kIconTile) {
        return;
    }

    std::vector<std::uint8_t> &pixels = texture.levels[0];
    const bool                 indexed = texture.format == gfx::TextureFormat::Index8;
    const std::size_t          size = indexed ? 1 : 4;

    if (pixels.size() < static_cast<std::size_t>(texture.width) * texture.height * size) {
        return;
    }

    for (int y = 0; y < kIconTile; y++) {
        for (int x = 0; x < kIconTile; x++) {
            const std::array<u8, 4> &rgba = kConfuseColours[kConfusePixels[static_cast<std::size_t>(y * kIconTile + x)]];
            const std::size_t        at = (static_cast<std::size_t>(kConfuseV + y) * texture.width + kConfuseU + x) * size;

            if (indexed) {
                pixels[at] = static_cast<std::uint8_t>(NearestColour(texture, rgba));
                continue;
            }

            for (std::size_t c = 0; c < 4; c++) {
                pixels[at + c] = rgba[c];
            }
        }
    }

    std::printf("[Effects] Confuse icon painted into charaface (%ux%u, %s)\n", texture.width, texture.height,
                indexed ? "colour numbers" : "true colour");
}

// Expanded's Babel's Spear beam: the Dark Genie's pink beam with red and green exchanged (light
// blue) at 0.3 of its opacity (BorrowedShotBakes.cs). Only the prop's own copy is recoloured, so
// the genie's beam keeps its pink.
constexpr float kBeamOpacity = 0.3f;

void RecolourBeamPixel(std::uint8_t *rgba) {
    std::swap(rgba[0], rgba[1]);
    rgba[3] = static_cast<std::uint8_t>(std::lround(rgba[3] * kBeamOpacity));
}

void RecolourBeam(render::PortDecodedTexture &texture) {
    if (texture.format == gfx::TextureFormat::Index8) {
        for (std::uint32_t &entry : texture.palette) {
            std::uint8_t rgba[4] = {static_cast<std::uint8_t>(entry), static_cast<std::uint8_t>(entry >> 8),
                                    static_cast<std::uint8_t>(entry >> 16), static_cast<std::uint8_t>(entry >> 24)};
            RecolourBeamPixel(rgba);
            entry = static_cast<std::uint32_t>(rgba[0]) | static_cast<std::uint32_t>(rgba[1]) << 8 |
                    static_cast<std::uint32_t>(rgba[2]) << 16 | static_cast<std::uint32_t>(rgba[3]) << 24;
        }

        return;
    }

    for (std::vector<std::uint8_t> &level : texture.levels) {
        for (std::size_t at = 0; at + 3 < level.size(); at += 4) {
            RecolourBeamPixel(&level[at]);
        }
    }
}

} // namespace

int Play(Prop prop, int clip, const float *at, float scale, float yaw, bool loop, float rate) {
    const int index = static_cast<int>(prop);

    if (index < 0 || index >= static_cast<int>(Prop::Count) || !Ready(index)) {
        return -1;
    }

    PropModel &model = g_props[index];

    for (int slot = 0; slot < kCopies; slot++) {
        Copy &copy = model.copies[slot];

        if (copy.live) {
            continue;
        }

        copy.live = true;
        copy.fading = false;
        copy.generation = (copy.generation + 1) & 0xFFFF;
        copy.pos[0] = at[0];
        copy.pos[1] = at[1];
        copy.pos[2] = at[2];
        copy.scale = scale;
        copy.yaw = yaw;
        copy.rate = rate;
        copy.chara.fade_out = 0;
        copy.chara.fade[0] = 1.0f;
        copy.chara.fade[3] = 0.0f;
        StartClip(model, copy, clip, loop);
        return (copy.generation << 8) | (index << 4) | slot;
    }

    return -1;
}

void Move(int handle, const float *at) {
    if (Copy *copy = Find(handle)) {
        copy->pos[0] = at[0];
        copy->pos[1] = at[1];
        copy->pos[2] = at[2];
    }
}

void Clip(int handle, int clip, bool loop) {
    if (Copy *copy = Find(handle)) {
        StartClip(g_props[(handle >> 4) & 0xF], *copy, clip, loop);
    }
}

void Resize(int handle, float scale) {
    if (Copy *copy = Find(handle)) {
        copy->scale = scale;
    }
}

void Fade(int handle) {
    if (Copy *copy = Find(handle); copy != nullptr && !copy->fading) {
        copy->fading = true;
        copy->chara.fade[3] = 0.0f;
        copy->chara.fade_out = 1;
    }
}

void End(int handle) {
    if (Copy *copy = Find(handle)) {
        copy->live = false;
    }
}

bool Live(int handle) {
    return Find(handle) != nullptr;
}

void Flash(const float *light, const float *fog, int ease_steps) {
    for (int i = 0; i < 3; i++) {
        g_flash_light[i] = light[i];
        g_flash_fog[i] = fog[i];
    }

    g_flash_length = std::max(1, ease_steps);
    g_flash_steps = g_flash_length;
}

void Dim(float level) {
    g_dim_held = std::max(g_dim_held, std::clamp(level, 0.0f, 1.0f));
}

void WeaponTint(const float *rgb) {
    for (int i = 0; i < 3; i++) {
        g_weapon_add[i] += rgb[i];
    }
}

void PlayerTint(const float *rgb) {
    for (int i = 0; i < 3; i++) {
        g_player_add[i] += rgb[i];
    }
}

void MonsterTint(int m, const float *rgb, int steps) {
    if (m < 0 || m >= 16 || steps <= 0) {
        return;
    }

    Tint &tint = g_monster_tint[m];

    for (int i = 0; i < 3; i++) {
        tint.rgb[i] = rgb[i];
    }

    tint.steps = steps;
    tint.length = steps;
}

void Step() {
    // The flash eases back; a dim moves toward what was held this step and lets go when nothing
    // holds it.
    if (g_flash_steps > 0) {
        g_flash_steps--;
    }

    if (g_dim < g_dim_held) {
        g_dim = std::min(g_dim_held, g_dim + kDimEase);
    } else {
        g_dim = std::max(g_dim_held, g_dim - kDimEase);
    }

    g_dim_held = 0.0f;

    // The weapon's tint is drawn from what this step asked for.
    for (int i = 0; i < 3; i++) {
        g_weapon_shown[i] = g_weapon_add[i];
        g_weapon_add[i] = 0.0f;
    }

    // The active character's tint, cleared once when it ends.
    if (Any(g_player_add)) {
        SetOffset(CharaMain, g_player_add);
        g_player_shown = true;
    } else if (g_player_shown) {
        const float none[3] = {};
        SetOffset(CharaMain, none);
        g_player_shown = false;
    }

    for (float &channel : g_player_add) {
        channel = 0.0f;
    }

    // The enemies' tints fade out over their length.
    for (int m = 0; m < 16; m++) {
        Tint &tint = g_monster_tint[m];

        if (tint.steps <= 0 && !tint.shown) {
            continue;
        }

        if (NowMonstorUnit == nullptr) {
            tint = {};
            continue;
        }

        float rgb[3] = {};

        if (tint.steps > 0) {
            const float k = static_cast<float>(tint.steps) / static_cast<float>(tint.length);

            for (int i = 0; i < 3; i++) {
                rgb[i] = tint.rgb[i] * k;
            }

            tint.steps--;
            tint.shown = true;
        } else {
            tint.shown = false;
        }

        for (CCharacter &part : NowMonstorUnit->chara[m]) {
            SetOffset(part, rgb);
        }
    }

    // A fading prop ends once it is gone.
    for (PropModel &prop : g_props) {
        for (Copy &copy : prop.copies) {
            if (copy.live && copy.fading && copy.chara.fade[0] <= 0.01f) {
                copy.live = false;
            }
        }
    }
}

void Draw() {
    if (UserStatus == nullptr) {
        return;
    }

    const int floor = FloorKey();

    for (int index = 0; index < static_cast<int>(Prop::Count); index++) {
        PropModel &prop = g_props[index];

        if (!prop.ready || prop.floor != floor) {
            continue;
        }

        bool reloaded = false;

        for (Copy &copy : prop.copies) {
            if (!copy.live) {
                continue;
            }

            if (!reloaded) {
                TexManager.ReloadTexture(Vif1Packet, kFirstBlock + index);
                reloaded = true;
            }

            const float size = std::max(0.01f, copy.scale);
            copy.chara.SetPosition(copy.pos[0], copy.pos[1], copy.pos[2]);
            copy.chara.SetRotation(0.0f, copy.yaw, 0.0f);
            copy.chara.SetScale(size, size, size);
            copy.chara.Step();
            copy.chara.Draw();

            // A clip that plays once ends with its last frame.
            if (!copy.loop && copy.motion >= 0 && copy.chara.motion_type.motion_info != nullptr) {
                const float end = static_cast<float>(copy.chara.motion_type.motion_info[copy.motion].end);

                if (copy.chara.motion_type.state.time >= end - 1.0f) {
                    copy.live = false;
                }
            }
        }
    }
}

void SceneLight() {
    const float flash = Flashing();
    const float dim = g_dim;

    if (flash <= 0.0f && dim <= 0.0f) {
        return;
    }

    const bool  sub = lightingMode != 0;
    const float *rate = sub ? sub_fogRate : main_fogRate;
    const u8    *fog_colour = sub ? sub_fogColor : main_fogColor;
    const float keep = 1.0f - (1.0f - kDimKeep) * dim;

    // The ambient and the lights' colours: washed toward the flash's light, then darkened.
    sceVu0FVECTOR ambient;
    MGGetAmbient(ambient);

    for (int i = 0; i < 3; i++) {
        ambient[i] = Mix(ambient[i], g_flash_light[i], flash) * keep;
    }

    MGSetAmbient(ambient);

    sceVu0FMATRIX direction;
    sceVu0FMATRIX colour;
    MGGetPLight(direction, colour);

    for (int row = 0; row < 3; row++) {
        for (int i = 0; i < 3; i++) {
            colour[row][i] = Mix(colour[row][i], g_flash_light[i] / 128.0f, flash) * keep;
        }
    }

    MGSetPLight(direction, colour);

    // The fog: pulled in to nothing at the flash's peak, in the flash's colour; pulled closer and
    // toward black as the room dims.
    float near_z = Mix(rate[0], 0.0f, flash);
    float far_z = Mix(rate[1], 1.0f, flash);
    float fog[3];

    for (int i = 0; i < 3; i++) {
        fog[i] = Mix(static_cast<float>(fog_colour[i]), g_flash_fog[i], flash) * keep;
    }

    near_z = Mix(near_z, near_z * kDimFogPull, dim);
    far_z = Mix(far_z, far_z * kDimFogPull, dim);

    const auto channel = [](float value) {
        return static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
    };

    MGSetFogParm(near_z, far_z, channel(fog[0]), channel(fog[1]), channel(fog[2]), rate[2], rate[3]);
    MGSetBGColor(fog[0], fog[1], fog[2], 128.0f);
}

void WeaponDraw(bool begin) {
    if (NowWeapon == nullptr) {
        return;
    }

    if (begin) {
        g_weapon_drawn = Any(g_weapon_shown);

        if (!g_weapon_drawn) {
            return;
        }

        for (int i = 0; i < 3; i++) {
            g_weapon_saved[i] = NowWeapon->ambient_offset[i];
            NowWeapon->ambient_offset[i] += g_weapon_shown[i];
        }

        return;
    }

    if (g_weapon_drawn) {
        SetOffset(*NowWeapon, g_weapon_saved);
        g_weapon_drawn = false;
    }
}

void GroundShadow(float x, float ground, float z, float radius, float darkness) {
    CTexture *disc = SoftDisc();

    if (disc == nullptr || radius <= 0.0f || darkness <= 0.0f) {
        return;
    }

    // Lifted off the floor's surface, as Expanded's is.
    constexpr float kLift = 0.3f;

    const float y = ground + kLift;
    float       corner[4][4] = {
        {x - radius, y, z - radius, 1.0f},
        {x + radius, y, z - radius, 1.0f},
        {x - radius, y, z + radius, 1.0f},
        {x + radius, y, z + radius, 1.0f},
    };
    int screen[4][4];

    for (int i = 0; i < 4; i++) {
        if (MGRotTransPers(screen[i], corner[i], 0) == 0) {
            return;
        }
    }

    DiscBlend(false);
    spRGBA colour = {0, 0, 0, static_cast<u8>(std::lround(std::clamp(darkness, 0.0f, 1.0f) * 128.0f))};
    set3DSprite(Vif1Packet, disc, CRect_i_(0, 0, kDiscSize, kDiscSize), screen[0], screen[1], screen[2], screen[3], &colour);
    DiscRestore();
}

void GlowDisc(const float *at, float radius, const float *rgb, float strength) {
    CTexture *disc = SoftDisc();

    if (disc == nullptr || at == nullptr || radius <= 0.0f || strength <= 0.0f) {
        return;
    }

    float centre[4] = {at[0], at[1], at[2], 1.0f};
    int   top_left[4];
    int   bottom_right[4];

    if (MGRotTransPers3DSprite(top_left, bottom_right, centre, 2.0f * radius, 2.0f * radius, 0) != 1) {
        return;
    }

    const auto level = [&](int c) {
        return static_cast<u8>(std::clamp(std::lround(rgb[c] / 2.0f), 0L, 255L));
    };

    DiscBlend(true);
    spRGBA colour = {level(0), level(1), level(2), static_cast<u8>(std::lround(std::clamp(strength, 0.0f, 1.0f) * 128.0f))};
    set3DSprite(Vif1Packet, disc, CRect_i_(0, 0, kDiscSize, kDiscSize), top_left, bottom_right, &colour);
    DiscRestore();
}

void PaintTexture(const char *name, int block, render::PortDecodedTexture &texture) {
    if (name == nullptr) {
        return;
    }

    const std::string_view texture_name(name);

    if (texture_name == "charaface") {
        PaintConfuseIcon(texture);
        return;
    }

    // Only the beam prop's own pack. Town villagers share block 0x36 and would come out with red
    // and green swapped at 0.3 opacity (the green, see-through Mayor).
    if (g_entering_prop == static_cast<int>(Prop::Beam) && block == kFirstBlock + g_entering_prop) {
        RecolourBeam(texture);
    }
}

void Reset() {
    for (PropModel &prop : g_props) {
        for (Copy &copy : prop.copies) {
            copy.live = false;
        }
    }

    g_flash_steps = 0;
    g_dim = 0.0f;
    g_dim_held = 0.0f;

    for (Tint &tint : g_monster_tint) {
        tint = {};
    }

    if (g_player_shown) {
        const float none[3] = {};
        SetOffset(CharaMain, none);
        g_player_shown = false;
    }
}

} // namespace dc::game::expanded::fx

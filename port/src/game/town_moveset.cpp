#include "town_moveset.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace dc::game::moveset {

namespace {

// ---------------------------------------------------------------------------------------------
// Bytes
// ---------------------------------------------------------------------------------------------

std::uint32_t U32(const Bytes &b, std::size_t at) {
    if (at + 4 > b.size()) {
        throw std::runtime_error(std::format("read past the end at 0x{:X}", at));
    }

    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

void PutU32(Bytes &b, std::size_t at, std::uint32_t v) {
    std::memcpy(b.data() + at, &v, 4);
}

float F32(const Bytes &b, std::size_t at) {
    float v = 0.0f;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

void AppendU32(Bytes &b, std::uint32_t v) {
    const std::size_t at = b.size();
    b.resize(at + 4);
    PutU32(b, at, v);
}

std::string NameAt(const Bytes &b, std::size_t at, std::size_t length) {
    std::string name;

    for (std::size_t i = 0; i < length && at + i < b.size() && b[at + i] != 0; i++) {
        name.push_back(static_cast<char>(b[at + i]));
    }

    return name;
}

std::string Lower(std::string_view s) {
    std::string out(s);

    for (char &c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return out;
}

std::string Basename(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// ---------------------------------------------------------------------------------------------
// Packs (.chr): records of {name[64], data offset, size, stride, tag} each followed by its data
// ---------------------------------------------------------------------------------------------

struct Record {
    std::string name;
    std::size_t data_off = 0;
    std::size_t size = 0;
    Bytes       raw; // the whole record: header, data and padding

    Bytes Payload() const {
        return Bytes(raw.begin() + static_cast<std::ptrdiff_t>(data_off),
                     raw.begin() + static_cast<std::ptrdiff_t>(data_off + size));
    }

    void ReplacePayload(const Bytes &payload) {
        const std::size_t stride = (data_off + payload.size() + 15) & ~static_cast<std::size_t>(15);
        Bytes             next(stride, 0);
        std::copy_n(raw.begin(), data_off, next.begin());
        std::copy(payload.begin(), payload.end(), next.begin() + static_cast<std::ptrdiff_t>(data_off));
        PutU32(next, 0x44, static_cast<std::uint32_t>(payload.size()));
        PutU32(next, 0x48, static_cast<std::uint32_t>(stride));
        raw = std::move(next);
        size = payload.size();
    }
};

struct Pack {
    std::vector<Record> records;
    Bytes               trailer;

    Record *Find(std::string_view name) {
        const std::string wanted = Lower(Basename(name));

        for (Record &record : records) {
            if (Lower(record.name) == wanted) {
                return &record;
            }
        }

        return nullptr;
    }

    Record &Require(std::string_view name) {
        Record *record = Find(name);

        if (record == nullptr) {
            throw std::runtime_error(std::format("no {} in the pack", name));
        }

        return *record;
    }
};

Pack ParsePack(const Bytes &blob) {
    Pack        pack;
    std::size_t p = 0;

    while (p + 0x50 <= blob.size() && blob[p] != 0) {
        const std::size_t data_off = U32(blob, p + 0x40);
        const std::size_t size = U32(blob, p + 0x44);
        const std::size_t stride = U32(blob, p + 0x48);

        if (stride == 0 || p + stride > blob.size() || data_off + size > stride) {
            break;
        }

        Record record;
        record.name = NameAt(blob, p, 0x40);
        record.data_off = data_off;
        record.size = size;
        record.raw.assign(blob.begin() + static_cast<std::ptrdiff_t>(p), blob.begin() + static_cast<std::ptrdiff_t>(p + stride));
        pack.records.push_back(std::move(record));
        p += stride;
    }

    pack.trailer.assign(blob.begin() + static_cast<std::ptrdiff_t>(p), blob.end());
    return pack;
}

Bytes RebuildPack(const Pack &pack) {
    Bytes out;

    for (const Record &record : pack.records) {
        out.insert(out.end(), record.raw.begin(), record.raw.end());
    }

    out.insert(out.end(), pack.trailer.begin(), pack.trailer.end());
    return out;
}

// ---------------------------------------------------------------------------------------------
// Motion files (.mot): tracks of {joint, target, channel, ..., key count, next offset, tags} with
// 32-byte keys {frame, ..., value x4}. Channel 0 is a rotation quaternion (w, x, y, z), 2 a
// translation.
// ---------------------------------------------------------------------------------------------

using Vec4 = std::array<double, 4>;

struct Key {
    std::array<std::uint8_t, 32> raw{};

    std::uint32_t Frame() const {
        std::uint32_t v = 0;
        std::memcpy(&v, raw.data(), 4);
        return v;
    }

    void SetFrame(std::uint32_t frame) {
        std::memcpy(raw.data(), &frame, 4);
    }

    Vec4 Value() const {
        Vec4 v{};

        for (int i = 0; i < 4; i++) {
            float f = 0.0f;
            std::memcpy(&f, raw.data() + 0x10 + i * 4, 4);
            v[i] = f;
        }

        return v;
    }

    void SetValue(const Vec4 &v) {
        for (int i = 0; i < 4; i++) {
            const float f = static_cast<float>(v[i]);
            std::memcpy(raw.data() + 0x10 + i * 4, &f, 4);
        }
    }
};

Key NewKey(std::uint32_t frame, const Vec4 &v) {
    Key key;
    key.SetFrame(frame);
    key.SetValue(v);
    return key;
}

constexpr std::uint32_t kTagW6 = 0x74700000;
constexpr std::uint32_t kTagW7 = 0x747BFE95;

struct Track {
    std::uint32_t    w0 = 0; // joint
    std::uint32_t    w1 = 0;
    std::uint32_t    w2 = 0; // channel
    std::uint32_t    w3 = 0;
    std::uint32_t    w6 = 0;
    std::uint32_t    w7 = 0;
    std::vector<Key> keys;

    bool HasKeysIn(std::uint32_t lo, std::uint32_t hi) const {
        return std::ranges::any_of(keys, [&](const Key &k) { return lo <= k.Frame() && k.Frame() <= hi; });
    }

    void Sort() {
        std::ranges::stable_sort(keys, [](const Key &a, const Key &b) { return a.Frame() < b.Frame(); });
    }
};

struct Mot {
    std::vector<Track> tracks;

    Track *TrackBy(std::uint32_t joint, std::uint32_t channel) {
        for (Track &t : tracks) {
            if (t.w0 == joint && t.w2 == channel) {
                return &t;
            }
        }

        return nullptr;
    }
};

Mot ParseMot(const Bytes &data) {
    Mot         mot;
    std::size_t p = 0;

    while (p + 0x20 <= data.size()) {
        Track t;
        t.w0 = U32(data, p);
        t.w1 = U32(data, p + 4);
        t.w2 = U32(data, p + 8);
        t.w3 = U32(data, p + 12);
        const std::uint32_t count = U32(data, p + 16);
        const std::uint32_t more = U32(data, p + 20);
        t.w6 = U32(data, p + 24);
        t.w7 = U32(data, p + 28);
        p += 0x20;

        if (p + static_cast<std::size_t>(count) * 32 > data.size()) {
            throw std::runtime_error("a motion track runs past the end of its file");
        }

        for (std::uint32_t i = 0; i < count; i++) {
            Key key;
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(p), 32, key.raw.begin());
            t.keys.push_back(key);
            p += 32;
        }

        mot.tracks.push_back(std::move(t));

        if (more == 0) {
            break;
        }
    }

    return mot;
}

Bytes BuildMot(const Mot &mot) {
    Bytes out;

    for (std::size_t i = 0; i < mot.tracks.size(); i++) {
        const Track &t = mot.tracks[i];
        const bool   last = i + 1 == mot.tracks.size();
        AppendU32(out, t.w0);
        AppendU32(out, t.w1);
        AppendU32(out, t.w2);
        AppendU32(out, t.w3);
        AppendU32(out, static_cast<std::uint32_t>(t.keys.size()));
        AppendU32(out, last ? 0u : static_cast<std::uint32_t>(0x20 + t.keys.size() * 32));
        AppendU32(out, t.w6);
        AppendU32(out, t.w7);

        for (const Key &key : t.keys) {
            out.insert(out.end(), key.raw.begin(), key.raw.end());
        }
    }

    return out;
}

Mot ReadMot(Pack &pack, const std::string &name) {
    return ParseMot(pack.Require(name).Payload());
}

void WriteMot(Pack &pack, const std::string &name, const Mot &mot) {
    pack.Require(name).ReplacePayload(BuildMot(mot));
}

// The interpolated value at frame: a lerp for translations, a sign-corrected nlerp for rotations.
std::optional<Vec4> SampleTrack(const Track &t, int frame) {
    const Key *prev = nullptr;
    const Key *next = nullptr;

    for (const Key &k : t.keys) {
        const int f = static_cast<int>(k.Frame());

        if (f <= frame && (prev == nullptr || f > static_cast<int>(prev->Frame()))) {
            prev = &k;
        }

        if (f >= frame && (next == nullptr || f < static_cast<int>(next->Frame()))) {
            next = &k;
        }
    }

    if (prev == nullptr && next == nullptr) {
        return std::nullopt;
    }

    if (prev == nullptr) {
        return next->Value();
    }

    if (next == nullptr || next->Frame() == prev->Frame()) {
        return prev->Value();
    }

    const double u = static_cast<double>(frame - static_cast<int>(prev->Frame())) /
                     static_cast<double>(static_cast<int>(next->Frame()) - static_cast<int>(prev->Frame()));
    const Vec4 a = prev->Value();
    Vec4       b = next->Value();
    Vec4       v{};

    if (t.w2 == 0) {
        double dot = 0.0;

        for (int i = 0; i < 4; i++) {
            dot += a[i] * b[i];
        }

        if (dot < 0.0) {
            for (double &x : b) {
                x = -x;
            }
        }

        double n = 0.0;

        for (int i = 0; i < 4; i++) {
            v[i] = a[i] + (b[i] - a[i]) * u;
            n += v[i] * v[i];
        }

        n = std::sqrt(n);

        if (n == 0.0) {
            n = 1.0;
        }

        for (double &x : v) {
            x /= n;
        }

        return v;
    }

    for (int i = 0; i < 4; i++) {
        v[i] = a[i] + (b[i] - a[i]) * u;
    }

    return v;
}

// ---------------------------------------------------------------------------------------------
// Scenes (.mds): {count @0x08, stride @0x14}, then a node per stride from 0x18: {name[32], mesh
// offset, parent, bind matrix, id, 0x70}
// ---------------------------------------------------------------------------------------------

std::vector<std::string> MdsFrames(const Bytes &mds) {
    const std::uint32_t      count = U32(mds, 0x08);
    const std::uint32_t      stride = U32(mds, 0x14);
    std::vector<std::string> names;

    for (std::uint32_t i = 0; i < count; i++) {
        names.push_back(NameAt(mds, 0x18 + static_cast<std::size_t>(i) * stride, 0x20));
    }

    return names;
}

struct Bind {
    int                                 parent = -1;
    std::array<std::array<double, 3>, 3> rows{};
    std::array<double, 3>               translation{};
};

Bind MdsBind(const Bytes &mds, int i) {
    const std::size_t p = 0x18 + static_cast<std::size_t>(i) * 0x70;
    Bind              bind;
    bind.parent = static_cast<int>(U32(mds, p + 0x24));
    std::array<double, 16> m{};

    for (int k = 0; k < 16; k++) {
        m[k] = F32(mds, p + 0x28 + static_cast<std::size_t>(k) * 4);
    }

    bind.rows = {{{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {m[8], m[9], m[10]}}};
    bind.translation = {m[12], m[13], m[14]};
    return bind;
}

// 3x3 to quaternion (w, x, y, z), the inverse of the engine's quaternion to matrix.
Vec4 MatToQuat(const std::array<std::array<double, 3>, 3> &r) {
    const double m00 = r[0][0];
    const double m01 = r[0][1];
    const double m02 = r[0][2];
    const double m10 = r[1][0];
    const double m11 = r[1][1];
    const double m12 = r[1][2];
    const double m20 = r[2][0];
    const double m21 = r[2][1];
    const double m22 = r[2][2];
    const double tr = m00 + m11 + m22;

    if (tr > 0.0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        return {0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    }

    if (m00 >= m11 && m00 >= m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        return {(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
    }

    if (m11 >= m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        return {(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
    }

    const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    return {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
}

Vec4 QuatMul(const Vec4 &a, const Vec4 &b) {
    return {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
            a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
            a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]};
}

double Dot(const Vec4 &a, const Vec4 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

// ---------------------------------------------------------------------------------------------
// Configs (info.cfg)
// ---------------------------------------------------------------------------------------------

std::string Text(const Bytes &b) {
    return std::string(b.begin(), b.end());
}

Bytes FromText(const std::string &s) {
    return Bytes(s.begin(), s.end());
}

// The first quoted argument after tag at a word start, or empty.
std::string Quoted(const std::string &text, std::string_view tag) {
    std::size_t at = 0;

    while ((at = text.find(tag, at)) != std::string::npos) {
        std::size_t p = at + tag.size();

        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
            p++;
        }

        if (p > at + tag.size() && p < text.size() && text[p] == '"') {
            const std::size_t end = text.find('"', p + 1);

            if (end != std::string::npos && end > p + 1) {
                return text.substr(p + 1, end - p - 1);
            }
        }

        at += tag.size();
    }

    return {};
}

// The quoted arguments of the body MOTION 0 line: motion, bone binds, weights.
std::vector<std::string> MotionLine(const std::string &text) {
    std::vector<std::string> out;
    std::size_t              at = 0;

    while ((at = text.find("MOTION", at)) != std::string::npos) {
        const bool word = at == 0 || text[at - 1] == '\n' || text[at - 1] == ' ' || text[at - 1] == '\t';
        std::size_t p = at + 6;

        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
            p++;
        }

        if (word && p > at + 6 && p < text.size() && text[p] == '0' && p + 1 < text.size() && text[p + 1] == ',') {
            const std::size_t eol = text.find('\n', p);
            const std::string line = text.substr(p, eol == std::string::npos ? std::string::npos : eol - p);
            std::size_t       q = 0;

            while ((q = line.find('"', q)) != std::string::npos) {
                const std::size_t end = line.find('"', q + 1);

                if (end == std::string::npos) {
                    break;
                }

                out.push_back(line.substr(q + 1, end - q - 1));
                q = end + 1;
            }

            return out;
        }

        at += 6;
    }

    return out;
}

struct CfgInfo {
    Record     *cfg = nullptr;
    std::string body_mot;
    std::string body_bbp;
    std::string body_mds;
    std::string shadow_mot;
    std::string shadow_mds;
};

CfgInfo CfgOf(Pack &pack, std::string_view prefer = {}) {
    std::vector<Record *> candidates;

    for (Record &record : pack.records) {
        if (Lower(record.name).ends_with(".cfg")) {
            candidates.push_back(&record);
        }
    }

    if (!prefer.empty()) {
        std::ranges::stable_partition(candidates, [&](Record *r) { return Lower(r->name) == Lower(prefer); });
    }

    CfgInfo info;

    for (Record *record : candidates) {
        const std::string text = Text(record->Payload());

        if (text.find("KEY_START") != std::string::npos && text.find("MOTION") != std::string::npos) {
            info.cfg = record;
            break;
        }
    }

    if (info.cfg == nullptr && !candidates.empty()) {
        info.cfg = candidates.front();
    }

    if (info.cfg == nullptr) {
        throw std::runtime_error("the pack has no info.cfg");
    }

    const std::string              text = Text(info.cfg->Payload());
    const std::vector<std::string> motion = MotionLine(text);

    if (!motion.empty()) {
        info.body_mot = motion[0];
    }

    if (motion.size() > 1) {
        info.body_bbp = motion[1];
    }

    info.body_mds = Quoted(text, "MODEL");
    info.shadow_mot = Quoted(text, "SHADOW_MOTION");
    info.shadow_mds = Quoted(text, "SHADOW_MODEL");

    if (info.body_mot.empty() || info.body_mds.empty()) {
        throw std::runtime_error(std::format("{} names no body model or motion", info.cfg->name));
    }

    return info;
}

// The cfg's line ending: the disc's are CRLF.
std::string LineEnd(const std::string &text) {
    return text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

// ---------------------------------------------------------------------------------------------
// The recipes (TownModelBakes.cs Chars)
// ---------------------------------------------------------------------------------------------

struct Slot {
    int                      idx = 0;
    int                      lo = 0;
    int                      hi = 0;
    double                   speed = 0.0;
    const char              *name = "";
    const char              *src = nullptr;
    int                      win_lo = 0;
    int                      win_hi = 0;
    const char              *src_cfg = nullptr;
    std::optional<Vec4>      root_offset;
    bool                     reverse = false;
    std::optional<int>       hold;
    std::optional<int>       root_freeze;
    std::vector<std::string> pin_nodes;
    int                      pin_frame = 0;
};

struct Spin {
    std::string           node;
    std::array<double, 3> axis{};
    double                rate = 0.0;
    double                handoff = 0.0;
    std::pair<int, int>   ramp_up;
    std::pair<int, int>   loop;
    std::pair<int, int>   ramp_down;
};

struct MeshGraft {
    const char              *src = nullptr;
    int                      pose_frame = 0;
    std::vector<std::string> nodes;
    Spin                     spin;
};

struct ShadowInject {
    const char *src = nullptr;
    const char *model = nullptr;
    const char *mot = nullptr;
    const char *bbp = nullptr;
    const char *wgt = nullptr;
};

struct Character {
    const char                 *name = "";
    const char                 *base = "";
    bool                        root_motion = false;
    std::optional<MeshGraft>    mesh_graft;
    std::optional<ShadowInject> shadow_inject;
    std::vector<Slot>           slots;
};

Slot Kept(int idx, int lo, int hi, double speed, const char *name) {
    return Slot{.idx = idx, .lo = lo, .hi = hi, .speed = speed, .name = name};
}

Slot Copied(int idx, int lo, int hi, double speed, const char *name, const char *src, int win_lo, int win_hi) {
    return Slot{.idx = idx, .lo = lo, .hi = hi, .speed = speed, .name = name, .src = src, .win_lo = win_lo, .win_hi = win_hi};
}

Slot Offset(Slot slot, double z) {
    slot.root_offset = Vec4{0.0, 0.0, z, 0.0};
    return slot;
}

Slot Reversed(Slot slot) {
    slot.reverse = true;
    return slot;
}

Slot FromCfg(Slot slot, const char *cfg) {
    slot.src_cfg = cfg;
    return slot;
}

constexpr const char *kS86Cat = "gedit/s86/chara/c04cat.chr";
constexpr const char *kE04Cat = "gedit/e01/chara/e04c04cat.chr";
constexpr const char *kC04pCat = "gedit/e01/chara/c04pcat.chr";
constexpr const char *kE613Cat = "dun/d01/event/e613c04cat.chr";
constexpr const char *kE403 = "gedit/e05/chara/e403c18a.chr";
constexpr const char *kE402 = "gedit/s13/chara/e402c18a.chr";
constexpr const char *kC18p = "gedit/e05/chara/c18p.chr";
constexpr const char *kC06b = "dun/mainchara/c06b.chr";
constexpr const char *kE101 = "gedit/s01/chara/e101.chr";
constexpr const char *kE102 = "gedit/s01/chara/e102.chr";
constexpr const char *kC10b = "dun/mainchara/c10b.chr";
constexpr const char *kDunC05a = "dun/mainchara/c05a.chr";
constexpr const char *kE223 = "gedit/e03/chara/e223c05a.chr";
constexpr const char *kE228 = "gedit/e03/chara/e228c05a.chr";

std::vector<Character> Characters() {
    std::vector<Character> chars;

    // Xiao: the base c04pcat keeps idle and walk; run, door, sit, refusal, fall, land and the ladder
    // clips come from her other cat models.
    chars.push_back(Character{
        .name = "Xiao",
        .base = kC04pCat,
        .slots = {
            Kept(0, 10, 20, 0.10, "idle"),
            Copied(1, 120, 136, 0.50, "run", kS86Cat, 120, 136),
            Kept(2, 60, 80, 0.30, "walk"),
            Offset(Copied(3, 150, 159, 0.20, "door", kE04Cat, 160, 169), -2.0),
            Kept(4, 150, 159, 0.20, "door2"),
            Copied(5, 30, 40, 0.10, "sit", kS86Cat, 30, 40),
            Kept(6, 30, 40, 0.10, "sit-loop"),
            Copied(7, 228, 278, 0.30, "refuse", kE613Cat, 115, 165),
            Copied(8, 205, 214, 0.50, "fall(leap)", kS86Cat, 205, 214),
            Copied(9, 215, 227, 0.36, "land", kS86Cat, 215, 227),
            Kept(10, 150, 159, 0.20, "door3"),
            Copied(11, 95, 105, 0.40, "jump-ready", kS86Cat, 95, 105),
            Copied(12, 285, 294, 0.60, "float-up", kE04Cat, 160, 169),
            Reversed(Copied(13, 300, 320, 0.30, "walk-back", kC04pCat, 60, 80)),
        },
    });

    // Osmond: the base c18p, his helicopter backpack added from his flight model with its spin made
    // up, falls and lands from e403.
    Slot hold = Kept(5, 90, 92, 0.10, "item(hold69)");
    hold.hold = 69;
    chars.push_back(Character{
        .name = "Osmond",
        .base = kC18p,
        .mesh_graft = MeshGraft{
            .src = kE402,
            .pose_frame = 15,
            .nodes = {"bone4", "bone5", "bone6", "tukene1", "obj41", "obj42", "obj43", "obj38", "obj39", "obj40", "obj35", "obj36", "obj37"},
            .spin = Spin{.node = "tukene1",
                         .axis = {0.0, 1.0, 0.0},
                         .rate = 72.0,
                         .handoff = 36.0,
                         .ramp_up = {240, 255},
                         .loop = {260, 270},
                         .ramp_down = {275, 290}},
        },
        .slots = {
            Kept(0, 10, 20, 0.10, "idle"),
            Copied(1, 130, 150, 0.75, "run(sped)", kC18p, 30, 50),
            Kept(2, 30, 50, 0.20, "walk"),
            Kept(3, 60, 80, 0.20, "door(talk)"),
            Kept(4, 60, 80, 0.30, "door2(talk)"),
            hold,
            Kept(6, 90, 92, 0.10, "item-loop"),
            Kept(7, 10, 20, 0.10, "damage(skip)"),
            Copied(8, 100, 108, 0.30, "fall", kE403, 210, 218),
            Copied(9, 110, 125, 0.30, "land", kE403, 220, 235),
            Kept(10, 60, 80, 0.30, "door3(dbl)"),
            Copied(11, 155, 175, 0.30, "jump-down", kE403, 185, 205),
            Copied(12, 180, 235, 0.25, "propeller", kE402, 280, 335),
            Copied(13, 240, 255, 0.25, "start-fly", kE402, 345, 360),
            Copied(14, 260, 270, 0.20, "fly-loop", kE402, 360, 370),
            Reversed(Copied(15, 275, 290, 0.25, "rev-startfly", kE402, 345, 360)),
            Reversed(Copied(16, 295, 350, 0.30, "rev-propeller", kE402, 280, 335)),
        },
    });

    // Goro: c06p's own run and walk; item get and refusal from his dungeon model, fall, land and the
    // ladder hops from his treehouse cutscenes.
    chars.push_back(Character{
        .name = "Goro",
        .base = "gedit/s01/chara/c06p.chr",
        .root_motion = true,
        .slots = {
            Kept(0, 10, 20, 0.15, "idle"),
            Kept(1, 60, 80, 0.40, "run"),
            Kept(2, 30, 50, 0.25, "walk"),
            Kept(3, 10, 20, 0.15, "door(idle)"),
            Kept(4, 10, 20, 0.15, "door2(idle)"),
            Copied(5, 90, 100, 0.30, "item", kC06b, 515, 525),
            Copied(6, 105, 115, 0.30, "item-loop", kC06b, 530, 540),
            Copied(7, 120, 149, 0.50, "refuse(no)", kC06b, 421, 450),
            FromCfg(Copied(8, 155, 175, 0.30, "fall", kE101, 75, 95), "e101c06a.cfg"),
            FromCfg(Copied(9, 180, 190, 0.25, "land(stand)", kE101, 95, 105), "e101c06a.cfg"),
            Kept(10, 10, 20, 0.15, "door3(idle)"),
            FromCfg(Copied(11, 195, 215, 0.30, "climb-hopA", kE102, 90, 110), "e102c06a.cfg"),
            FromCfg(Copied(12, 220, 240, 0.30, "climb-hopB", kE102, 110, 130), "e102c06a.cfg"),
            Reversed(FromCfg(Copied(13, 245, 255, 0.25, "rev-land", kE101, 95, 105), "e101c06a.cfg")),
        },
    });

    // Ungaga: e323_2c10a keeps idle, walk and his talk clips for the doors; his battle run, item get,
    // refusal, fall and land come from his dungeon model.
    Slot refuse = Copied(7, 270, 310, 0.22, "refuse(NG)", kC10b, 490, 530);
    refuse.pin_nodes = {"kon_1", "r_handa"};
    refuse.pin_frame = 15;
    Slot fall = Copied(8, 315, 315, 0.0, "fall(296)", kC10b, 296, 296);
    chars.push_back(Character{
        .name = "Ungaga",
        .base = "gedit/e04/chara/e323_2c10a.chr",
        .root_motion = true,
        .slots = {
            Kept(0, 10, 20, 0.10, "idle"),
            Copied(1, 60, 80, 0.55, "run", kC10b, 60, 80),
            Kept(2, 30, 50, 0.27, "walk"),
            Offset(Kept(3, 115, 131, 0.20, "door(chest)"), -2.5),
            Kept(4, 115, 131, 0.20, "door2"),
            Copied(5, 240, 250, 0.30, "item", kC10b, 630, 640),
            Copied(6, 255, 265, 0.30, "item-loop", kC10b, 643, 653),
            refuse,
            fall,
            Reversed(Copied(9, 357, 358, 0.20, "land(rev)", kC10b, 295, 296)),
            Kept(10, 115, 131, 0.20, "door3"),
        },
    });

    // Ruby: the simple c05a with a shadow added from e223; her walk, run and the rest from her dungeon
    // model, the doors from e223, the float and jump from e228 with their travel frozen.
    Slot jump = Copied(11, 200, 230, 0.20, "jump", kE228, 285, 315);
    jump.root_freeze = 200;
    Slot jump_loop = Copied(12, 235, 245, 0.10, "jump-loop", kE228, 320, 330);
    jump_loop.root_freeze = 175;
    chars.push_back(Character{
        .name = "Ruby",
        .base = "gedit/e03/chara/c05a.chr",
        .root_motion = true,
        .shadow_inject = ShadowInject{.src = kE223, .model = "e223c05s.mds", .mot = "e223c05s.mot", .bbp = "e223c05s.bbp", .wgt = "e223c05s.wgt"},
        .slots = {
            Copied(0, 10, 20, 0.15, "idle", kDunC05a, 10, 20),
            Copied(1, 60, 80, 0.55, "run", kDunC05a, 60, 80),
            Copied(2, 30, 50, 0.25, "walk", kDunC05a, 30, 50),
            Offset(Copied(3, 90, 102, 0.15, "door(pat)", kE223, 100, 112), -3.0),
            Kept(4, 90, 102, 0.15, "door2(pat)"),
            Copied(5, 120, 130, 0.30, "item", kDunC05a, 575, 585),
            Copied(6, 135, 145, 0.30, "item-loop", kDunC05a, 590, 600),
            Copied(7, 150, 170, 0.20, "refuse(no)", kDunC05a, 605, 625),
            Copied(8, 175, 195, 0.10, "fall(float)", kE228, 10, 30),
            Kept(9, 10, 20, 0.15, "land(idle)"),
            Kept(10, 90, 102, 0.15, "door3(pat)"),
            jump,
            jump_loop,
        },
    });

    return chars;
}

// ---------------------------------------------------------------------------------------------
// The assembly (TownModelBakes.cs)
// ---------------------------------------------------------------------------------------------

using Span = std::pair<int, int>;

// The parts of [lo, hi] outside the windows.
std::vector<Span> FoldSpans(std::vector<Span> windows, int lo, int hi) {
    std::ranges::sort(windows);
    std::vector<Span> spans;
    int               cur = lo;

    for (const auto &[a, b] : windows) {
        if (cur < a) {
            spans.emplace_back(cur, a - 1);
        }

        cur = std::max(cur, b + 1);
    }

    if (cur <= hi) {
        spans.emplace_back(cur, hi);
    }

    return spans;
}

std::vector<int> SpanEdges(const std::vector<Span> &spans) {
    std::vector<int> edges;

    for (const auto &[lo, hi] : spans) {
        edges.push_back(lo);

        if (hi != lo) {
            edges.push_back(hi);
        }
    }

    return edges;
}

// A source joint's place in the destination rig by name; the root always maps to the root.
std::vector<int> JointRemap(const std::vector<std::string> &src, const std::vector<std::string> &dst) {
    std::map<std::string, int> dst_of;

    for (int i = 0; i < static_cast<int>(dst.size()); i++) {
        dst_of.try_emplace(dst[i], i);
    }

    std::vector<int> remap(src.size(), -1);

    for (int i = 0; i < static_cast<int>(src.size()); i++) {
        if (i == 0) {
            remap[i] = 0;
        } else if (auto found = dst_of.find(src[i]); found != dst_of.end()) {
            remap[i] = found->second;
        }
    }

    return remap;
}

// Same-character rigs sometimes differ only in the root's name.
std::vector<std::string> AliasRoot(std::vector<std::string> s, const std::vector<std::string> &d) {
    if (!s.empty() && !d.empty() && s[0] != d[0] && std::ranges::find(d, s[0]) == d.end() && std::ranges::find(s, d[0]) == s.end()) {
        s[0] = d[0];
    }

    return s;
}

struct Splice {
    int written = 0;
    int no_joint = 0;
    int no_track = 0;
};

// MotSplice.SpliceByJoint: the source's keys in [slo, shi] replace the destination's in [dlo, dhi],
// joint by joint.
Splice SpliceByJoint(Mot &dst, const Mot &src, const std::vector<std::string> &sframes, const std::vector<std::string> &dframes,
                     int slo, int shi, int dlo, int dhi) {
    const std::vector<int> remap = JointRemap(sframes, dframes);
    const long             delta = static_cast<long>(dlo) - slo;
    Splice                 report;
    std::map<std::pair<std::uint32_t, std::uint32_t>, Track *> dst_by;

    for (Track &t : dst.tracks) {
        dst_by[{t.w0, t.w2}] = &t;
    }

    for (const Track &st : src.tracks) {
        if (!st.HasKeysIn(static_cast<std::uint32_t>(slo), static_cast<std::uint32_t>(shi))) {
            continue;
        }

        const int dw = st.w0 < remap.size() ? remap[st.w0] : -1;

        if (dw < 0) {
            report.no_joint++;
            continue;
        }

        auto found = dst_by.find({static_cast<std::uint32_t>(dw), st.w2});

        if (found == dst_by.end()) {
            report.no_track++;
            continue;
        }

        Track           &dt = *found->second;
        std::vector<Key> keys;

        for (const Key &k : dt.keys) {
            if (!(static_cast<std::uint32_t>(dlo) <= k.Frame() && k.Frame() <= static_cast<std::uint32_t>(dhi))) {
                keys.push_back(k);
            }
        }

        for (const Key &k : st.keys) {
            if (k.Frame() < static_cast<std::uint32_t>(slo) || k.Frame() > static_cast<std::uint32_t>(shi)) {
                continue;
            }

            const long f = static_cast<long>(k.Frame()) + delta;

            if (f < dlo || f > dhi) {
                continue;
            }

            Key copy = k;
            copy.SetFrame(static_cast<std::uint32_t>(f));
            keys.push_back(copy);
        }

        dt.keys = std::move(keys);
        dt.Sort();
        report.written++;
    }

    return report;
}

// Root-motion recovery: a source track in the window with no destination track gets one first, held
// at the destination joint's bind pose outside the copied windows.
void CreateMissingTracks(Mot &dst, const Mot &src, const std::vector<std::string> &sframes, const std::vector<std::string> &dframes,
                         int slo, int shi, const Bytes &dmds, const std::vector<Span> &spans) {
    const std::vector<int>                              remap = JointRemap(sframes, dframes);
    std::set<std::pair<std::uint32_t, std::uint32_t>> have;

    for (const Track &t : dst.tracks) {
        have.insert({t.w0, t.w2});
    }

    for (const Track &st : src.tracks) {
        if (!st.HasKeysIn(static_cast<std::uint32_t>(slo), static_cast<std::uint32_t>(shi))) {
            continue;
        }

        const int dw = st.w0 < remap.size() ? remap[st.w0] : -1;

        if (dw < 0 || have.contains({static_cast<std::uint32_t>(dw), st.w2})) {
            continue;
        }

        const Bind bind = MdsBind(dmds, dw);
        const Vec4 v = st.w2 == 0 ? MatToQuat(bind.rows) : Vec4{bind.translation[0], bind.translation[1], bind.translation[2], 0.0};
        Track      nt{.w0 = static_cast<std::uint32_t>(dw), .w1 = st.w1, .w2 = st.w2, .w3 = st.w3, .w6 = st.w6, .w7 = st.w7};

        for (int f : SpanEdges(spans)) {
            nt.keys.push_back(NewKey(static_cast<std::uint32_t>(f), v));
        }

        auto at = std::ranges::find_if(dst.tracks, [&](const Track &t) {
            return t.w0 > static_cast<std::uint32_t>(dw) || (t.w0 == static_cast<std::uint32_t>(dw) && t.w2 > st.w2);
        });
        dst.tracks.insert(at, std::move(nt));
        have.insert({static_cast<std::uint32_t>(dw), st.w2});
    }
}

// Every copied track gets keys at the window's edges, from the source's edges, so sparse tracks do
// not lean into the next clip.
void SealGraft(Mot &dst, const Mot &src, const std::vector<std::string> &sframes, const std::vector<std::string> &dframes, int slo,
               int shi, int dlo, int dhi) {
    std::map<int, int> remap;

    for (int i = 0; i < static_cast<int>(sframes.size()); i++) {
        auto found = std::ranges::find(dframes, sframes[i]);

        if (found != dframes.end()) {
            remap[i] = static_cast<int>(found - dframes.begin());
        }
    }

    std::map<std::pair<std::uint32_t, std::uint32_t>, Track *> dst_by;

    for (Track &t : dst.tracks) {
        dst_by[{t.w0, t.w2}] = &t;
    }

    for (const Track &st : src.tracks) {
        auto joint = remap.find(static_cast<int>(st.w0));

        if (joint == remap.end()) {
            continue;
        }

        auto found = dst_by.find({static_cast<std::uint32_t>(joint->second), st.w2});

        if (found == dst_by.end() || found->second->keys.empty()) {
            continue;
        }

        Track &dt = *found->second;

        for (const auto &[sf, df] : {std::pair{slo, dlo}, std::pair{shi, dhi}}) {
            if (std::ranges::any_of(dt.keys, [&](const Key &k) { return k.Frame() == static_cast<std::uint32_t>(df); })) {
                continue;
            }

            const std::optional<Vec4> v = SampleTrack(st, sf);

            if (!v) {
                continue;
            }

            Key key = dt.keys.front();
            key.SetFrame(static_cast<std::uint32_t>(df));
            key.SetValue(*v);
            dt.keys.push_back(key);
            dt.Sort();
        }
    }
}

void Graft(Pack &dst_pack, const std::string &dst_mot, const std::string &dst_mds, Pack &src_pack, const std::string &src_mot,
           const std::string &src_mds, const Slot &slot, bool root_motion, const std::vector<Span> &spans, std::vector<std::string> &log) {
    Mot                      dst = ReadMot(dst_pack, dst_mot);
    const Mot                src = ReadMot(src_pack, src_mot);
    const Bytes              dmds = dst_pack.Require(dst_mds).Payload();
    const std::vector<std::string> dframes = MdsFrames(dmds);
    std::vector<std::string> sframes = MdsFrames(src_pack.Require(src_mds).Payload());

    if (root_motion) {
        sframes = AliasRoot(sframes, dframes);
        CreateMissingTracks(dst, src, sframes, dframes, slot.win_lo, slot.win_hi, dmds, spans);
    }

    const Splice report = SpliceByJoint(dst, src, sframes, dframes, slot.win_lo, slot.win_hi, slot.lo, slot.hi);
    SealGraft(dst, src, sframes, dframes, slot.win_lo, slot.win_hi, slot.lo, slot.hi);
    WriteMot(dst_pack, dst_mot, dst);

    log.push_back(std::format("  slot {} {} ({}): {} tracks copied from {} {}-{}, {} with no joint, {} with no track", slot.idx, slot.name,
                              dst_mot, report.written, Basename(slot.src), slot.win_lo, slot.win_hi, report.no_joint, report.no_track));
}

// A still pose: every track sampled at the frame, held across the window.
void BakeHold(Pack &pack, const std::string &mot_name, int frame, int dlo, int dhi) {
    Mot mot = ReadMot(pack, mot_name);

    for (Track &t : mot.tracks) {
        const std::optional<Vec4> v = SampleTrack(t, frame);

        if (!v || t.keys.empty()) {
            continue;
        }

        std::erase_if(t.keys, [&](const Key &k) { return static_cast<std::uint32_t>(dlo) <= k.Frame() && k.Frame() <= static_cast<std::uint32_t>(dhi); });

        if (t.keys.empty()) {
            throw std::runtime_error("hold: a track had keys only inside the held window");
        }

        for (int f : {dlo, dhi}) {
            Key key = t.keys.front();
            key.SetFrame(static_cast<std::uint32_t>(f));
            key.SetValue(*v);
            t.keys.push_back(key);
        }

        t.Sort();
    }

    WriteMot(pack, mot_name, mot);
}

// Mirrors the keys inside the window, so the clip plays backwards.
void ReverseWindow(Pack &pack, const std::string &mot_name, int dlo, int dhi) {
    Mot mot = ReadMot(pack, mot_name);

    for (Track &t : mot.tracks) {
        bool any = false;

        for (Key &k : t.keys) {
            if (static_cast<std::uint32_t>(dlo) <= k.Frame() && k.Frame() <= static_cast<std::uint32_t>(dhi)) {
                k.SetFrame(static_cast<std::uint32_t>(dlo + dhi - static_cast<int>(k.Frame())));
                any = true;
            }
        }

        if (any) {
            t.Sort();
        }
    }

    WriteMot(pack, mot_name, mot);
}

// Moves the root's translation keys in the window (the first translation track).
void ApplyRootOffset(Pack &pack, const std::string &mot_name, int dlo, int dhi, const Vec4 &offset) {
    Mot  mot = ReadMot(pack, mot_name);
    auto root = std::ranges::find_if(mot.tracks, [](const Track &t) { return t.w2 == 2; });

    if (root == mot.tracks.end()) {
        return;
    }

    for (Key &k : root->keys) {
        if (static_cast<std::uint32_t>(dlo) <= k.Frame() && k.Frame() <= static_cast<std::uint32_t>(dhi)) {
            Vec4 v = k.Value();

            for (int i = 0; i < 3; i++) {
                v[i] += offset[i];
            }

            k.SetValue(v);
        }
    }

    WriteMot(pack, mot_name, mot);
}

// Freezes every translation in the window at its value on ref_frame (no baked travel).
void FreezeRoots(Pack &pack, const std::string &mot_name, int ref_frame, int dlo, int dhi) {
    Mot mot = ReadMot(pack, mot_name);

    for (Track &t : mot.tracks) {
        if (t.w2 != 2 || !t.HasKeysIn(static_cast<std::uint32_t>(dlo), static_cast<std::uint32_t>(dhi))) {
            continue;
        }

        const std::optional<Vec4> v = SampleTrack(t, ref_frame);

        if (!v) {
            continue;
        }

        for (Key &k : t.keys) {
            if (static_cast<std::uint32_t>(dlo) <= k.Frame() && k.Frame() <= static_cast<std::uint32_t>(dhi)) {
                k.SetValue(*v);
            }
        }
    }

    WriteMot(pack, mot_name, mot);
}

// Holds the named nodes at their ref_frame pose across the window (a prop the source rig moved
// under another name).
void PinNodes(Pack &pack, const std::string &mot_name, const std::string &mds_name, const std::vector<std::string> &nodes, int ref_frame,
              int dlo, int dhi) {
    Mot                            mot = ReadMot(pack, mot_name);
    const std::vector<std::string> frames = MdsFrames(pack.Require(mds_name).Payload());

    for (Track &t : mot.tracks) {
        if (t.w0 >= frames.size() || std::ranges::find(nodes, frames[t.w0]) == nodes.end()) {
            continue;
        }

        const std::optional<Vec4> v = SampleTrack(t, ref_frame);

        if (!v) {
            continue;
        }

        std::erase_if(t.keys, [&](const Key &k) { return static_cast<std::uint32_t>(dlo) <= k.Frame() && k.Frame() <= static_cast<std::uint32_t>(dhi); });

        if (t.keys.empty()) {
            throw std::runtime_error("pin: a track had keys only inside the pinned window");
        }

        for (int f : {dlo, dhi}) {
            Key key = t.keys.front();
            key.SetFrame(static_cast<std::uint32_t>(f));
            key.SetValue(*v);
            t.keys.push_back(key);
        }

        t.Sort();
    }

    WriteMot(pack, mot_name, mot);
}

// Gives a base with no shadow the donor's shadow set, declared in its cfg.
void InjectShadow(Pack &base, Pack &donor, const ShadowInject &inject) {
    for (const char *name : {inject.model, inject.bbp, inject.wgt, inject.mot}) {
        Record *record = donor.Find(name);

        if (record == nullptr) {
            throw std::runtime_error(std::format("shadow: the donor has no {}", name));
        }

        if (base.Find(record->name) != nullptr) {
            throw std::runtime_error(std::format("shadow: the base already has {}", record->name));
        }

        base.records.push_back(*record);
    }

    Record           *cfg = CfgOf(base).cfg;
    std::string       text = Text(cfg->Payload());
    const std::string eol = LineEnd(text);
    const std::size_t va = text.find("VERTEX_ANIME");

    if (va == std::string::npos) {
        throw std::runtime_error("shadow: the cfg has no VERTEX_ANIME line");
    }

    std::size_t at = text.find(eol, va) + eol.size();
    text.insert(at, "SHADOW_VERTEX_ANIME 1" + eol);

    const std::size_t model = text.find("MODEL ");

    if (model == std::string::npos) {
        throw std::runtime_error("shadow: the cfg has no MODEL line");
    }

    at = text.find(eol, model) + eol.size();
    text.insert(at, std::format("SHADOW_MODEL \"{}\"{}", inject.model, eol));

    const std::size_t shadow = text.find("SHADOW_MOTION");

    if (shadow == std::string::npos) {
        throw std::runtime_error("shadow: the cfg has no SHADOW_MOTION line");
    }

    const std::size_t line_end = text.find_first_of("\r\n", shadow);
    text.replace(shadow, (line_end == std::string::npos ? text.size() : line_end) - shadow,
                 std::format("SHADOW_MOTION \"{}\", \"{}\", \"{}\"", inject.mot, inject.bbp, inject.wgt));
    cfg->ReplacePayload(FromText(text));
}

// Osmond's helicopter: the named nodes, their meshes and bind rows copied from the flight model onto
// the base scene, and a track per source channel holding the stowed pose outside the copied windows.
void GraftMeshNodes(Pack &base, Pack &src, const MeshGraft &graft, const std::vector<Span> &windows, int max_frame) {
    CfgInfo                        scfg = CfgOf(src);
    CfgInfo                        cfg = CfgOf(base);
    const Bytes                    bp = base.Require(cfg.body_mds).Payload();
    const Bytes                    spl = src.Require(scfg.body_mds).Payload();
    const std::vector<std::string> bnames = MdsFrames(bp);
    const std::vector<std::string> snames = MdsFrames(spl);
    const int                      nb = static_cast<int>(bnames.size());
    const int                      count = static_cast<int>(graft.nodes.size());

    for (const std::string &n : graft.nodes) {
        if (std::ranges::find(bnames, n) != bnames.end()) {
            throw std::runtime_error(std::format("helicopter: {} is already in the base rig", n));
        }

        if (std::ranges::find(snames, n) == snames.end()) {
            throw std::runtime_error(std::format("helicopter: {} is not in the flight model", n));
        }
    }

    if (!cfg.shadow_mds.empty()) {
        const std::vector<std::string> shadow = MdsFrames(base.Require(cfg.shadow_mds).Payload());

        for (const std::string &n : graft.nodes) {
            if (std::ranges::find(shadow, n) != shadow.end()) {
                throw std::runtime_error(std::format("helicopter: the base shadow rig has {}", n));
            }
        }
    }

    const std::size_t mesh0 = 0x18 + static_cast<std::size_t>(nb) * 0x70 - 8;
    std::size_t       first = SIZE_MAX;

    for (int i = 0; i < nb; i++) {
        const std::uint32_t off = U32(bp, 0x18 + static_cast<std::size_t>(i) * 0x70 + 0x20);

        if (off != 0) {
            first = std::min<std::size_t>(first, off);
        }
    }

    if (first != mesh0 || std::memcmp(bp.data() + mesh0, "MDT\0", 4) != 0) {
        throw std::runtime_error("helicopter: the base scene's node table and meshes are not laid out as expected");
    }

    const std::uint32_t        delta = static_cast<std::uint32_t>(count) * 0x70;
    std::map<std::string, int> new_index;

    for (int k = 0; k < count; k++) {
        new_index[graft.nodes[k]] = nb + k;
    }

    Bytes table(bp.begin(), bp.begin() + static_cast<std::ptrdiff_t>(mesh0));
    AppendU32(table, static_cast<std::uint32_t>(nb));
    AppendU32(table, 0x70);
    PutU32(table, 0x08, static_cast<std::uint32_t>(nb + count));

    for (int i = 0; i < nb; i++) {
        const std::size_t   at = 0x18 + static_cast<std::size_t>(i) * 0x70 + 0x20;
        const std::uint32_t off = U32(table, at);

        if (off != 0) {
            PutU32(table, at, off + delta);
        }
    }

    const Bytes       block(bp.begin() + static_cast<std::ptrdiff_t>(mesh0), bp.end());
    const std::size_t chunk_start = 0x18 + static_cast<std::size_t>(nb + count) * 0x70 - 8 + block.size();
    Bytes             records;
    Bytes             chunks;

    for (int k = 0; k < count; k++) {
        const std::string &n = graft.nodes[k];
        const int          si = static_cast<int>(std::ranges::find(snames, n) - snames.begin());
        Bytes              rec(spl.begin() + 0x18 + si * 0x70, spl.begin() + 0x18 + si * 0x70 + 0x68);
        const int          spar = static_cast<int>(U32(rec, 0x24));
        int                npar = -1;

        if (spar >= 0) {
            const std::string &pname = snames[spar];

            if (auto in_base = std::ranges::find(bnames, pname); in_base != bnames.end()) {
                npar = static_cast<int>(in_base - bnames.begin());
            } else if (auto grafted = new_index.find(pname); grafted != new_index.end()) {
                npar = grafted->second;
            } else {
                throw std::runtime_error(std::format("helicopter: {}'s parent {} is in neither rig", n, pname));
            }

            if (npar >= nb + k) {
                throw std::runtime_error(std::format("helicopter: {}'s parent {} would come after it", n, pname));
            }
        }

        PutU32(rec, 0x24, static_cast<std::uint32_t>(npar));
        const std::uint32_t smo = U32(rec, 0x20);

        if (smo != 0) {
            if (std::memcmp(spl.data() + smo, "MDT\0", 4) != 0) {
                throw std::runtime_error(std::format("helicopter: {}'s mesh is not an MDT chunk", n));
            }

            const std::uint32_t chunk_size = U32(spl, smo + 8);
            PutU32(rec, 0x20, static_cast<std::uint32_t>(chunk_start + chunks.size()));
            chunks.insert(chunks.end(), spl.begin() + smo, spl.begin() + smo + chunk_size);

            while (chunks.size() % 16 != 0) {
                chunks.push_back(0);
            }
        }

        records.insert(records.end(), rec.begin(), rec.end());
        AppendU32(records, static_cast<std::uint32_t>(nb + k + 1));
        AppendU32(records, 0x70);
    }

    records.resize(records.size() - 8);

    Bytes scene = table;
    scene.insert(scene.end(), records.begin(), records.end());
    scene.insert(scene.end(), block.begin(), block.end());
    scene.insert(scene.end(), chunks.begin(), chunks.end());
    base.Require(cfg.body_mds).ReplacePayload(scene);

    const Bytes bb = base.Require(cfg.body_bbp).Payload();
    const Bytes sb = src.Require(scfg.body_bbp).Payload();

    if (bb.size() != static_cast<std::size_t>(nb) * 64 || sb.size() != snames.size() * 64) {
        throw std::runtime_error("helicopter: a .bbp is not 64 bytes per node");
    }

    Bytes binds = bb;

    for (const std::string &n : graft.nodes) {
        const std::size_t si = static_cast<std::size_t>(std::ranges::find(snames, n) - snames.begin());
        binds.insert(binds.end(), sb.begin() + static_cast<std::ptrdiff_t>(si * 64), sb.begin() + static_cast<std::ptrdiff_t>(si * 64 + 64));
    }

    base.Require(cfg.body_bbp).ReplacePayload(binds);

    const Mot               sm = ReadMot(src, scfg.body_mot);
    Mot                     dm = ReadMot(base, cfg.body_mot);
    const std::vector<Span> spans = FoldSpans(windows, 1, max_frame);
    const std::vector<int>  edges = SpanEdges(spans);

    for (const std::string &n : graft.nodes) {
        bool has_rotation = false;

        for (const Track &st : sm.tracks) {
            if (st.w0 >= snames.size() || snames[st.w0] != n) {
                continue;
            }

            const std::optional<Vec4> v = SampleTrack(st, graft.pose_frame);

            if (!v) {
                continue;
            }

            Track t{.w0 = static_cast<std::uint32_t>(new_index[n]), .w1 = st.w1, .w2 = st.w2, .w3 = st.w3, .w6 = st.w6, .w7 = st.w7};

            for (int f : edges) {
                t.keys.push_back(NewKey(static_cast<std::uint32_t>(f), *v));
            }

            has_rotation = has_rotation || st.w2 == 0;
            dm.tracks.push_back(std::move(t));
        }

        if (n == graft.spin.node && !has_rotation) {
            // The spin's hub has no rotation of its own: one held at its bind orientation.
            const int si = static_cast<int>(std::ranges::find(snames, n) - snames.begin());
            const Vec4 qb = MatToQuat(MdsBind(spl, si).rows);
            Track      t{.w0 = static_cast<std::uint32_t>(new_index[n]), .w1 = 0, .w2 = 0, .w3 = 0x20, .w6 = kTagW6, .w7 = kTagW7};

            for (int f : edges) {
                t.keys.push_back(NewKey(static_cast<std::uint32_t>(f), qb));
            }

            dm.tracks.push_back(std::move(t));
        }
    }

    WriteMot(base, cfg.body_mot, dm);
}

// The propeller's spin, made up on the hub: an ease-in ramp up, a loop at full rate, a ramp down to
// rest on a whole turn.
void ApplySpin(Pack &base, const MeshGraft &graft) {
    const Spin    &spin = graft.spin;
    CfgInfo        cfg = CfgOf(base);
    const Bytes    pl = base.Require(cfg.body_mds).Payload();
    const auto     frames = MdsFrames(pl);
    const int      hub = static_cast<int>(std::ranges::find(frames, spin.node) - frames.begin());
    Mot            dm = ReadMot(base, cfg.body_mot);
    Track         *t = dm.TrackBy(static_cast<std::uint32_t>(hub), 0);

    if (t == nullptr) {
        throw std::runtime_error("spin: the hub has no rotation track");
    }

    const double rate = spin.rate;
    const double h = spin.handoff;
    const auto [loop_lo, loop_hi] = spin.loop;
    std::map<int, double> keys;

    {
        const auto [lo, hi] = spin.ramp_up;
        const double n = hi - lo;
        const double turns = std::max(1.0, std::nearbyint(rate * n * 2.0 / 3.0 / 360.0));
        const double total = 360.0 * turns;
        const double a = (rate * n - 2.0 * total) / (n * n * n);
        const double b = (3.0 * total - rate * n) / (n * n);

        for (int fr = lo; fr <= hi; fr++) {
            const double x = fr - lo;
            keys[fr] = a * x * x * x + b * x * x;
        }
    }

    for (int fr = loop_lo; fr <= loop_hi; fr++) {
        keys[fr] = h + (fr - loop_lo) * rate;
    }

    {
        const auto [lo, hi] = spin.ramp_down;
        const double        n = hi - lo;
        std::vector<double> totals;

        for (int k = 1; k <= 8; k++) {
            totals.push_back(k * 360.0);
        }

        const double target = h + rate * n * 2.0 / 3.0;
        std::ranges::stable_sort(totals, [&](double a, double b) { return std::abs(a - target) < std::abs(b - target); });
        bool solved = false;

        for (double total : totals) {
            const double d = total - h - rate * n;
            const double c3 = (-rate * n - 2.0 * d) / (n * n * n);
            const double c2 = (d - c3 * n * n * n) / (n * n);
            auto         theta = [&](double x) { return c3 * x * x * x + c2 * x * x + rate * x + h; };
            auto         speed = [&](double x) { return 3.0 * c3 * x * x + 2.0 * c2 * x + rate; };
            std::vector<double> points = {0.0, n};

            if (c3 != 0.0) {
                const double x = -c2 / (3.0 * c3);

                if (x > 0.0 && x < n) {
                    points.push_back(x);
                }
            }

            const bool rising = std::ranges::all_of(points, [&](double x) { return speed(x) > -1e-9; });
            double     step = 0.0;

            for (int k = 0; k < static_cast<int>(n); k++) {
                step = std::max(step, theta(k + 1) - theta(k));
            }

            if (rising && step < 90.0) {
                for (int fr = lo; fr <= hi; fr++) {
                    keys[fr] = theta(fr - lo);
                }

                solved = true;
                break;
            }
        }

        if (!solved) {
            throw std::runtime_error("spin: no ramp down fits");
        }
    }

    for (const Key &k : t->keys) {
        if (keys.contains(static_cast<int>(k.Frame()))) {
            throw std::runtime_error("spin: a spin window runs into the hub's held keys");
        }
    }

    const Vec4 qb = MatToQuat(MdsBind(pl, hub).rows);

    for (const auto &[fr, deg] : keys) {
        const double th = deg * (3.14159265358979323846 / 180.0);
        const double s = std::sin(th / 2.0);
        const Vec4   q = QuatMul({std::cos(th / 2.0), spin.axis[0] * s, spin.axis[1] * s, spin.axis[2] * s}, qb);
        t->keys.push_back(NewKey(static_cast<std::uint32_t>(fr), q));
    }

    t->Sort();
    std::optional<Vec4> prev;

    for (Key &k : t->keys) {
        Vec4 v = k.Value();

        if (prev && Dot(*prev, v) < 0.0) {
            for (double &x : v) {
                x = -x;
            }

            k.SetValue(v);
        }

        prev = v;
    }

    WriteMot(base, cfg.body_mot, dm);
}

// The KEY lines between KEY_START and MOTION_END, one per slot in slot order.
void RewriteKeys(Record &cfg, const std::vector<Slot> &slots) {
    std::string       text = Text(cfg.Payload());
    const std::string eol = LineEnd(text);
    std::vector<Slot> ordered = slots;
    std::ranges::sort(ordered, {}, &Slot::idx);
    std::string keys;

    for (const Slot &s : ordered) {
        keys += std::format("KEY\t{},\t{},\t{:.2f}, //{}{}", s.lo, s.hi, s.speed, std::string_view(s.name).substr(0, 12), eol);
    }

    const std::size_t start = text.find("KEY_START");

    if (start == std::string::npos) {
        throw std::runtime_error("the cfg has no KEY_START");
    }

    const std::size_t line = text.find(eol, start) + eol.size();
    const std::size_t end = text.find("MOTION_END", line);

    if (end == std::string::npos) {
        throw std::runtime_error("the cfg has no MOTION_END");
    }

    text = text.substr(0, line) + keys + text.substr(end);
    cfg.ReplacePayload(FromText(text));
}

void Assemble(const Character &ch, Pack &base, const Reader &read, std::vector<std::string> &log) {
    std::map<std::string, Pack> cache;
    auto                        source = [&](const char *path) -> Pack & {
        auto found = cache.find(path);

        if (found != cache.end()) {
            return found->second;
        }

        Bytes bytes;

        if (!read(path, bytes) || bytes.empty()) {
            throw std::runtime_error(std::format("{} is not in the game data", path));
        }

        return cache.emplace(path, ParsePack(bytes)).first->second;
    };

    if (ch.shadow_inject) {
        InjectShadow(base, source(ch.shadow_inject->src), *ch.shadow_inject);
        log.push_back(std::format("  shadow added from {}", Basename(ch.shadow_inject->src)));
    }

    const CfgInfo cfg = CfgOf(base);
    int           max_frame = 0;

    for (const Slot &s : ch.slots) {
        max_frame = std::max(max_frame, s.hi);
    }

    if (ch.mesh_graft) {
        std::vector<Span> windows;

        for (const Slot &s : ch.slots) {
            if (s.src != nullptr && std::string_view(s.src) == ch.mesh_graft->src) {
                windows.emplace_back(s.lo, s.hi);
            }
        }

        GraftMeshNodes(base, source(ch.mesh_graft->src), *ch.mesh_graft, windows, max_frame);
        log.push_back(std::format("  helicopter: {} nodes added from {}", ch.mesh_graft->nodes.size(), Basename(ch.mesh_graft->src)));
    }

    std::vector<Span> root_spans;

    if (ch.root_motion) {
        std::vector<Span> windows;

        for (const Slot &s : ch.slots) {
            if (s.src != nullptr || s.hold) {
                windows.emplace_back(s.lo, s.hi);
            }
        }

        root_spans = FoldSpans(windows, 1, max_frame);
    }

    for (const Slot &s : ch.slots) {
        if (s.src != nullptr) {
            Pack   &src = source(s.src);
            CfgInfo scfg = CfgOf(src, s.src_cfg != nullptr ? s.src_cfg : "");
            Graft(base, cfg.body_mot, cfg.body_mds, src, scfg.body_mot, scfg.body_mds, s, ch.root_motion, root_spans, log);

            if (!cfg.shadow_mot.empty() && !scfg.shadow_mot.empty() && !cfg.shadow_mds.empty() && !scfg.shadow_mds.empty()) {
                Graft(base, cfg.shadow_mot, cfg.shadow_mds, src, scfg.shadow_mot, scfg.shadow_mds, s, ch.root_motion, root_spans, log);
            }
        }

        if (s.hold) {
            BakeHold(base, cfg.body_mot, *s.hold, s.lo, s.hi);

            if (!cfg.shadow_mot.empty()) {
                BakeHold(base, cfg.shadow_mot, *s.hold, s.lo, s.hi);
            }
        }

        if (s.root_offset) {
            ApplyRootOffset(base, cfg.body_mot, s.lo, s.hi, *s.root_offset);

            if (!cfg.shadow_mot.empty()) {
                ApplyRootOffset(base, cfg.shadow_mot, s.lo, s.hi, *s.root_offset);
            }
        }

        if (s.reverse) {
            ReverseWindow(base, cfg.body_mot, s.lo, s.hi);

            if (!cfg.shadow_mot.empty()) {
                ReverseWindow(base, cfg.shadow_mot, s.lo, s.hi);
            }
        }

        if (s.root_freeze) {
            FreezeRoots(base, cfg.body_mot, *s.root_freeze, s.lo, s.hi);

            if (!cfg.shadow_mot.empty()) {
                FreezeRoots(base, cfg.shadow_mot, *s.root_freeze, s.lo, s.hi);
            }
        }

        if (!s.pin_nodes.empty()) {
            PinNodes(base, cfg.body_mot, cfg.body_mds, s.pin_nodes, s.pin_frame, s.lo, s.hi);
        }
    }

    if (ch.mesh_graft) {
        ApplySpin(base, *ch.mesh_graft);
        log.push_back("  propeller spin added");
    }

    RewriteKeys(*CfgOf(base).cfg, ch.slots);
    log.push_back(std::format("  {} KEYs written", ch.slots.size()));
}

const Character *CharacterFor(int ally) {
    static const std::vector<Character> chars = Characters();
    static constexpr std::array<const char *, 6> kNames = {"", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

    if (ally <= 0 || ally >= static_cast<int>(kNames.size())) {
        return nullptr;
    }

    for (const Character &ch : chars) {
        if (std::string_view(ch.name) == kNames[ally]) {
            return &ch;
        }
    }

    return nullptr;
}

} // namespace

const char *BaseModel(int ally) {
    const Character *ch = CharacterFor(ally);
    return ch != nullptr ? ch->base : nullptr;
}

bool Build(int ally, const Bytes &base, const Reader &read, Bytes &out, std::vector<std::string> &log) {
    out.clear();
    const Character *ch = CharacterFor(ally);

    if (ch == nullptr) {
        return false;
    }

    try {
        Pack pack = ParsePack(base);

        if (pack.records.empty()) {
            throw std::runtime_error("the base model is not a pack");
        }

        log.push_back(std::format("{}: building on {} ({} bytes)", ch->name, ch->base, base.size()));
        Assemble(*ch, pack, read, log);
        out = RebuildPack(pack);

        // The rebuilt pack must read back the way the game reads it.
        Pack        check = ParsePack(out);
        CfgInfo     cfg = CfgOf(check);
        const Mot   mot = ReadMot(check, cfg.body_mot);
        std::size_t keys = 0;

        for (const Track &t : mot.tracks) {
            keys += t.keys.size();
        }

        log.push_back(std::format("{}: done, {} -> {} bytes, {} motion tracks, {} keys", ch->name, base.size(), out.size(), mot.tracks.size(), keys));
        return true;
    } catch (const std::exception &error) {
        log.push_back(std::format("{}: not built: {}", ch->name, error.what()));
        out.clear();
        return false;
    }
}

} // namespace dc::game::moveset

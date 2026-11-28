#include "expanded_town_maps.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "character.hpp"
#include "collision.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "expanded_town_map_data.hpp"
#include "gamepad.hpp"
#include "main.hpp"
#include "mainselect.hpp"
#include "platform/paths.hpp"

/* Ported from Dark Cloud Expanded's IsoPatch/ (TownSceneBakes.cs, BrownbooCollision.cs,
 * TownCollisionBakes.cs) and Formats/ (SceneScn.cs, CollisionMdt.cs, CollisionMdsWriter.cs), BSD
 * 2-Clause, Copyright (c) 2023, Gundorada Workshop; the full notice is in
 * expanded_town_map_data.cpp. The byte layouts and the order of the steps are Expanded's. */

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

// A line for the console, tagged, and for town.txt.
void Note(std::string_view line) {
    std::printf("[TownMap] %.*s\n", static_cast<int>(line.size()), line.data());
    LogLine("town.txt", std::format("[TownMap] {}", line));
}

// A patch that finds the bytes it expects missing stops with this; the scene keeps the step
// before it.
struct Mismatch : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ── Bytes ──────────────────────────────────────────────────────────────────────────────────────

using Bytes = std::vector<std::uint8_t>;

void Need(const Bytes &b, std::size_t at, std::size_t n, std::string_view what) {
    if (at > b.size() || n > b.size() - at) {
        throw Mismatch(std::format("{} reads past the scene ({:#x} + {})", what, at, n));
    }
}

std::uint32_t U32(const Bytes &b, std::size_t at) {
    Need(b, at, 4, "U32");
    std::uint32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

std::int32_t I32(const Bytes &b, std::size_t at) {
    return static_cast<std::int32_t>(U32(b, at));
}

float F32(const Bytes &b, std::size_t at) {
    Need(b, at, 4, "F32");
    float v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

void PutU32(Bytes &b, std::size_t at, std::uint32_t v) {
    Need(b, at, 4, "PutU32");
    std::memcpy(b.data() + at, &v, 4);
}

void PutU32(Bytes &out, std::uint32_t v) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(&v);
    out.insert(out.end(), p, p + 4);
}

void PutF32(Bytes &b, std::size_t at, float v) {
    Need(b, at, 4, "PutF32");
    std::memcpy(b.data() + at, &v, 4);
}

void Pad16(Bytes &b) {
    while (b.size() % 16 != 0) {
        b.push_back(0);
    }
}

std::size_t Align16(std::size_t x) {
    return (x + 15) & ~std::size_t{15};
}

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

std::size_t FindFrom(const Bytes &hay, std::string_view needle, std::size_t from, std::size_t end = kNone) {
    end = std::min(end, hay.size());
    if (needle.empty() || from >= end || needle.size() > end - from) {
        return kNone;
    }
    auto first = hay.begin() + static_cast<std::ptrdiff_t>(from);
    auto last = hay.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::search(first, last, needle.begin(), needle.end(),
                          [](std::uint8_t a, char c) { return a == static_cast<std::uint8_t>(c); });
    return it == last ? kNone : static_cast<std::size_t>(it - hay.begin());
}

std::size_t Find(const Bytes &hay, std::string_view needle) {
    return FindFrom(hay, needle, 0);
}

std::size_t FindLast(const Bytes &hay, std::string_view needle, std::size_t before) {
    before = std::min(before, hay.size());
    if (needle.size() > before) {
        return kNone;
    }
    for (std::size_t at = before - needle.size() + 1; at-- > 0;) {
        if (std::memcmp(hay.data() + at, needle.data(), needle.size()) == 0) {
            return at;
        }
    }
    return kNone;
}

// A node name with its null terminator, as the name fields hold it.
std::string Z(std::string_view name) {
    std::string key(name);
    key.push_back('\0');
    return key;
}

std::string NameAt(const Bytes &b, std::size_t at, std::size_t max) {
    std::string out;
    for (std::size_t i = 0; i < max && at + i < b.size() && b[at + i] != 0; i++) {
        out.push_back(static_cast<char>(b[at + i]));
    }
    return out;
}

bool IsTag(const Bytes &b, std::size_t at, char a, char c2, char c3) {
    return at + 3 <= b.size() && b[at] == a && b[at + 1] == c2 && b[at + 2] == c3;
}

// ── The scene directory (SceneScn.DirectoryList) ───────────────────────────────────────────────

struct DirEntry {
    std::string name;
    std::size_t off = 0;
    std::size_t size = 0;
    std::size_t entry_off = 0;
};

std::vector<DirEntry> Directory(const Bytes &scn) {
    // The count at +4 bounds it too, so a sub-file's first bytes are never read as an entry.
    std::vector<DirEntry> out;
    const std::size_t     count = U32(scn, 4);
    for (std::size_t o = 0x10; o + 0x30 <= scn.size() && out.size() < count; o += 0x30) {
        std::string name = NameAt(scn, o, 16);
        if (name.empty() || !std::isalnum(static_cast<unsigned char>(name[0]))) {
            break;
        }
        out.push_back({name, U32(scn, o + 0x10), U32(scn, o + 0x14), o});
    }
    return out;
}

const DirEntry &Sub(const std::vector<DirEntry> &dir, std::string_view name) {
    for (const DirEntry &e : dir) {
        if (e.name == name) {
            return e;
        }
    }
    throw Mismatch(std::format("{} not in the scene directory", name));
}

// ── Geometry ───────────────────────────────────────────────────────────────────────────────────

using Vec = std::array<double, 3>;
using Tri = std::array<Vec, 3>;
using Mat = std::array<double, 16>; // column-major, as the MDS stores it

Mat Compose(const Mat &a, const Mat &b) {
    Mat r{};
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            double s = 0;
            for (int k = 0; k < 4; k++) {
                s += a[k * 4 + row] * b[c * 4 + k];
            }
            r[c * 4 + row] = s;
        }
    }
    return r;
}

Vec Xform(const Mat &m, const Vec &p) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

Vec Centroid(const Tri &t) {
    return {(t[0][0] + t[1][0] + t[2][0]) / 3, (t[0][1] + t[1][1] + t[2][1]) / 3, (t[0][2] + t[1][2] + t[2][2]) / 3};
}

std::vector<Tri> TrisFrom(const map_data::TriTable &table) {
    std::vector<Tri> out;
    out.reserve(table.count);
    for (std::size_t r = 0; r < table.count; r++) {
        const double *row = table.rows[r];
        out.push_back({
            Vec{row[0], row[1], row[2]},
            Vec{row[3], row[4], row[5]},
            Vec{row[6], row[7], row[8]}
        });
    }
    return out;
}

// Rounded to `scale` steps (10 = a tenth), half to even.
std::int64_t Step(double x, double scale) {
    return static_cast<std::int64_t>(std::nearbyint(x * scale));
}

// ExactMath.Round: Python's round(x, nd), on the double's exact value with ties to even. to_chars
// rounds that way, so its decimal string read back is the same double. (+0.0 folds -0 into 0.)
double ExactRound(double x, int nd) {
    char buf[64];
    auto [end, ec] = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, nd);
    if (ec != std::errc{}) {
        return x;
    }
    double out = x;
    std::from_chars(buf, end, out);
    return out + 0.0;
}

// CollisionGeom.TriKeyRounded(t, 1): the three corners to a tenth, sorted, so winding doesn't count.
using TriKey = std::array<std::int64_t, 9>;

TriKey KeyTenth(const Tri &t) {
    std::array<std::array<std::int64_t, 3>, 3> v{};
    for (int k = 0; k < 3; k++) {
        for (int a = 0; a < 3; a++) {
            v[k][a] = Step(t[k][a], 10.0);
        }
    }
    std::ranges::sort(v);
    TriKey key{};
    for (int k = 0; k < 3; k++) {
        for (int a = 0; a < 3; a++) {
            key[k * 3 + a] = v[k][a];
        }
    }
    return key;
}

// CollisionMdt.KdSplit(tris, max, proportional: true): split along the longest centroid axis,
// each side getting its share of ceil(n / max) leaves.
void KdSplit(std::vector<Tri> ts, std::size_t max_tris, std::vector<std::vector<Tri>> &out) {
    const std::size_t k = (ts.size() + max_tris - 1) / max_tris;
    if (k <= 1) {
        out.push_back(std::move(ts));
        return;
    }
    std::vector<Vec> cs;
    cs.reserve(ts.size());
    for (const Tri &t : ts) {
        cs.push_back(Centroid(t));
    }
    int    axis = 0;
    double best = -std::numeric_limits<double>::infinity();
    for (int a = 0; a < 3; a++) {
        auto [lo, hi] = std::ranges::minmax(cs, {}, [a](const Vec &c) { return c[a]; });
        double ext = hi[a] - lo[a];
        if (ext > best) {
            best = ext;
            axis = a;
        }
    }
    std::vector<std::size_t> order(ts.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return cs[i][axis]; });
    const auto       half = static_cast<double>(k / 2);
    std::size_t      mid = static_cast<std::size_t>(std::nearbyint(static_cast<double>(ts.size()) * half / static_cast<double>(k)));
    std::vector<Tri> left, right;
    for (std::size_t i = 0; i < order.size(); i++) {
        (i < mid ? left : right).push_back(ts[order[i]]);
    }
    KdSplit(std::move(left), max_tris, out);
    KdSplit(std::move(right), max_tris, out);
}

// ── Collision MDTs and MDS blocks ──────────────────────────────────────────────────────────────

// CollisionMdt.Parse: total @w2, verts @w4 (XYZW), display list @w10 with the triangle count at
// DL+0x14 and five-int records from DL+0x18.
std::vector<Tri> ParseCollisionMdt(const Bytes &scn, std::size_t mdt) {
    std::vector<Tri> out;
    if (U32(scn, mdt) != 0x54444d) {
        return out;
    }
    const std::size_t pos = U32(scn, mdt + 0x10);
    const std::size_t dl = U32(scn, mdt + 0x28);
    const std::size_t tc = U32(scn, mdt + dl + 0x14);
    const std::size_t rb = mdt + dl + 0x18;
    auto              vert = [&](std::int32_t i) {
        std::size_t at = mdt + pos + static_cast<std::size_t>(i) * 0x10;
        return Vec{F32(scn, at), F32(scn, at + 4), F32(scn, at + 8)};
    };
    for (std::size_t t = 0; t < tc; t++) {
        std::int32_t a = I32(scn, rb + t * 0x14), b = I32(scn, rb + t * 0x14 + 4), c = I32(scn, rb + t * 0x14 + 8);
        if (a < 0 || b < 0 || c < 0) {
            throw Mismatch("collision MDT with a negative index");
        }
        out.push_back({vert(a), vert(b), vert(c)});
    }
    return out;
}

// CollisionMdt.Build (no colour block): vertices merged on their 3-decimal rounding, one display
// list record per triangle.
// CollisionMdt.Build: vertices merged on their 3-decimal rounding, one display list record per
// triangle; with `attrs`, each triangle's 16-byte colour entry (deduplicated) in a colour block.
Bytes BuildCollisionMdt(const std::vector<Tri> &tris, double y_shift = 0.0, const std::vector<Bytes> *attrs = nullptr) {
    std::map<std::array<double, 3>, std::uint32_t> index;
    std::vector<Vec>                               verts;
    std::vector<std::array<std::uint32_t, 3>>      idx;
    for (const Tri &t : tris) {
        std::array<std::uint32_t, 3> tri{};
        for (int k = 0; k < 3; k++) {
            const Vec                   p = {t[k][0], t[k][1] + y_shift, t[k][2]};
            const std::array<double, 3> key = {ExactRound(p[0], 3), ExactRound(p[1], 3), ExactRound(p[2], 3)};
            auto [it, inserted] = index.try_emplace(key, static_cast<std::uint32_t>(verts.size()));
            if (inserted) {
                verts.push_back(p);
            }
            tri[k] = it->second;
        }
        idx.push_back(tri);
    }
    std::vector<Bytes>         entries;
    std::vector<std::uint32_t> tri_colour;
    if (attrs != nullptr) {
        if (attrs->size() != idx.size()) {
            throw Mismatch(std::format("{} colour entries for {} triangles", attrs->size(), idx.size()));
        }
        std::map<Bytes, std::uint32_t> seen;
        for (const Bytes &e : *attrs) {
            if (e.size() != 0x10) {
                throw Mismatch("a colour entry isn't 16 bytes");
            }
            auto [it, inserted] = seen.try_emplace(e, static_cast<std::uint32_t>(entries.size()));
            if (inserted) {
                entries.push_back(e);
            }
            tri_colour.push_back(it->second);
        }
    }
    const std::size_t vc = verts.size(), tc = idx.size(), pos = 0x40;
    const std::size_t dl = Align16(pos + vc * 16);
    std::size_t       body_end = dl + 6 * 4 + tc * 0x14, colour_off = 0;
    if (attrs != nullptr) {
        colour_off = Align16(body_end);
        body_end = colour_off + 0x10 * entries.size();
    }
    const std::size_t   total = Align16(body_end);
    Bytes               mdt(total, 0);
    const std::uint32_t header[16] = {0x0054444d, 0x40, static_cast<std::uint32_t>(total), static_cast<std::uint32_t>(vc),
                                      static_cast<std::uint32_t>(pos), static_cast<std::uint32_t>(2 * tc), 0, 0, 0, 0,
                                      static_cast<std::uint32_t>(dl), 0, 0, 1, static_cast<std::uint32_t>(colour_off), 0};
    for (int i = 0; i < 16; i++) {
        PutU32(mdt, static_cast<std::size_t>(i) * 4, header[i]);
    }
    for (std::size_t i = 0; i < vc; i++) {
        PutF32(mdt, pos + i * 16, static_cast<float>(verts[i][0]));
        PutF32(mdt, pos + i * 16 + 4, static_cast<float>(verts[i][1]));
        PutF32(mdt, pos + i * 16 + 8, static_cast<float>(verts[i][2]));
        PutF32(mdt, pos + i * 16 + 12, 1.0f);
    }
    std::size_t o = dl;
    for (std::uint32_t w : {0x003400b8u, 0x10u, static_cast<std::uint32_t>(tc), 0x20202020u, 3u, static_cast<std::uint32_t>(tc)}) {
        PutU32(mdt, o, w);
        o += 4;
    }
    for (std::size_t t = 0; t < tc; t++) {
        for (std::uint32_t v : idx[t]) {
            PutU32(mdt, o, v);
            o += 4;
        }
        PutU32(mdt, o, attrs != nullptr ? tri_colour[t] : 0u);
        o += 8; // colour index, pad
    }
    for (std::size_t e = 0; e < entries.size(); e++) {
        std::memcpy(mdt.data() + colour_off + e * 0x10, entries[e].data(), 0x10);
    }
    return mdt;
}

struct Named {
    std::string        name;
    std::vector<Tri>   tris;
    std::vector<Bytes> attrs = {}; // per-triangle 16-byte colour entries (event-trigger tags), or none
};

// CollisionMdsWriter.BuildFlatMds: node 0 the root, the rest its children, identity frames.
Bytes BuildFlatMds(const std::vector<Named> &named) {
    const std::size_t n = named.size();
    Bytes             out(0x10 + n * 0x70, 0);
    out[0] = 'M';
    out[1] = 'D';
    out[2] = 'S';
    PutU32(out, 4, 1);
    PutU32(out, 8, static_cast<std::uint32_t>(n));
    PutU32(out, 12, 0x10);
    Bytes       blob;
    std::size_t cur = 0x10 + n * 0x70;
    for (std::size_t i = 0; i < n; i++) {
        const std::size_t node = 0x10 + i * 0x70;
        PutU32(out, node + 4, 0x70);
        for (std::size_t c = 0; c < std::min<std::size_t>(15, named[i].name.size()); c++) {
            out[node + 8 + c] = static_cast<std::uint8_t>(named[i].name[c]);
        }
        Bytes mdt = BuildCollisionMdt(named[i].tris, 0.0, named[i].attrs.empty() ? nullptr : &named[i].attrs);
        PutU32(out, node + 0x28, static_cast<std::uint32_t>(cur));
        PutU32(out, node + 0x2c, i == 0 ? 0xffffffffu : 0u);
        static constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (int k = 0; k < 16; k++) {
            PutF32(out, node + 0x30 + static_cast<std::size_t>(k) * 4, kIdentity[k]);
        }
        blob.insert(blob.end(), mdt.begin(), mdt.end());
        cur += mdt.size();
    }
    out.insert(out.end(), blob.begin(), blob.end());
    return out;
}

bool IsVariantChar(std::uint8_t c) {
    return std::isalnum(c) || c == '_';
}

// The `<name><suffix>.mds` variant's MDS block, as an offset in the sub-file, or kNone
// (CollisionMdsWriter.VariantOff: the offset word sits 3 bytes past the name's terminator).
std::size_t VariantOff(const Bytes &scn, const DirEntry &sub, std::string_view name, std::string_view suffix) {
    const std::string key = std::format("{}{}.mds", name, suffix) + '\0';
    const std::size_t at = FindFrom(scn, key, sub.off, sub.off + sub.size);
    if (at == kNone) {
        return kNone;
    }
    const std::size_t field = at + key.size() + 3;
    if (field + 4 > sub.off + sub.size) {
        return kNone;
    }
    const std::size_t off = U32(scn, field);
    return off > 0 && off < sub.size && IsTag(scn, sub.off + off, 'M', 'D', 'S') ? off : kNone;
}

struct Variant {
    std::size_t field; // where its offset word sits in the scene
    std::size_t off;   // its MDS block, as an offset in the sub-file
};

// Every `*.mds` variant named in the sub-file whose offset word points at an MDS block.
std::vector<Variant> Variants(const Bytes &scn, const DirEntry &sub) {
    std::vector<Variant> out;
    const std::size_t    end = sub.off + sub.size;
    const std::string    tail = std::string(".mds") + '\0';
    for (std::size_t at = FindFrom(scn, tail, sub.off, end); at != kNone; at = FindFrom(scn, tail, at + tail.size(), end)) {
        if (at == sub.off || !IsVariantChar(scn[at - 1])) {
            continue;
        }
        const std::size_t field = at + tail.size() + 3;
        if (field + 4 > end) {
            continue;
        }
        const std::size_t off = U32(scn, field);
        if (off > 0 && off < sub.size && IsTag(scn, sub.off + off, 'M', 'D', 'S')) {
            out.push_back({field, off});
        }
    }
    return out;
}

// CollisionMdsWriter.ReplaceABlock: swaps a sub-file's whole `<name><suffix>` MDS block; the
// variant offsets after it and the scene directory follow.
Bytes ReplaceBlock(const Bytes &scn, std::string_view sub_name, Bytes mds, std::string_view suffix) {
    const std::vector<DirEntry> dir = Directory(scn);
    const DirEntry             &sub = Sub(dir, sub_name);
    const std::size_t           vo = VariantOff(scn, sub, sub_name, suffix);
    if (vo == kNone) {
        throw Mismatch(std::format("{}: no {}", sub_name, suffix));
    }
    const std::vector<Variant> variants = Variants(scn, sub);
    std::size_t                next = sub.size;
    for (const Variant &v : variants) {
        if (v.off > vo) {
            next = std::min(next, v.off);
        }
    }
    const std::size_t old_size = next - vo;
    Pad16(mds);
    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(mds.size()) - static_cast<std::ptrdiff_t>(old_size);
    Bytes                out;
    out.reserve(scn.size() + mds.size());
    out.insert(out.end(), scn.begin(), scn.begin() + static_cast<std::ptrdiff_t>(sub.off + vo));
    out.insert(out.end(), mds.begin(), mds.end());
    out.insert(out.end(), scn.begin() + static_cast<std::ptrdiff_t>(sub.off + vo + old_size), scn.end());
    for (const Variant &v : variants) {
        if (v.off > vo) {
            PutU32(out, v.field, static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(v.off) + delta));
        }
    }
    for (const DirEntry &e : dir) {
        if (e.off == sub.off) {
            PutU32(out, e.entry_off + 0x14, static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(e.size) + delta));
        } else if (e.off > sub.off) {
            PutU32(out, e.entry_off + 0x10, static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(e.off) + delta));
        }
    }
    return out;
}

// CollisionMdsWriter.AppendVariantNodes: new collision nodes on a sub-file's variant, the vanilla
// nodes and their MDTs kept byte for byte; the new table entries are clones of node 1.
Bytes AppendVariantNodes(const Bytes &scn, std::string_view sub_name, const std::vector<std::pair<std::string, Bytes>> &add,
                         std::string_view suffix) {
    constexpr std::size_t       kTemplateNode = 1;
    const std::vector<DirEntry> dir = Directory(scn);
    const DirEntry             &sub = Sub(dir, sub_name);
    const std::size_t           vo = VariantOff(scn, sub, sub_name, suffix);
    if (vo == kNone) {
        throw Mismatch(std::format("{}: no {}", sub_name, suffix));
    }
    std::size_t next = sub.size;
    for (const Variant &v : Variants(scn, sub)) {
        if (vo < v.off && v.off < sub.size) {
            next = std::min(next, v.off);
        }
    }
    const std::size_t mds_size = next - vo;
    const std::size_t base = sub.off + vo;
    const std::size_t cnt = U32(scn, base + 8), tbl = U32(scn, base + 12);
    if (cnt <= kTemplateNode) {
        throw Mismatch(std::format("{}{} has {} nodes", sub_name, suffix, cnt));
    }
    Need(scn, base, tbl + cnt * 0x70, "variant table");
    std::vector<std::pair<std::size_t, std::size_t>> order; // (mesh offset, node)
    for (std::size_t i = 0; i < cnt; i++) {
        const std::int32_t mo = I32(scn, base + tbl + i * 0x70 + 0x28);
        if (mo != 0) {
            order.emplace_back(static_cast<std::size_t>(mo), i);
        }
    }
    std::ranges::stable_sort(order, {}, [](const auto &p) { return p.first; });
    Bytes out(scn.begin() + static_cast<std::ptrdiff_t>(base), scn.begin() + static_cast<std::ptrdiff_t>(base + tbl + cnt * 0x70));
    for (std::size_t k = 0; k < add.size(); k++) {
        Bytes entry(scn.begin() + static_cast<std::ptrdiff_t>(base + tbl + kTemplateNode * 0x70),
                    scn.begin() + static_cast<std::ptrdiff_t>(base + tbl + (kTemplateNode + 1) * 0x70));
        PutU32(entry, 0, static_cast<std::uint32_t>(cnt + k));
        for (std::size_t c = 0; c < 16; c++) {
            entry[8 + c] = c < add[k].first.size() && c < 15 ? static_cast<std::uint8_t>(add[k].first[c]) : 0;
        }
        out.insert(out.end(), entry.begin(), entry.end());
    }
    std::vector<std::pair<std::size_t, std::uint32_t>> patches;
    for (std::size_t k = 0; k < order.size(); k++) {
        const std::size_t from = order[k].first;
        const std::size_t to = k + 1 < order.size() ? order[k + 1].first : mds_size;
        if (to < from || to > mds_size) {
            throw Mismatch("variant MDTs out of order");
        }
        const std::size_t at = out.size();
        out.insert(out.end(), scn.begin() + static_cast<std::ptrdiff_t>(base + from), scn.begin() + static_cast<std::ptrdiff_t>(base + to));
        Pad16(out);
        patches.emplace_back(tbl + order[k].second * 0x70 + 0x28, static_cast<std::uint32_t>(at));
    }
    for (std::size_t k = 0; k < add.size(); k++) {
        const std::size_t at = out.size();
        out.insert(out.end(), add[k].second.begin(), add[k].second.end());
        Pad16(out);
        patches.emplace_back(tbl + (cnt + k) * 0x70 + 0x28, static_cast<std::uint32_t>(at));
    }
    PutU32(out, 8, static_cast<std::uint32_t>(cnt + add.size()));
    for (const auto &[at, value] : patches) {
        PutU32(out, at, value);
    }
    return ReplaceBlock(scn, sub_name, std::move(out), suffix);
}

// ── Brownboo: the edit-view cleanup (TownSceneBakes) ───────────────────────────────────────────

constexpr std::array<std::string_view, 12> kUpperWallNodes = {
    "s04g0105__n",
    "s04g0106__n",
    "s04g0107__n",
    "s04g0108__n",
    "s04g0109__n",
    "s04g0110__n",
    "s04g0111__n",
    "s04g0112__n",
    "s04g0113__n",
    "s04g0114__n",
    "s04g0115__n",
    "s04g0116__n",
};

struct CornerNode {
    std::string_view name;
    double           y_max; // only triangles with their centre below this go
};

constexpr std::array<CornerNode, 18> kCornerTriNodes = {
    {
     {"s040101__s", 1e9},
     {"s04g0102__s", 1e9},
     {"s04g0103__s", 1e9},
     {"s04g0104__s", 1e9},
     {"s04g0105__n", 1e9},
     {"s04g0106__n", 1e9},
     {"s04g0107__n", 1e9},
     {"s04g0108__n", 1e9},
     {"s04g0109__n", 1e9},
     {"s04g0110__n", 1e9},
     {"s04g0111__n", 1e9},
     {"s04g0112__n", 1e9},
     {"s04g0113__n", 1e9},
     {"s04g0114__n", 1e9},
     {"s04g0115__n", 1e9},
     {"s04g0116__n", 1e9},
     {"s04g0117__s", -50.0},
     {"s04g0117__s1", -50.0},
     }
};

// IsoBytes.FindMdt: the node's name field sits 8 into its MDS entry, the mesh word at 0x28; the
// word is absolute or relative to the owning MDS block.
std::size_t FindMdt(const Bytes &scn, std::string_view node) {
    const std::size_t at = Find(scn, Z(node));
    if (at == kNone || at < 8) {
        throw Mismatch(std::format("node {} not found", node));
    }
    const std::int32_t mesh = I32(scn, at - 8 + 0x28);
    const std::size_t  mds = FindLast(scn, std::string_view("MDS\0", 4), at);
    for (std::int64_t cand : {static_cast<std::int64_t>(mesh), static_cast<std::int64_t>(mds == kNone ? 0 : mds) + mesh}) {
        if (cand > 0 && static_cast<std::size_t>(cand) + 3 < scn.size() && IsTag(scn, static_cast<std::size_t>(cand), 'M', 'D', 'T')) {
            return static_cast<std::size_t>(cand);
        }
    }
    throw Mismatch(std::format("MDT for {} not resolved", node));
}

bool UpFacing(const Bytes &s, std::size_t vbase, std::int32_t i0, std::int32_t i1, std::int32_t i2) {
    auto         v = [&](std::int32_t i, int a) { return static_cast<double>(F32(s, vbase + static_cast<std::size_t>(i) * 0x10 + static_cast<std::size_t>(a) * 4)); };
    const double ax = v(i0, 0), ay = v(i0, 1), az = v(i0, 2);
    const double bx = v(i1, 0), by = v(i1, 1), bz = v(i1, 2);
    const double cx = v(i2, 0), cy = v(i2, 1), cz = v(i2, 2);
    const double nx = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
    const double ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);
    const double nz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    return len > 0 && ny / len > 0.9;
}

// RemoveRingCornerTris: each crater ring's four up-facing corner fills, and the crater floors'
// sunken strays below Y -50, collapsed to zero-area triangles in their triangle lists. Runs before
// the wall cull, which renames the upper rings.
int RemoveRingCornerTris(Bytes &scn) {
    int removed = 0;
    for (const CornerNode &node : kCornerTriNodes) {
        const std::size_t   mdt = FindMdt(scn, node.name);
        const std::size_t   dl = U32(scn, mdt + 10 * 4);
        const auto          vcount = static_cast<std::int32_t>(U32(scn, mdt + 3 * 4));
        const std::size_t   vbase = mdt + U32(scn, mdt + 4 * 4);
        const std::uint32_t colour = U32(scn, mdt + 8 * 4);
        const std::size_t   rb = (colour != 0xffffffffu && colour > 0 ? 4 : 3) * 4;
        const std::size_t   numsub = U32(scn, mdt + dl + 8);
        std::size_t         o = dl + 0x10;
        for (std::size_t sm = 0; sm < numsub; sm++) {
            const std::int32_t prim = I32(scn, mdt + o);
            const std::int32_t vcnt = I32(scn, mdt + o + 4);
            if (vcnt < 0) {
                throw Mismatch(std::format("{}: bad submesh", node.name));
            }
            o += 0xC;
            const std::size_t recbase = mdt + o;
            o += static_cast<std::size_t>(vcnt) * rb;
            if (prim != 3) {
                continue;
            }
            for (std::int32_t k = 0; k + 2 < vcnt; k += 3) {
                const std::int32_t i0 = I32(scn, recbase + static_cast<std::size_t>(k) * rb);
                const std::int32_t i1 = I32(scn, recbase + static_cast<std::size_t>(k + 1) * rb);
                const std::int32_t i2 = I32(scn, recbase + static_cast<std::size_t>(k + 2) * rb);
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= vcount || i1 >= vcount || i2 >= vcount) {
                    continue;
                }
                if (i0 == i1 || i1 == i2 || i0 == i2) {
                    continue;
                }
                const double cy = (static_cast<double>(F32(scn, vbase + static_cast<std::size_t>(i0) * 0x10 + 4)) +
                                   F32(scn, vbase + static_cast<std::size_t>(i1) * 0x10 + 4) +
                                   F32(scn, vbase + static_cast<std::size_t>(i2) * 0x10 + 4)) /
                                  3.0;
                if (cy >= node.y_max || !UpFacing(scn, vbase, i0, i1, i2)) {
                    continue;
                }
                PutU32(scn, recbase + static_cast<std::size_t>(k + 1) * rb, static_cast<std::uint32_t>(i0));
                PutU32(scn, recbase + static_cast<std::size_t>(k + 2) * rb, static_cast<std::uint32_t>(i0));
                removed++;
            }
        }
    }
    return removed;
}

// CullUpperCraterWalls: the upper rings' "__n" suffix becomes "__s" (single-sided), as the lower
// rings have it. One byte per node.
void CullUpperCraterWalls(Bytes &scn) {
    std::vector<std::size_t> at;
    for (std::string_view node : kUpperWallNodes) {
        const std::size_t found = Find(scn, Z(node));
        if (found == kNone) {
            throw Mismatch(std::format("crater wall {} not found", node));
        }
        at.push_back(found + node.size() - 1);
    }
    for (std::size_t p : at) {
        scn[p] = 's';
    }
}

// CullBuildings: the houses single-sided too, so a camera inside one sees through it. The "__n"
// houses flip to "__s"; the ones without a suffix get "__s" in their name field's zero padding.
void CullBuildings(Bytes &scn) {
    std::vector<std::size_t> flips;
    for (std::string_view node : {"h0101__n", "h0102__n", "h0103__n"}) {
        const std::size_t found = Find(scn, Z(node));
        if (found == kNone) {
            throw Mismatch(std::format("house {} not found", node));
        }
        flips.push_back(found + std::string_view(node).size() - 1);
    }
    std::vector<std::size_t> appends;
    for (auto [node, expect] : {
             std::pair<std::string_view, int>{"h0104", 1},
             {"h0301", 3},
             {"h0302", 3}
    }) {
        const std::string key = Z(node);
        int               hits = 0;
        for (std::size_t at = Find(scn, key); at != kNone; at = FindFrom(scn, key, at + node.size())) {
            Need(scn, at + node.size(), 4, "house name");
            for (std::size_t c = 0; c < 4; c++) {
                if (scn[at + node.size() + c] != 0) {
                    throw Mismatch(std::format("house {}: its name field isn't zero-padded", node));
                }
            }
            appends.push_back(at + node.size());
            hits++;
        }
        if (hits != expect) {
            throw Mismatch(std::format("house {}: found {}, expected {}", node, hits, expect));
        }
    }
    for (std::size_t p : flips) {
        scn[p] = 's';
    }
    for (std::size_t p : appends) {
        std::memcpy(scn.data() + p, "__s\0", 4);
    }
}

// ── Brownboo: the collision (BrownbooCollision) ────────────────────────────────────────────────

// s04g01_v's nodes and their triangles in world space (s04g01 is placed at the origin).
std::vector<Named> VanillaVNodes(const Bytes &scn) {
    const std::vector<DirEntry> dir = Directory(scn);
    const DirEntry             &sub = Sub(dir, "s04g01");
    const std::size_t           vo = VariantOff(scn, sub, "s04g01", "_v");
    std::vector<Named>          out;
    if (vo == kNone) {
        return out;
    }
    const std::size_t mds = sub.off + vo;
    const std::size_t cnt = U32(scn, mds + 8), tbl = U32(scn, mds + 12);

    struct Node {
        std::string  name;
        std::int32_t mesh = 0, parent = -1;
        Mat          mat{};
    };

    std::vector<Node> nodes(cnt);
    for (std::size_t i = 0; i < cnt; i++) {
        const std::size_t b = mds + tbl + i * 0x70;
        for (std::size_t k = 0; k < 16; k++) {
            nodes[i].mat[k] = F32(scn, b + 0x30 + k * 4);
        }
        nodes[i].name = NameAt(scn, b + 8, 16);
        nodes[i].mesh = I32(scn, b + 0x28);
        nodes[i].parent = I32(scn, b + 0x2c);
    }
    std::vector<Mat>  world(cnt);
    std::vector<bool> done(cnt, false);
    auto              wm = [&](auto &&self, std::size_t i, int depth) -> const Mat              &{
        if (depth > 64) {
            throw Mismatch("s04g01_v node parents loop");
        }
        if (!done[i]) {
            const std::int32_t p = nodes[i].parent;
            world[i] = (p < 0 || static_cast<std::size_t>(p) >= cnt) ? nodes[i].mat : Compose(self(self, static_cast<std::size_t>(p), depth + 1), nodes[i].mat);
            done[i] = true;
        }
        return world[i];
    };
    for (std::size_t i = 0; i < cnt; i++) {
        if (nodes[i].mesh == 0) {
            continue;
        }
        std::size_t fo = kNone;
        for (std::int64_t cand : {static_cast<std::int64_t>(nodes[i].mesh), static_cast<std::int64_t>(mds) + nodes[i].mesh}) {
            if (cand > 0 && static_cast<std::size_t>(cand) + 3 < scn.size() && IsTag(scn, static_cast<std::size_t>(cand), 'M', 'D', 'T')) {
                fo = static_cast<std::size_t>(cand);
                break;
            }
        }
        if (fo == kNone) {
            continue;
        }
        const Mat       &m = wm(wm, i, 0);
        std::vector<Tri> tris = ParseCollisionMdt(scn, fo);
        for (Tri &t : tris) {
            for (Vec &p : t) {
                p = Xform(m, p);
            }
        }
        if (!tris.empty()) {
            out.push_back({nodes[i].name, std::move(tris)});
        }
    }
    return out;
}

// obj56 with the lumpy iwa01 rock (its triangles inside the padded box, clear of the central
// cylinder) swapped for the smooth hull; the walkway strip through the tunnel stays.
std::vector<Tri> Iwa01RingObj56(const std::vector<Named> &v_nodes) {
    const Named *obj56 = nullptr;
    for (const Named &n : v_nodes) {
        if (n.name == "obj56") {
            obj56 = &n;
            break;
        }
    }
    if (obj56 == nullptr) {
        throw Mismatch("s04g01_v has no obj56");
    }
    const double    *box = map_data::kBrownbooIwa01SelBox;
    std::vector<Tri> sel;
    for (const Tri &t : obj56->tris) {
        bool in = std::ranges::all_of(t, [&](const Vec &p) {
            return box[0] <= p[0] && p[0] <= box[1] && box[2] <= p[2] && p[2] <= box[3] && std::sqrt(p[0] * p[0] + p[2] * p[2]) > 83;
        });
        if (in) {
            sel.push_back(t);
        }
    }
    std::vector<Vec> keep_centres;
    for (const Tri &t : TrisFrom(map_data::kBrownbooIwa01KeepTris)) {
        keep_centres.push_back(Centroid(t));
    }
    std::set<TriKey> drop;
    for (const Tri &t : sel) {
        drop.insert(KeyTenth(t));
    }
    for (const Tri &t : sel) {
        const Vec c = Centroid(t);
        bool      kept = std::ranges::any_of(keep_centres, [&](const Vec &k) {
            return std::max({std::abs(c[0] - k[0]), std::abs(c[1] - k[1]), std::abs(c[2] - k[2])}) < 0.2;
        });
        if (kept) {
            drop.erase(KeyTenth(t));
        }
    }
    std::vector<Tri> out;
    for (const Tri &t : obj56->tris) {
        if (!drop.contains(KeyTenth(t))) {
            out.push_back(t);
        }
    }
    std::vector<Tri> hull = TrisFrom(map_data::kBrownbooIwa01Hull);
    out.insert(out.end(), hull.begin(), hull.end());
    return out;
}

Bytes RebuildCameraHull(const Bytes &scn) {
    const std::vector<Named> v_nodes = VanillaVNodes(scn);
    const Named             *v = nullptr;
    for (const Named &n : v_nodes) {
        if (n.name == "v") {
            v = &n;
            break;
        }
    }
    if (v == nullptr) {
        throw Mismatch("s04g01_v has no node v (already patched?)");
    }
    std::vector<Named> named = {
        {"v", v->tris}
    };
    std::vector<std::vector<Tri>> leaves;
    KdSplit(Iwa01RingObj56(v_nodes), 100, leaves);
    for (std::size_t i = 0; i < leaves.size(); i++) {
        named.push_back({std::format("c56_{:02}", i), std::move(leaves[i])});
    }
    std::size_t tri_count = 0;
    for (const Named &n : named) {
        tri_count += n.tris.size();
    }
    Bytes out = ReplaceBlock(scn, "s04g01", BuildFlatMds(named), "_v");
    Note(std::format("s04: s04g01_v rebuilt: {} nodes, {} tris, scene {} -> {} bytes", named.size(), tri_count, scn.size(), out.size()));
    return out;
}

Bytes AddPondRocks(const Bytes &scn) {
    if (Find(scn, "rock_iwa01") != kNone) {
        throw Mismatch("s04g01_a already has the rock nodes");
    }
    std::vector<std::pair<std::string, Bytes>> rocks = {
        {"rock_iwa01", BuildCollisionMdt(TrisFrom(map_data::kBrownbooRockIwa01))},
        {"rock_iwa02", BuildCollisionMdt(TrisFrom(map_data::kBrownbooRockIwa02))},
        {"rock_iwa03", BuildCollisionMdt(TrisFrom(map_data::kBrownbooRockIwa03))},
    };
    Bytes out = AppendVariantNodes(scn, "s04g01", rocks, "_a");
    Note(std::format("s04: pond rocks added to s04g01_a, scene {} -> {} bytes", scn.size(), out.size()));
    return out;
}

// ── Yellow Drops: the water (TownSceneBakes) ───────────────────────────────────────────────────

// The water goes from 0 up to the bank notch's lip. Expanded's fishing spot 23 sits 1 above it.
constexpr float kYellowDropsWaterY = 4.25f;

// RaiseYellowDropsSurfaceMesh: the suimenn sheet (s1302) raised by its node matrix's Y
// translation, entry 1 of the sub-file's first MDS (0x244 into the sub).
void RaiseYellowDropsSurfaceMesh(Bytes &scn) {
    const std::size_t n = U32(scn, 4);
    for (std::size_t i = 0; i < n; i++) {
        const std::size_t ent = 0x10 + i * 0x30;
        Need(scn, ent, 6, "directory entry");
        if (std::memcmp(scn.data() + ent, "s1302\0", 6) != 0) {
            continue;
        }
        const std::size_t off = U32(scn, ent + 0x10) + 0x244;
        const float       cur = F32(scn, off);
        if (std::abs(cur) > 0.001f) {
            throw Mismatch(std::format("the suimenn sheet's height is {}, expected about 0", cur));
        }
        PutF32(scn, off, kYellowDropsWaterY);
        return;
    }
    throw Mismatch("s1302 not in the scene directory");
}

// ── Visual MDTs (Formats/MdtCodec.cs) ──────────────────────────────────────────────────────────

using Vec4 = std::array<double, 4>;

// A visual MDT parsed to its blocks and rebuilt in their recorded order with the same padding.
// Header words: 2 total, 3 vertex count, 4 positions, 6 UVs, 8 colours (none when 0 or from
// 0x80000000; then records are four ints), 9 display list size, 10 display list, 12 normals,
// 14 materials (0x60 each). The display list is a 0x10 preamble (word 2 the submesh count), then
// per submesh [prim, count, material] and its records [pos, uv, norm(, col)].
struct MdtMesh {
    std::array<std::uint32_t, 16> hdr{};
    bool                          has_col = false;
    std::vector<Vec4>             pos, uv, norm, col;

    struct Submesh {
        std::int32_t                  prim = 3, mat = 0;
        std::vector<std::vector<int>> recs;
    };

    std::vector<Submesh>         subs;
    std::array<std::int32_t, 4>  preamble{};
    std::vector<Bytes>           materials;
    std::vector<std::string>     order;
    std::map<std::string, Bytes> pads;

    int Stride() const { return has_col ? 4 : 3; }
};

std::vector<Vec4> ReadVec16(const Bytes &s, std::size_t b, std::size_t n) {
    std::vector<Vec4> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; i++) {
        v.push_back({F32(s, b + i * 16), F32(s, b + i * 16 + 4), F32(s, b + i * 16 + 8), F32(s, b + i * 16 + 12)});
    }
    return v;
}

MdtMesh ParseMdt(const Bytes &scn, std::size_t fo) {
    if (!IsTag(scn, fo, 'M', 'D', 'T')) {
        throw Mismatch("not an MDT");
    }
    MdtMesh m;
    for (std::size_t i = 0; i < 16; i++) {
        m.hdr[i] = U32(scn, fo + i * 4);
    }
    const std::size_t   total = m.hdr[2];
    const std::size_t   pos = m.hdr[4], uv = m.hdr[6], dlsz = m.hdr[9], dl = m.hdr[10], norm = m.hdr[12], mat = m.hdr[14];
    const std::uint32_t colour = m.hdr[8];
    m.has_col = colour > 0 && colour < 0x80000000u;
    const std::size_t stride = static_cast<std::size_t>(m.Stride());
    if (!(0x40 <= total && total <= scn.size() - fo)) {
        throw Mismatch(std::format("MDT total size {:#x} out of range", total));
    }
    for (std::size_t off : {pos, uv, dl, norm, mat}) {
        if (off != 0 && !(0x40 <= off && off < total)) {
            throw Mismatch(std::format("MDT block offset {:#x} outside the MDT", off));
        }
    }
    for (std::size_t i = 0; i < 4; i++) {
        m.preamble[i] = I32(scn, fo + dl + i * 4);
    }
    const std::int32_t numsub = m.preamble[2];
    if (!(0 < numsub && static_cast<std::size_t>(numsub) <= m.hdr[3] + 1)) {
        throw Mismatch(std::format("implausible submesh count {}", numsub));
    }
    std::size_t       o = dl + 0x10;
    const std::size_t dl_end = (0 < dlsz && dlsz <= total) ? dl + dlsz : total;
    int               max_uv = -1, max_norm = -1, max_col = -1;
    for (std::int32_t k = 0; k < numsub; k++) {
        if (o + 0xC > total) {
            throw Mismatch("submesh header past the MDT");
        }
        MdtMesh::Submesh sm;
        sm.prim = I32(scn, fo + o);
        const std::int32_t vcnt = I32(scn, fo + o + 4);
        sm.mat = I32(scn, fo + o + 8);
        o += 0xC;
        if (vcnt < 0 || o + static_cast<std::size_t>(vcnt) * stride * 4 > dl_end + 0x10) {
            throw Mismatch(std::format("submesh count {} overruns the display list", vcnt));
        }
        for (std::int32_t r = 0; r < vcnt; r++) {
            std::vector<int> rec(stride);
            for (std::size_t c = 0; c < stride; c++) {
                rec[c] = I32(scn, fo + o + (static_cast<std::size_t>(r) * stride + c) * 4);
            }
            max_uv = std::max(max_uv, rec[1]);
            max_norm = std::max(max_norm, rec[2]);
            if (m.has_col) {
                max_col = std::max(max_col, rec[3]);
            }
            sm.recs.push_back(std::move(rec));
        }
        o += static_cast<std::size_t>(vcnt) * stride * 4;
        m.subs.push_back(std::move(sm));
    }
    const std::size_t npos = m.hdr[3];
    m.pos = ReadVec16(scn, fo + pos, npos);
    m.uv = ReadVec16(scn, fo + uv, static_cast<std::size_t>(max_uv + 1));
    if (norm > 0) {
        m.norm = ReadVec16(scn, fo + norm, static_cast<std::size_t>(max_norm + 1));
    }
    if (m.has_col) {
        m.col = ReadVec16(scn, fo + colour, static_cast<std::size_t>(max_col + 1));
    }
    if (mat > total) {
        throw Mismatch("materials past the MDT");
    }
    const std::size_t nmat = (total - mat) / 0x60;
    for (std::size_t i = 0; i < nmat; i++) {
        Need(scn, fo + mat + i * 0x60, 0x60, "material");
        m.materials.emplace_back(scn.begin() + static_cast<std::ptrdiff_t>(fo + mat + i * 0x60),
                                 scn.begin() + static_cast<std::ptrdiff_t>(fo + mat + (i + 1) * 0x60));
    }
    std::vector<std::pair<std::string, std::size_t>> named = {
        {"POS", pos},
        {"DL",  dl },
        {"UV",  uv }
    };
    if (norm > 0) {
        named.emplace_back("NORM", norm);
    }
    named.emplace_back("MAT", mat);
    if (m.has_col) {
        named.emplace_back("COL", colour);
    }
    std::ranges::stable_sort(named, {}, [](const auto &x) { return x.second; });
    std::map<std::string, std::size_t> sizes = {
        {"POS",  npos * 16         },
        {"DL",   dlsz              },
        {"UV",   m.uv.size() * 16  },
        {"NORM", m.norm.size() * 16},
        {"COL",  m.col.size() * 16 },
        {"MAT",  nmat * 0x60       }
    };
    for (std::size_t i = 0; i < named.size(); i++) {
        m.order.push_back(named[i].first);
        const std::size_t next = i + 1 < named.size() ? named[i + 1].second : total;
        const std::size_t a = named[i].second + sizes[named[i].first];
        Bytes             pad;
        if (next > a) {
            Need(scn, fo + a, next - a, "MDT padding");
            pad.assign(scn.begin() + static_cast<std::ptrdiff_t>(fo + a), scn.begin() + static_cast<std::ptrdiff_t>(fo + next));
        }
        m.pads[named[i].first] = std::move(pad);
    }
    return m;
}

Bytes VecBytes(const std::vector<Vec4> &vecs) {
    Bytes b(vecs.size() * 16, 0);
    for (std::size_t i = 0; i < vecs.size(); i++) {
        for (std::size_t k = 0; k < 4; k++) {
            PutF32(b, i * 16 + k * 4, static_cast<float>(vecs[i][k]));
        }
    }
    return b;
}

Bytes DlBytes(const MdtMesh &m) {
    Bytes b;
    PutU32(b, static_cast<std::uint32_t>(m.preamble[0]));
    PutU32(b, static_cast<std::uint32_t>(m.preamble[1]));
    PutU32(b, static_cast<std::uint32_t>(m.subs.size()));
    PutU32(b, static_cast<std::uint32_t>(m.preamble[3]));
    for (const auto &sm : m.subs) {
        PutU32(b, static_cast<std::uint32_t>(sm.prim));
        PutU32(b, static_cast<std::uint32_t>(sm.recs.size()));
        PutU32(b, static_cast<std::uint32_t>(sm.mat));
        for (const auto &r : sm.recs) {
            for (int c = 0; c < m.Stride(); c++) {
                PutU32(b, static_cast<std::uint32_t>(r[static_cast<std::size_t>(c)]));
            }
        }
    }
    return b;
}

Bytes BuildMdt(const MdtMesh &m) {
    std::map<std::string, Bytes> block;
    block["POS"] = VecBytes(m.pos);
    block["DL"] = DlBytes(m);
    block["UV"] = VecBytes(m.uv);
    block["NORM"] = VecBytes(m.norm);
    block["COL"] = m.has_col ? VecBytes(m.col) : Bytes{};
    for (const Bytes &mat : m.materials) {
        block["MAT"].insert(block["MAT"].end(), mat.begin(), mat.end());
    }
    Bytes                              out(0x40, 0);
    std::map<std::string, std::size_t> offs;
    for (const std::string &name : m.order) {
        offs[name] = out.size();
        out.insert(out.end(), block[name].begin(), block[name].end());
        if (auto it = m.pads.find(name); it != m.pads.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
    auto hw = m.hdr;
    hw[2] = static_cast<std::uint32_t>(out.size());
    hw[3] = static_cast<std::uint32_t>(m.pos.size());
    hw[4] = static_cast<std::uint32_t>(offs["POS"]);
    hw[6] = static_cast<std::uint32_t>(offs["UV"]);
    hw[8] = m.has_col ? static_cast<std::uint32_t>(offs["COL"]) : m.hdr[8];
    hw[9] = static_cast<std::uint32_t>(block["DL"].size());
    hw[10] = static_cast<std::uint32_t>(offs["DL"]);
    hw[12] = !m.norm.empty() ? static_cast<std::uint32_t>(offs["NORM"]) : 0;
    hw[14] = static_cast<std::uint32_t>(offs["MAT"]);
    for (std::size_t i = 0; i < 16; i++) {
        PutU32(out, i * 4, hw[i]);
    }
    return out;
}

struct MeshTri {
    std::int32_t                    mat;
    std::array<std::vector<int>, 3> recs;
};

// Every submesh's triangles: lists in threes, strips with alternating winding.
std::vector<MeshTri> MeshTriangles(const MdtMesh &m, bool drop_degenerate_strips) {
    std::vector<MeshTri> out;
    for (const auto &sm : m.subs) {
        const auto &r = sm.recs;
        if (sm.prim == 3) {
            for (std::size_t k = 0; k + 2 < r.size(); k += 3) {
                out.push_back({
                    sm.mat, {r[k], r[k + 1], r[k + 2]}
                });
            }
        } else if (sm.prim == 4) {
            for (std::size_t i = 0; i + 2 < r.size(); i++) {
                const auto &a = i % 2 == 0 ? r[i] : r[i + 1];
                const auto &b = i % 2 == 0 ? r[i + 1] : r[i];
                const auto &c = r[i + 2];
                if (drop_degenerate_strips && (a[0] == b[0] || b[0] == c[0] || a[0] == c[0])) {
                    continue;
                }
                out.push_back({
                    sm.mat, {a, b, c}
                });
            }
        } else {
            throw Mismatch(std::format("prim {}", sm.prim));
        }
    }
    return out;
}

// ── Placed meshes (SceneScn.PlacedMeshes), from the parsed mapinfo ──────────────────────────────

struct PlacedMesh {
    std::string                     name, sub;
    int                             inst = 0;
    std::vector<Vec>                verts;
    std::vector<std::array<int, 3>> tris; // SceneScn.Flatten: position indices, strips' degenerates kept
};

std::string BaseName(const char *path) {
    std::string_view base(path);
    if (const auto slash = base.rfind('/'); slash != std::string_view::npos) {
        base.remove_prefix(slash + 1);
    }
    return std::string(base);
}

// Every mesh node of every GROUND or WATER placement, in world space: the node chain, then the
// placement's turn about Y and its position.
std::vector<PlacedMesh> PlacedMeshes(const Bytes &scn, const EDIT_MAP_INFO *info) {
    std::vector<PlacedMesh> out;
    if (info == nullptr) {
        return out;
    }
    const std::vector<DirEntry> dir = Directory(scn);
    std::map<std::string, int>  inst_counter;
    for (const MAP_PARTS_INFO &obj : info->map_objects) {
        if (obj.name[0][0] == '\0' && obj.name[1][0] == '\0' && obj.name[2][0] == '\0' && obj.name[3][0] == '\0') {
            break;
        }
        if (obj.kind != 1 && obj.kind != 0x15) {
            continue;
        }
        const std::string name = BaseName(obj.name[0]);
        const DirEntry   *e = nullptr;
        for (const DirEntry &d : dir) {
            if (d.name == name) {
                e = &d;
                break;
            }
        }
        if (e == nullptr) {
            continue;
        }
        const std::size_t mds = FindFrom(scn, std::string_view("MDS\0", 4), e->off, e->off + e->size);
        if (mds == kNone) {
            continue;
        }
        const int                 inst = inst_counter[name]++;
        const std::size_t         cnt = U32(scn, mds + 8), tbl = U32(scn, mds + 12);
        std::vector<Mat>          local(cnt);
        std::vector<std::int32_t> parent(cnt), mesh(cnt);
        std::vector<std::string>  names(cnt);
        for (std::size_t i = 0; i < cnt; i++) {
            const std::size_t b = mds + tbl + i * 0x70;
            for (std::size_t k = 0; k < 16; k++) {
                local[i][k] = F32(scn, b + 0x30 + k * 4);
            }
            names[i] = NameAt(scn, b + 8, 16);
            mesh[i] = I32(scn, b + 0x28);
            parent[i] = I32(scn, b + 0x2c);
        }
        std::vector<Mat>  world(cnt);
        std::vector<bool> done(cnt, false);
        auto              wm = [&](auto &&self, std::size_t i, int depth) -> const Mat              &{
            if (depth > 64) {
                throw Mismatch("node parents loop");
            }
            if (!done[i]) {
                const std::int32_t p = parent[i];
                world[i] = (p < 0 || static_cast<std::size_t>(p) >= cnt) ? local[i] : Compose(self(self, static_cast<std::size_t>(p), depth + 1), local[i]);
                done[i] = true;
            }
            return world[i];
        };
        const double c = std::cos(static_cast<double>(obj.rotation[1])), s = std::sin(static_cast<double>(obj.rotation[1]));
        for (std::size_t i = 0; i < cnt; i++) {
            if (mesh[i] == 0) {
                continue;
            }
            std::size_t fo = kNone;
            for (std::int64_t cand : {static_cast<std::int64_t>(mesh[i]), static_cast<std::int64_t>(mds) + mesh[i]}) {
                if (cand > 0 && static_cast<std::size_t>(cand) + 3 < scn.size() && IsTag(scn, static_cast<std::size_t>(cand), 'M', 'D', 'T')) {
                    fo = static_cast<std::size_t>(cand);
                    break;
                }
            }
            if (fo == kNone) {
                continue;
            }
            MdtMesh                         m;
            std::vector<std::array<int, 3>> flat;
            try {
                m = ParseMdt(scn, fo);
                for (const MeshTri &t : MeshTriangles(m, false)) {
                    flat.push_back({t.recs[0][0], t.recs[1][0], t.recs[2][0]});
                }
            } catch (const Mismatch &) {
                continue;
            }
            if (flat.empty()) {
                continue;
            }
            const Mat &mm = wm(wm, i, 0);
            PlacedMesh pm{names[i], name, inst, {}, std::move(flat)};
            for (const Vec4 &p : m.pos) {
                const Vec v = Xform(mm, {p[0], p[1], p[2]});
                pm.verts.push_back({v[0] * c + v[2] * s + obj.position[0], v[1] + obj.position[1], -v[0] * s + v[2] * c + obj.position[2]});
            }
            out.push_back(std::move(pm));
        }
    }
    return out;
}

// ── Yellow Drops: the west bank (YellowDropsWestBank) ──────────────────────────────────────────
//
// The bank bulges west by up to 70 (a half sine along z -145..274), baked at twice the station
// density into a rebuilt s1301: the grid10 and grid11 visual meshes, the s1301_a crown wall and
// floor and the s1301_c camera wall, the factory's doumu_c camera ring pulled in, and camera hulls
// for the two torii-gate pillars as new _c nodes. (The fish walls along the new waterline go with
// the new fishing spots.)

constexpr double      kWestBulge = 70.0;
constexpr double      kWbSpan[2] = {-145.0, 274.0};
constexpr int         kWbSubdiv = 1;   // extra stations per segment
constexpr double      kYLocal = 210.0; // s1301's node frames sit at world y + 210
constexpr double      kTol = 0.35;
constexpr std::size_t kUsaS1301Size = 0x4ca50;

// The vanilla west-bank rows in world space, north-west to south-east.
constexpr std::array<Vec, 5> kEdgeTop = {
    {{-426, 23, -142}, {-424, 23, -23}, {-413, 23, 102}, {-388, 23, 206}, {-382, 23, 275}}
};
constexpr std::array<Vec, 5> kEdgeBot = {
    {{-427, -10, -142}, {-424, -10, -23}, {-413, -10, 102}, {-388, -10, 206}, {-382, -10, 275}}
};
constexpr std::array<Vec, 5> kCrown = {
    {{-420, 30, -148}, {-417, 30, -24}, {-404, 30, 103}, {-378, 30, 206}, {-369, 30, 273}}
};
constexpr std::array<Vec, 5> kCam = {
    {{-441, 30, -134}, {-441, 30, -22}, {-430, 30, 103}, {-405, 30, 208}, {-398, 30, 275}}
};

double WbProfile(double z) {
    const double z0 = kWbSpan[0], z1 = kWbSpan[1];
    if (!(z0 < z && z < z1)) {
        return 0.0;
    }
    return -kWestBulge * std::sin(std::numbers::pi * (z - z0) / (z1 - z0));
}

using Chain = std::vector<Vec>;

Chain RowChain(const std::array<Vec, 5> &row, double lo_z, double hi_z) {
    Chain out;
    for (const Vec &p : row) {
        if (lo_z <= p[2] && p[2] <= hi_z) {
            out.push_back({p[0], p[1] + kYLocal, p[2]});
        }
    }
    return out;
}

Chain RowAt(const std::array<Vec, 5> &row, double y) {
    Chain out;
    for (const Vec &p : row) {
        out.push_back({p[0], y, p[2]});
    }
    return out;
}

std::vector<Chain> ChainsFor(std::string_view which) {
    if (which == "grid10") {
        return {RowChain(kEdgeTop, -150, 0), RowChain(kEdgeBot, -150, 0), RowChain(kCrown, -150, 0)};
    }
    if (which == "grid11") {
        return {RowChain(kEdgeTop, -30, 280), RowChain(kEdgeBot, -30, 280), RowChain(kCrown, -30, 280)};
    }
    if (which == "_a") {
        return {RowAt(kCrown, 240.0), RowAt(kCrown, 336.0)};
    }
    return {RowAt(kCam, 240.0), RowAt(kCam, 200.0)}; // _c
}

Vec Lerp3(const Vec &a, const Vec &b, int k) {
    const double t = k / (kWbSubdiv + 1.0);
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

std::vector<std::pair<Vec, Vec>> Segments(const std::vector<Chain> &chains) {
    std::vector<std::pair<Vec, Vec>> segs;
    for (const Chain &ch : chains) {
        for (std::size_t i = 0; i + 1 < ch.size(); i++) {
            segs.emplace_back(ch[i], ch[i + 1]);
        }
    }
    return segs;
}

template <typename P>
bool ColMatch(const P &p, const Vec &s) {
    return std::abs(p[0] - s[0]) <= kTol && std::abs(p[1] - s[1]) <= 1.0 && std::abs(p[2] - s[2]) <= kTol;
}

std::vector<Vec> AllColumns(const std::vector<Chain> &chains) {
    std::vector<Vec> cols;
    for (const Chain &ch : chains) {
        cols.insert(cols.end(), ch.begin(), ch.end());
        for (std::size_t i = 0; i + 1 < ch.size(); i++) {
            for (int k = 1; k <= kWbSubdiv; k++) {
                cols.push_back(Lerp3(ch[i], ch[i + 1], k));
            }
        }
    }
    return cols;
}

template <typename P>
bool OnSegment(const std::vector<std::pair<Vec, Vec>> &segs, const P &pa, const P &pb) {
    for (const auto &[a, b] : segs) {
        if ((ColMatch(pa, a) && ColMatch(pb, b)) || (ColMatch(pa, b) && ColMatch(pb, a))) {
            return true;
        }
    }
    return false;
}

// Rounded to four places for the midpoint keys.
std::int64_t Key4(double t) {
    return Step(t, 10000.0);
}

// EditVisualMdt: every triangle edge along a station segment split at its midpoint (the UVs,
// normals and colours lerped), then every station column shifted west by the bulge.
std::pair<Bytes, int> EditVisualMdt(const Bytes &sub, std::size_t fo, const std::vector<Chain> &chains) {
    MdtMesh                                                m = ParseMdt(sub, fo);
    std::vector<MeshTri>                                   tris = MeshTriangles(m, true);
    const auto                                             segs = Segments(chains);
    std::map<std::tuple<int, int, std::int64_t>, int>      midpos;
    std::map<std::tuple<int, int, std::int64_t, int>, int> midattr;
    auto                                                   block = [&](int b) -> std::vector<Vec4>                                                   &{ return b == 0 ? m.uv : b == 1 ? m.norm
                                                                                                                                     : m.col; };
    auto                                                   lerp_entry = [&](int b, int ia, int ib, double t) {
        const auto key = std::make_tuple(std::min(ia, ib), std::max(ia, ib), Key4(t), b);
        if (auto it = midattr.find(key); it != midattr.end()) {
            return it->second;
        }
        auto &lst = block(b);
        if (ia < 0 || ib < 0 || static_cast<std::size_t>(ia) >= lst.size() || static_cast<std::size_t>(ib) >= lst.size()) {
            throw Mismatch("record index outside its block");
        }
        const Vec4   va = lst[static_cast<std::size_t>(ia)], vb = lst[static_cast<std::size_t>(ib)];
        const double t2 = ia > ib ? 1.0 - t : t;
        Vec4         nv{};
        for (std::size_t j = 0; j < 4; j++) {
            nv[j] = va[j] + (vb[j] - va[j]) * t2;
        }
        lst.push_back(nv);
        midattr[key] = static_cast<int>(lst.size() - 1);
        return midattr[key];
    };
    auto mid_rec = [&](const std::vector<int> &ra, const std::vector<int> &rb, double t) {
        const auto key = std::make_tuple(std::min(ra[0], rb[0]), std::max(ra[0], rb[0]), Key4(ra[0] < rb[0] ? t : 1 - t));
        if (!midpos.contains(key)) {
            const Vec4 pa = m.pos.at(static_cast<std::size_t>(ra[0])), pb = m.pos.at(static_cast<std::size_t>(rb[0]));
            Vec4       np{};
            for (std::size_t j = 0; j < 4; j++) {
                np[j] = pa[j] + (pb[j] - pa[j]) * t;
            }
            m.pos.push_back(np);
            midpos[key] = static_cast<int>(m.pos.size() - 1);
        }
        std::vector<int> rec = {midpos[key], lerp_entry(0, ra[1], rb[1], t)};
        rec.push_back(!m.norm.empty() ? lerp_entry(1, ra[2], rb[2], t) : 0);
        if (m.has_col) {
            rec.push_back(lerp_entry(2, ra[3], rb[3], t));
        }
        return rec;
    };
    auto seg_of_edge = [&](int ia, int ib) {
        return OnSegment(segs, m.pos.at(static_cast<std::size_t>(ia)), m.pos.at(static_cast<std::size_t>(ib)));
    };
    bool changed = true;
    for (int guard = 0; changed && guard < 30; guard++) {
        changed = false;
        std::vector<MeshTri> out;
        for (const MeshTri &tri : tris) {
            bool done = false;
            for (int e = 0; e < 3; e++) {
                const auto &ra = tri.recs[static_cast<std::size_t>(e)];
                const auto &rb = tri.recs[static_cast<std::size_t>((e + 1) % 3)];
                const auto &rc = tri.recs[static_cast<std::size_t>((e + 2) % 3)];
                if (seg_of_edge(ra[0], rb[0])) {
                    const auto mrec = mid_rec(ra, rb, 0.5);
                    out.push_back({
                        tri.mat, {ra, mrec, rc}
                    });
                    out.push_back({
                        tri.mat, {mrec, rb, rc}
                    });
                    changed = done = true;
                    break;
                }
            }
            if (!done) {
                out.push_back(tri);
            }
        }
        tris = std::move(out);
    }
    const auto cols = AllColumns(chains);
    int        moved = 0;
    for (Vec4 &p : m.pos) {
        for (const Vec &c : cols) {
            if (ColMatch(p, c)) {
                p[0] += WbProfile(p[2]);
                moved++;
                break;
            }
        }
    }
    std::vector<MdtMesh::Submesh> bymat;
    for (const MeshTri &tri : tris) {
        auto it = std::ranges::find_if(bymat, [&](const auto &s) { return s.mat == tri.mat; });
        if (it == bymat.end()) {
            bymat.push_back({3, tri.mat, {}});
            it = bymat.end() - 1;
        }
        it->recs.insert(it->recs.end(), tri.recs.begin(), tri.recs.end());
    }
    m.subs = std::move(bymat);
    m.hdr[5] = static_cast<std::uint32_t>(m.uv.size());
    m.hdr[11] = static_cast<std::uint32_t>(m.norm.size());
    if (m.has_col) {
        m.hdr[7] = static_cast<std::uint32_t>(m.col.size());
    }
    std::size_t dl_len = 16;
    for (const auto &sm : m.subs) {
        dl_len += 12 + sm.recs.size() * static_cast<std::size_t>(m.Stride()) * 4;
    }
    std::size_t mat_len = 0;
    for (const Bytes &b : m.materials) {
        mat_len += b.size();
    }
    std::map<std::string, std::size_t> lens = {
        {"POS",  16 * m.pos.size()                },
        {"UV",   16 * m.uv.size()                 },
        {"NORM", 16 * m.norm.size()               },
        {"COL",  m.has_col ? 16 * m.col.size() : 0},
        {"DL",   dl_len                           },
        {"MAT",  mat_len                          }
    };
    for (const std::string &name : m.order) {
        m.pads[name] = Bytes((16 - lens[name] % 16) % 16, 0);
    }
    Bytes out = BuildMdt(m);
    for (std::size_t w : {4, 6, 10, 12, 14}) {
        if (U32(out, w * 4) % 16 != 0) {
            throw Mismatch(std::format("rebuilt MDT block {:#x} misaligned", U32(out, w * 4)));
        }
    }
    return {std::move(out), moved};
}

// EditCollMdt: the same split and shift on a collision MDT; the new vertices and records are
// appended and the header pointed at them.
std::pair<Bytes, int> EditCollMdt(const Bytes &sub, std::size_t fo, const std::vector<Chain> &chains) {
    const std::size_t                        pos_off = U32(sub, fo + 0x10), dl = U32(sub, fo + 0x28), total = U32(sub, fo + 8);
    const std::size_t                        tc = U32(sub, fo + dl + 0x14);
    std::vector<std::array<std::int32_t, 5>> recs;
    int                                      nv = 0;
    for (std::size_t t = 0; t < tc; t++) {
        std::array<std::int32_t, 5> r{};
        for (std::size_t k = 0; k < 5; k++) {
            r[k] = I32(sub, fo + dl + 0x18 + t * 0x14 + k * 4);
        }
        for (std::size_t k = 0; k < 3; k++) {
            if (r[k] < 0) {
                throw Mismatch("collision record with a negative index");
            }
            nv = std::max(nv, r[k] + 1);
        }
        recs.push_back(r);
    }
    std::vector<Vec4>                  pos = ReadVec16(sub, fo + pos_off, static_cast<std::size_t>(nv));
    const auto                         segs = Segments(chains);
    std::map<std::pair<int, int>, int> midcache;
    auto                               midpoint = [&](int ia, int ib) {
        const auto key = std::make_pair(std::min(ia, ib), std::max(ia, ib));
        if (!midcache.contains(key)) {
            const Vec4 a = pos[static_cast<std::size_t>(ia)], b = pos[static_cast<std::size_t>(ib)];
            pos.push_back({(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2});
            midcache[key] = static_cast<int>(pos.size() - 1);
        }
        return midcache[key];
    };
    bool changed = true;
    for (int guard = 0; changed && guard < 30; guard++) {
        changed = false;
        std::vector<std::array<std::int32_t, 5>> out;
        for (const auto &r : recs) {
            bool done = false;
            for (int e = 0; e < 3; e++) {
                const int ia = r[static_cast<std::size_t>(e)], ib = r[static_cast<std::size_t>((e + 1) % 3)];
                if (OnSegment(segs, pos[static_cast<std::size_t>(ia)], pos[static_cast<std::size_t>(ib)])) {
                    const int mi = midpoint(ia, ib);
                    auto      r1 = r, r2 = r;
                    r1[static_cast<std::size_t>((e + 1) % 3)] = mi;
                    r2[static_cast<std::size_t>(e)] = mi;
                    out.push_back(r1);
                    out.push_back(r2);
                    changed = done = true;
                    break;
                }
            }
            if (!done) {
                out.push_back(r);
            }
        }
        recs = std::move(out);
    }
    const auto cols = AllColumns(chains);
    int        moved = 0;
    for (Vec4 &p : pos) {
        for (const Vec &c : cols) {
            if (ColMatch(p, c)) {
                p[0] += WbProfile(p[2]);
                moved++;
                break;
            }
        }
    }
    Need(sub, fo, total, "collision MDT");
    Bytes o(sub.begin() + static_cast<std::ptrdiff_t>(fo), sub.begin() + static_cast<std::ptrdiff_t>(fo + total));
    Pad16(o);
    const std::size_t new_pos = o.size();
    for (const Vec4 &p : pos) {
        for (std::size_t k = 0; k < 4; k++) {
            float       f = static_cast<float>(p[k]);
            const auto *b = reinterpret_cast<const std::uint8_t *>(&f);
            o.insert(o.end(), b, b + 4);
        }
    }
    const std::size_t new_dl = o.size();
    Need(sub, fo + dl, 0x18, "collision display list");
    o.insert(o.end(), sub.begin() + static_cast<std::ptrdiff_t>(fo + dl), sub.begin() + static_cast<std::ptrdiff_t>(fo + dl + 0x18));
    for (const auto &r : recs) {
        for (std::int32_t v : r) {
            PutU32(o, static_cast<std::uint32_t>(v));
        }
    }
    Pad16(o);
    PutU32(o, 0x08, static_cast<std::uint32_t>(o.size()));
    PutU32(o, 0x0c, static_cast<std::uint32_t>(pos.size()));
    PutU32(o, 0x10, static_cast<std::uint32_t>(new_pos));
    PutU32(o, 0x28, static_cast<std::uint32_t>(new_dl));
    PutU32(o, new_dl + 0x14, static_cast<std::uint32_t>(recs.size()));
    return {std::move(o), moved};
}

// RelayMds: the MDS at mds_off rebuilt with replacement MDTs by node name, plus new nodes cloned
// from node 1 with their own name, index and mesh offset. With no edits it gives the block back
// byte for byte, which is checked first.
Bytes RelayMds(const Bytes &sub, std::size_t mds_off, std::size_t mds_size, const std::map<std::string, Bytes> &edits,
               const std::vector<std::pair<std::string, Bytes>> &add = {}) {
    constexpr std::size_t kTemplateNode = 1;
    const std::size_t     cnt = U32(sub, mds_off + 8), tbl = U32(sub, mds_off + 12);
    Need(sub, mds_off, tbl + cnt * 0x70, "MDS table");

    struct Node {
        std::size_t i;
        std::string name;
        std::size_t mo;
    };

    std::vector<Node> order;
    for (std::size_t i = 0; i < cnt; i++) {
        const std::size_t  b = mds_off + tbl + i * 0x70;
        const std::int32_t mo = I32(sub, b + 0x28);
        if (mo != 0) {
            order.push_back({i, NameAt(sub, b + 8, 16), static_cast<std::size_t>(mo)});
        }
    }
    std::ranges::stable_sort(order, {}, &Node::mo);
    if (!add.empty() && cnt <= kTemplateNode) {
        throw Mismatch("MDS too small to clone a node from");
    }
    Bytes out(sub.begin() + static_cast<std::ptrdiff_t>(mds_off), sub.begin() + static_cast<std::ptrdiff_t>(mds_off + tbl + cnt * 0x70));
    for (std::size_t k = 0; k < add.size(); k++) {
        Bytes ent(sub.begin() + static_cast<std::ptrdiff_t>(mds_off + tbl + kTemplateNode * 0x70),
                  sub.begin() + static_cast<std::ptrdiff_t>(mds_off + tbl + (kTemplateNode + 1) * 0x70));
        PutU32(ent, 0, static_cast<std::uint32_t>(cnt + k));
        for (std::size_t c = 0; c < 16; c++) {
            ent[8 + c] = c < add[k].first.size() && c < 15 ? static_cast<std::uint8_t>(add[k].first[c]) : 0;
        }
        out.insert(out.end(), ent.begin(), ent.end());
    }
    PutU32(out, 8, static_cast<std::uint32_t>(cnt + add.size()));
    std::vector<std::pair<std::size_t, std::uint32_t>> patches;
    for (std::size_t k = 0; k < order.size(); k++) {
        const std::size_t from = order[k].mo;
        const std::size_t to = k + 1 < order.size() ? order[k + 1].mo : mds_size;
        if (to < from || to > mds_size) {
            throw Mismatch("MDS meshes out of order");
        }
        const std::size_t at = out.size();
        if (auto it = edits.find(order[k].name); it != edits.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        } else {
            out.insert(out.end(), sub.begin() + static_cast<std::ptrdiff_t>(mds_off + from), sub.begin() + static_cast<std::ptrdiff_t>(mds_off + to));
        }
        Pad16(out);
        patches.emplace_back(tbl + order[k].i * 0x70 + 0x28, static_cast<std::uint32_t>(at));
    }
    for (std::size_t k = 0; k < add.size(); k++) {
        const std::size_t at = out.size();
        out.insert(out.end(), add[k].second.begin(), add[k].second.end());
        Pad16(out);
        patches.emplace_back(tbl + (cnt + k) * 0x70 + 0x28, static_cast<std::uint32_t>(at));
    }
    for (const auto &[at, value] : patches) {
        PutU32(out, at, value);
    }
    return out;
}

// ── The pillar camera hulls and the doumu hug ──────────────────────────────────────────────────

constexpr double kPillarPad = 8.0;
constexpr int    kHullN = 8;
constexpr double kHullYLo = -10.0, kHullYHi = 130.0;
constexpr double kBandLo = 20.0, kBandHi = 45.0;
constexpr double kDoumuPull = 20.0, kDoumuClear = 4.0;
constexpr double kDoumuC[2] = {-2.0, -6.0};

using P2 = std::array<double, 2>;

int Mod(int a, int m) {
    return ((a % m) + m) % m;
}

double Round2(double x) {
    return std::nearbyint(x * 100.0) / 100.0;
}

// Splits a pillar pair's points in two at the widest gap along their main axis.
std::array<std::vector<P2>, 2> Cluster2(const std::vector<P2> &pts) {
    const std::size_t n = pts.size();
    if (n < 4) {
        throw Mismatch(std::format("only {} pillar points", n));
    }
    double cx = 0, cz = 0;
    for (const P2 &p : pts) {
        cx += p[0];
    }
    cx /= static_cast<double>(n);
    for (const P2 &p : pts) {
        cz += p[1];
    }
    cz /= static_cast<double>(n);
    double sxx = 0, szz = 0, sxz = 0;
    for (const P2 &p : pts) {
        sxx += (p[0] - cx) * (p[0] - cx);
    }
    for (const P2 &p : pts) {
        szz += (p[1] - cz) * (p[1] - cz);
    }
    for (const P2 &p : pts) {
        sxz += (p[0] - cx) * (p[1] - cz);
    }
    const double                       ang = 0.5 * std::atan2(2 * sxz, sxx - szz), ax = std::cos(ang), az = std::sin(ang);
    std::vector<std::pair<double, P2>> proj;
    for (const P2 &p : pts) {
        proj.emplace_back(p[0] * ax + p[1] * az, p);
    }
    std::ranges::stable_sort(proj, [](const auto &a, const auto &b) {
        return std::tie(a.first, a.second[0], a.second[1]) < std::tie(b.first, b.second[0], b.second[1]);
    });
    std::size_t gap_i = 0;
    double      gap = -1.0;
    for (std::size_t i = n / 4; i < 3 * n / 4; i++) {
        const double d = proj[i + 1].first - proj[i].first;
        if (d > gap) {
            gap = d;
            gap_i = i;
        }
    }
    std::array<std::vector<P2>, 2> out;
    for (std::size_t i = 0; i < n; i++) {
        out[i <= gap_i ? 0 : 1].push_back(proj[i].second);
    }
    return out;
}

std::vector<P2> Hull(const std::vector<P2> &pts0) {
    std::vector<P2> pts = pts0;
    std::ranges::sort(pts);
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) {
        return pts;
    }
    auto half = [](auto first, auto last) {
        std::vector<P2> h;
        for (auto it = first; it != last; ++it) {
            const P2 &p = *it;
            while (h.size() >= 2) {
                const P2 &a = h[h.size() - 2], &b = h[h.size() - 1];
                if ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) <= 0) {
                    h.pop_back();
                } else {
                    break;
                }
            }
            h.push_back(p);
        }
        return h;
    };
    std::vector<P2> lo = half(pts.begin(), pts.end());
    std::vector<P2> hi = half(pts.rbegin(), pts.rend());
    std::vector<P2> out(lo.begin(), lo.end() - 1);
    out.insert(out.end(), hi.begin(), hi.end() - 1);
    return out;
}

std::vector<P2> Simplify(const std::vector<P2> &hull, int n) {
    if (static_cast<int>(hull.size()) <= n) {
        return hull;
    }
    double cx = 0, cz = 0;
    for (const P2 &p : hull) {
        cx += p[0];
    }
    cx /= static_cast<double>(hull.size());
    for (const P2 &p : hull) {
        cz += p[1];
    }
    cz /= static_cast<double>(hull.size());
    std::map<int, std::pair<double, P2>> bins;
    for (const P2 &p : hull) {
        const int    b = Mod(static_cast<int>((std::atan2(p[1] - cz, p[0] - cx) + std::numbers::pi) / (2 * std::numbers::pi) * n), n);
        const double r = std::hypot(p[0] - cx, p[1] - cz);
        if (auto it = bins.find(b); it == bins.end() || r > it->second.first) {
            bins[b] = {r, p};
        }
    }
    std::vector<P2> out;
    for (const auto &[b, v] : bins) {
        out.push_back(v.second);
    }
    return out;
}

std::vector<P2> Chaikin(const std::vector<P2> &poly) {
    std::vector<P2>   out;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; i++) {
        const P2 &a = poly[i], &b = poly[(i + 1) % n];
        out.push_back({0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1]});
        out.push_back({0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1]});
    }
    return out;
}

std::vector<Tri> Walls(const std::vector<std::vector<P2>> &feet) {
    std::vector<Tri> tris;
    for (const auto &foot : feet) {
        const std::size_t n = foot.size();
        double            area2 = 0;
        for (std::size_t i = 0; i < n; i++) {
            area2 += foot[i][0] * foot[(i + 1) % n][1] - foot[(i + 1) % n][0] * foot[i][1];
        }
        std::vector<P2> loop = foot;
        if (!(area2 < 0)) {
            std::ranges::reverse(loop);
        }
        for (std::size_t i = 0; i < n; i++) {
            const P2 &a = loop[i], &b = loop[(i + 1) % n];
            tris.push_back({
                Vec{a[0], kHullYLo, a[1]},
                Vec{b[0], kHullYLo, b[1]},
                Vec{b[0], kHullYHi, b[1]}
            });
            tris.push_back({
                Vec{a[0], kHullYLo, a[1]},
                Vec{b[0], kHullYHi, b[1]},
                Vec{a[0], kHullYHi, a[1]}
            });
        }
    }
    return tris;
}

// The torii pillars' camera hulls: each pair's legs (the "extru" meshes of s1303 and s1304, first
// placement, between heights 20 and 45), hulled, cut to 8 corners, padded 8 out and rounded.
std::vector<std::pair<std::string, std::vector<Tri>>> PillarHulls(const std::vector<PlacedMesh> &placed) {
    constexpr std::array<std::tuple<const char *, const char *, int>, 2> kTargets = {
        {{"pcam_xs", "s1303", 0}, {"pcam_xn", "s1304", 0}}
    };
    std::vector<std::pair<std::string, std::vector<Tri>>> out;
    for (const auto &[label, sub, inst] : kTargets) {
        std::vector<P2> verts;
        for (const PlacedMesh &pm : placed) {
            if (pm.sub == sub && pm.inst == inst && pm.name.starts_with("extru")) {
                for (const Vec &v : pm.verts) {
                    if (kBandLo <= v[1] && v[1] <= kBandHi) {
                        verts.push_back({Round2(v[0]), Round2(v[2])});
                    }
                }
            }
        }
        std::vector<std::vector<P2>> feet;
        for (const auto &leg : Cluster2(verts)) {
            const std::vector<P2> hull = Simplify(Hull(leg), kHullN);
            if (hull.empty()) {
                throw Mismatch(std::format("{}: empty pillar hull", sub));
            }
            double cx = 0, cz = 0;
            for (const P2 &p : hull) {
                cx += p[0];
            }
            cx /= static_cast<double>(hull.size());
            for (const P2 &p : hull) {
                cz += p[1];
            }
            cz /= static_cast<double>(hull.size());
            std::vector<P2> padded;
            for (const P2 &p : hull) {
                double d = std::hypot(p[0] - cx, p[1] - cz);
                if (d == 0) {
                    d = 1.0;
                }
                padded.push_back({p[0] + (p[0] - cx) / d * kPillarPad, p[1] + (p[1] - cz) / d * kPillarPad});
            }
            feet.push_back(Chaikin(padded));
        }
        out.emplace_back(label, Walls(feet));
    }
    return out;
}

// The factory dome's (sphere27) radius in each 10-degree sector, widened to its neighbours.
std::array<double, 36> DoumuSectors(const std::vector<PlacedMesh> &placed) {
    std::array<double, 36> sec{};
    bool                   any = false;
    for (const PlacedMesh &pm : placed) {
        if (pm.name != "sphere27") {
            continue;
        }
        any = true;
        for (const Vec &v : pm.verts) {
            if (!(20.0 <= v[1] && v[1] <= 140.0)) {
                continue;
            }
            const double r = std::hypot(v[0] - kDoumuC[0], v[2] - kDoumuC[1]);
            const int    i = Mod(static_cast<int>(std::floor(std::atan2(v[2] - kDoumuC[1], v[0] - kDoumuC[0]) * (180.0 / std::numbers::pi) / 10)), 36);
            for (int j : {i - 1, i, i + 1}) {
                sec[static_cast<std::size_t>(Mod(j, 36))] = std::max(sec[static_cast<std::size_t>(Mod(j, 36))], r);
            }
        }
    }
    if (!any) {
        throw Mismatch("sphere27 (the factory dome) not found");
    }
    return sec;
}

P2 DoumuHugXz(const std::array<double, 36> &sec, double x, double z) {
    const double dx = x - kDoumuC[0], dz = z - kDoumuC[1], r = std::hypot(dx, dz);
    if (r <= 1.0) {
        return {x, z};
    }
    const double floor_r =
        sec[static_cast<std::size_t>(Mod(static_cast<int>(std::floor(std::atan2(dz, dx) * (180.0 / std::numbers::pi) / 10)), 36))] + kDoumuClear;
    const double r2 = std::max(r - kDoumuPull, std::min(floor_r, r));
    return {kDoumuC[0] + dx * r2 / r, kDoumuC[1] + dz * r2 / r};
}

// ── The rebuilt s1301 ──────────────────────────────────────────────────────────────────────────

Bytes RebuildS1301(const Bytes &scn, const EDIT_MAP_INFO *info) {
    const std::vector<DirEntry> dir = Directory(scn);
    const DirEntry             &entry = Sub(dir, "s1301");
    Need(scn, entry.off, entry.size, "s1301");
    const Bytes       sub(scn.begin() + static_cast<std::ptrdiff_t>(entry.off), scn.begin() + static_cast<std::ptrdiff_t>(entry.off + entry.size));
    const std::size_t ssize = sub.size();

    // Expanded's fixed block offsets are the USA sub's; here they come from the part's own record
    // (lod0, collision and camera), which has to lay them out back to back as the USA one does.
    const std::size_t vis_off = U32(sub, 0x48), vis_size = U32(sub, 0x58);
    const std::size_t a_off = U32(sub, 0x78), a_size = U32(sub, 0x7c);
    const std::size_t c_off = U32(sub, 0xc0), c_size = U32(sub, 0xc4);
    if (vis_off + vis_size != a_off || a_off + a_size != c_off || c_off + c_size != ssize) {
        throw Mismatch(std::format("s1301's blocks aren't laid out as expected ({:#x}+{:#x}, {:#x}+{:#x}, {:#x}+{:#x} of {:#x})", vis_off,
                                   vis_size, a_off, a_size, c_off, c_size, ssize));
    }
    for (std::size_t o : {0x4c, 0x50, 0x54}) {
        if (U32(sub, o) != a_off) {
            throw Mismatch(std::format("s1301's record word {:#x} isn't the collision block", o));
        }
    }
    for (std::size_t o : {0x90, 0xa8}) {
        if (U32(sub, o) != c_off) {
            throw Mismatch(std::format("s1301's record word {:#x} isn't the camera block", o));
        }
    }
    for (auto [off, size, tag] : {
             std::tuple{vis_off, vis_size, "visual"},
             std::tuple{a_off,   a_size,   "_a"    },
             std::tuple{c_off,   c_size,   "_c"    }
    }) {
        if (!IsTag(sub, off, 'M', 'D', 'S')) {
            throw Mismatch(std::format("s1301 {} block isn't an MDS", tag));
        }
        const Bytes relay = RelayMds(sub, off, size, {});
        if (relay.size() != size || !std::equal(relay.begin(), relay.end(), sub.begin() + static_cast<std::ptrdiff_t>(off))) {
            throw Mismatch(std::format("s1301 {} block doesn't relay byte for byte", tag));
        }
    }
    if (ssize != kUsaS1301Size) {
        Note(std::format("s13: s1301 is {:#x} bytes (the USA one is {:#x}); its layout checks out, so going on", ssize, kUsaS1301Size));
    }

    // The visual meshes grid10 and grid11.
    std::map<std::string, Bytes> edits_vis;
    {
        const std::size_t cnt = U32(sub, vis_off + 8), tbl = U32(sub, vis_off + 12);
        for (std::size_t i = 0; i < cnt; i++) {
            const std::size_t b = vis_off + tbl + i * 0x70;
            const std::string name = NameAt(sub, b + 8, 16);
            if (name != "grid10" && name != "grid11") {
                continue;
            }
            const auto [fresh, moved] = EditVisualMdt(sub, vis_off + static_cast<std::size_t>(I32(sub, b + 0x28)), ChainsFor(name));
            Note(std::format("s13: {} rebuilt ({} bytes, {} column verts shifted)", name, fresh.size(), moved));
            edits_vis[name] = fresh;
        }
    }
    if (edits_vis.size() != 2) {
        throw Mismatch("grid10 and grid11 not both found");
    }

    // The collision walls: the nodes holding the station columns.
    auto coll_edit = [&](std::size_t mds_off, const std::vector<Chain> &chains, std::string_view tag) {
        std::map<std::string, Bytes> edits;
        const std::size_t            cnt = U32(sub, mds_off + 8), tbl = U32(sub, mds_off + 12);
        std::vector<Vec>             stations;
        for (const Chain &c : chains) {
            stations.insert(stations.end(), c.begin(), c.end());
        }
        for (std::size_t i = 0; i < cnt; i++) {
            const std::size_t  b = mds_off + tbl + i * 0x70;
            const std::string  name = NameAt(sub, b + 8, 16);
            const std::int32_t mo = I32(sub, b + 0x28);
            if (mo == 0) {
                continue;
            }
            const std::size_t fo = mds_off + static_cast<std::size_t>(mo);
            if (!IsTag(sub, fo, 'M', 'D', 'T')) {
                continue;
            }
            const std::size_t pos_off = U32(sub, fo + 0x10), dl = U32(sub, fo + 0x28);
            const std::size_t tc = U32(sub, fo + dl + 0x14);
            int               nv = 0;
            for (std::size_t t = 0; t < tc; t++) {
                for (std::size_t k = 0; k < 3; k++) {
                    nv = std::max(nv, I32(sub, fo + dl + 0x18 + t * 0x14 + k * 4) + 1);
                }
            }
            int hits = 0;
            for (int vi = 0; vi < nv; vi++) {
                const std::size_t at = fo + pos_off + static_cast<std::size_t>(vi) * 0x10;
                const Vec         p = {F32(sub, at), F32(sub, at + 4), F32(sub, at + 8)};
                if (std::ranges::any_of(stations, [&](const Vec &s) { return ColMatch(p, s); })) {
                    hits++;
                }
            }
            if (hits == 0) {
                continue;
            }
            const auto [fresh, moved] = EditCollMdt(sub, fo, chains);
            Note(std::format("s13: {}/{} rebuilt ({} station verts found, {} shifted)", tag, name, hits, moved));
            edits[name] = fresh;
        }
        return edits;
    };
    std::map<std::string, Bytes> edits_a = coll_edit(a_off, ChainsFor("_a"), "s1301_a");
    std::map<std::string, Bytes> edits_c = coll_edit(c_off, ChainsFor("_c"), "s1301_c");
    if (edits_a.empty() || edits_c.empty()) {
        throw Mismatch("the collision wall nodes weren't found");
    }

    // The doumu_c hug: its frame carries a scale, so local to world, hug, then back through the
    // inverted 3x3.
    const std::vector<PlacedMesh> placed = PlacedMeshes(scn, info);
    const auto                    sectors = DoumuSectors(placed);
    {
        const std::size_t         cnt = U32(sub, c_off + 8), tbl = U32(sub, c_off + 12);
        std::vector<Mat>          local(cnt);
        std::vector<std::int32_t> parent(cnt);
        for (std::size_t i = 0; i < cnt; i++) {
            const std::size_t b = c_off + tbl + i * 0x70;
            for (std::size_t k = 0; k < 16; k++) {
                local[i][k] = F32(sub, b + 0x30 + k * 4);
            }
            parent[i] = I32(sub, b + 0x2c);
        }
        auto world_of = [&](std::size_t i) {
            Mat m = local[i];
            for (int depth = 0, p = parent[i]; p >= 0 && static_cast<std::size_t>(p) < cnt; p = parent[static_cast<std::size_t>(p)]) {
                if (++depth > 64) {
                    throw Mismatch("s1301_c node parents loop");
                }
                m = Compose(local[static_cast<std::size_t>(p)], m);
            }
            return m;
        };
        bool found = false;
        for (std::size_t i = 0; i < cnt; i++) {
            const std::size_t  b = c_off + tbl + i * 0x70;
            const std::int32_t mo = I32(sub, b + 0x28);
            if (NameAt(sub, b + 8, 16) != "doumu_c" || mo == 0) {
                continue;
            }
            found = true;
            const Mat    M = world_of(i);
            const double a = M[0], b2 = M[4], c2 = M[8], d2 = M[1], e2 = M[5], f2 = M[9], g2 = M[2], h2 = M[6], i2 = M[10];
            const double det = a * (e2 * i2 - f2 * h2) - b2 * (d2 * i2 - f2 * g2) + c2 * (d2 * h2 - e2 * g2);
            if (det == 0) {
                throw Mismatch("doumu_c's frame can't be inverted");
            }
            const double      inv[9] = {(e2 * i2 - f2 * h2) / det, (c2 * h2 - b2 * i2) / det, (b2 * f2 - c2 * e2) / det,
                                        (f2 * g2 - d2 * i2) / det, (a * i2 - c2 * g2) / det, (c2 * d2 - a * f2) / det,
                                        (d2 * h2 - e2 * g2) / det, (b2 * g2 - a * h2) / det, (a * e2 - b2 * d2) / det};
            const std::size_t fo = c_off + static_cast<std::size_t>(mo);
            const std::size_t w2 = U32(sub, fo + 8), w3 = U32(sub, fo + 12), w4 = U32(sub, fo + 16);
            Need(sub, fo, w2, "doumu_c");
            Bytes blk(sub.begin() + static_cast<std::ptrdiff_t>(fo), sub.begin() + static_cast<std::ptrdiff_t>(fo + w2));
            int   moved = 0;
            for (std::size_t vi = 0; vi < w3; vi++) {
                const std::size_t o = w4 + vi * 0x10;
                const double      vx = F32(blk, o), vy = F32(blk, o + 4), vz = F32(blk, o + 8);
                const float       vw = F32(blk, o + 12);
                const Vec         w = Xform(M, {vx, vy, vz});
                const P2          n = DoumuHugXz(sectors, w[0], w[2]);
                if (std::abs(n[0] - w[0]) + std::abs(n[1] - w[2]) > 1e-6) {
                    const double rx = n[0] - M[12], ry = w[1] - M[13], rz = n[1] - M[14];
                    const double lx = inv[0] * rx + inv[1] * ry + inv[2] * rz, lz = inv[6] * rx + inv[7] * ry + inv[8] * rz;
                    PutF32(blk, o, static_cast<float>(lx));
                    PutF32(blk, o + 4, static_cast<float>(vy));
                    PutF32(blk, o + 8, static_cast<float>(lz));
                    PutF32(blk, o + 12, vw);
                    moved++;
                }
            }
            edits_c["doumu_c"] = std::move(blk);
            Note(std::format("s13: doumu_c hugged ({} of {} verts pulled in)", moved, w3));
        }
        if (!found) {
            throw Mismatch("doumu_c not in s1301_c");
        }
    }

    // The pillar camera hulls, as new _c nodes.
    std::vector<std::pair<std::string, Bytes>> add_c;
    for (const auto &[label, tris] : PillarHulls(placed)) {
        add_c.emplace_back(label, BuildCollisionMdt(tris, kYLocal));
    }

    const Bytes vis = RelayMds(sub, vis_off, vis_size, edits_vis);
    const Bytes amds = RelayMds(sub, a_off, a_size, edits_a);
    const Bytes cmds = RelayMds(sub, c_off, c_size, edits_c, add_c);
    Bytes       out(sub.begin(), sub.begin() + static_cast<std::ptrdiff_t>(vis_off));
    out.insert(out.end(), vis.begin(), vis.end());
    out.insert(out.end(), amds.begin(), amds.end());
    out.insert(out.end(), cmds.begin(), cmds.end());
    const std::size_t new_a = vis_off + vis.size(), new_c = new_a + amds.size();
    for (std::size_t o : {0x4c, 0x50, 0x54, 0x78}) {
        PutU32(out, o, static_cast<std::uint32_t>(new_a));
    }
    PutU32(out, 0x58, static_cast<std::uint32_t>(vis.size()));
    PutU32(out, 0x7c, static_cast<std::uint32_t>(amds.size()));
    for (std::size_t o : {0x90, 0xa8, 0xc0}) {
        PutU32(out, o, static_cast<std::uint32_t>(new_c));
    }
    PutU32(out, 0xc4, static_cast<std::uint32_t>(cmds.size()));
    Note(std::format("s13: {} pillar camera hulls; s1301 {} -> {} bytes", add_c.size(), ssize, out.size()));
    return out;
}

// The scene with a whole sub-file swapped in place; the sub-files after it and the directory
// follow. (Expanded appends the new one instead; in place keeps the room after the scene.)
Bytes ReplaceSubInPlace(const Bytes &scn, std::string_view name, const Bytes &fresh) {
    const std::vector<DirEntry> dir = Directory(scn);
    const DirEntry             &sub = Sub(dir, name);
    Bytes                       padded = fresh;
    Pad16(padded);
    std::size_t next = scn.size();
    for (const DirEntry &e : dir) {
        if (e.off > sub.off) {
            next = std::min(next, e.off);
        }
    }
    if (next < sub.off + sub.size) {
        throw Mismatch(std::format("{} overlaps the next sub-file", name));
    }
    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(padded.size()) - static_cast<std::ptrdiff_t>(next - sub.off);
    Bytes                out(scn.begin(), scn.begin() + static_cast<std::ptrdiff_t>(sub.off));
    out.insert(out.end(), padded.begin(), padded.end());
    out.insert(out.end(), scn.begin() + static_cast<std::ptrdiff_t>(next), scn.end());
    for (const DirEntry &e : dir) {
        if (e.off == sub.off) {
            PutU32(out, e.entry_off + 0x14, static_cast<std::uint32_t>(fresh.size()));
        } else if (e.off > sub.off) {
            PutU32(out, e.entry_off + 0x10, static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(e.off) + delta));
        }
    }
    return out;
}

// ── Queens: geometry helpers (CollisionGeom) ───────────────────────────────────────────────────

using Tris = std::vector<Tri>;
using map_data::Table;

Vec V(double x, double y, double z) {
    return {x, y, z};
}

Tri MakeTri(const Vec &a, const Vec &b, const Vec &c) {
    return {a, b, c};
}

Tris TrisFromTable(const Table &t) {
    Tris out;
    for (std::size_t r = 0; r < t.rows; r++) {
        out.push_back({V(t.At(r, 0), t.At(r, 1), t.At(r, 2)), V(t.At(r, 3), t.At(r, 4), t.At(r, 5)), V(t.At(r, 6), t.At(r, 7), t.At(r, 8))});
    }
    return out;
}

void Append(Tris &to, const Tris &from) {
    to.insert(to.end(), from.begin(), from.end());
}

bool All(const Tri &t, auto &&pred) {
    return pred(t[0]) && pred(t[1]) && pred(t[2]);
}

double MinOf(const Tri &t, int a) {
    return std::min({t[0][a], t[1][a], t[2][a]});
}

double MaxOf(const Tri &t, int a) {
    return std::max({t[0][a], t[1][a], t[2][a]});
}

bool Box(const Tri &t, double x0, double x1, double y0, double y1, double z0, double z1, double e = 0.5) {
    return All(t, [&](const Vec &p) { return x0 - e <= p[0] && p[0] <= x1 + e && y0 - e <= p[1] && p[1] <= y1 + e && z0 - e <= p[2] && p[2] <= z1 + e; });
}

bool PlaneX(const Tri &t, double xv, double z0, double z1, double y0, double y1, double e = 0.5) {
    return All(t, [&](const Vec &p) { return std::abs(p[0] - xv) < e && z0 - e <= p[2] && p[2] <= z1 + e && y0 - e <= p[1] && p[1] <= y1 + e; });
}

bool PlaneZ(const Tri &t, double zv, double x0, double x1, double y0, double y1, double e = 0.5) {
    return All(t, [&](const Vec &p) { return std::abs(p[2] - zv) < e && x0 - e <= p[0] && p[0] <= x1 + e && y0 - e <= p[1] && p[1] <= y1 + e; });
}

Vec Normal(const Tri &t) {
    const Vec u = {t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]};
    const Vec v = {t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]};
    return {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
}

double Dot3(const Vec &a, const Vec &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec Cross3(const Vec &a, const Vec &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec Unit3(const Vec &a) {
    double l = std::sqrt(Dot3(a, a));
    if (l == 0) {
        l = 1.0;
    }
    return {a[0] / l, a[1] / l, a[2] / l};
}

bool Horiz(const Tri &t) {
    const Vec n = Normal(t);
    double    h = std::sqrt(Dot3(n, n));
    if (h == 0) {
        h = 1.0;
    }
    return std::abs(n[1]) > 0.7 * h;
}

using Key3 = std::array<double, 3>;
using TriKeyD = std::array<Key3, 3>;

Key3 Round3(const Vec &p, int nd) {
    return {ExactRound(p[0], nd), ExactRound(p[1], nd), ExactRound(p[2], nd)};
}

// TriKeyRounded(t, 0): the three vertices to whole units, sorted, so winding doesn't count.
TriKeyD KeyInt(const Tri &t) {
    TriKeyD k = {Round3(t[0], 0), Round3(t[1], 0), Round3(t[2], 0)};
    std::ranges::sort(k);
    return k;
}

// TriKeyWinding: the smallest cyclic rotation of the rounded vertices, so winding counts.
TriKeyD KeyWinding(const Tri &t) {
    const TriKeyD v = {Round3(t[0], 0), Round3(t[1], 0), Round3(t[2], 0)};
    TriKeyD       best = v;
    for (int r = 1; r < 3; r++) {
        const TriKeyD rot = {v[static_cast<std::size_t>(r)], v[static_cast<std::size_t>((r + 1) % 3)], v[static_cast<std::size_t>((r + 2) % 3)]};
        if (rot < best) {
            best = rot;
        }
    }
    return best;
}

std::set<TriKeyD> KeySet(const Table &table, TriKeyD (*key)(const Tri &)) {
    std::set<TriKeyD> out;
    for (const Tri &t : TrisFromTable(table)) {
        out.insert(key(t));
    }
    return out;
}

Tris DirQuad(const Vec &a, const Vec &b, const Vec &c, const Vec &d, const Vec &want) {
    if (Dot3(Normal(MakeTri(a, b, c)), want) >= 0) {
        return {MakeTri(a, b, c), MakeTri(a, c, d)};
    }
    return {MakeTri(a, c, b), MakeTri(a, d, c)};
}

Tris Quad(const Vec &a, const Vec &b, const Vec &c, const Vec &d) {
    return {MakeTri(a, b, c), MakeTri(a, c, d)};
}

bool PtInTri2(double px, double py, const std::array<std::pair<double, double>, 3> &tt) {
    const auto [x0, y0] = tt[0];
    const auto [x1, y1] = tt[1];
    const auto [x2, y2] = tt[2];
    const double d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
    const double d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
    const double d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0);
    return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
}

// The true cells of a grid covered with maximal rectangles (right, then down), never across a hole.
std::vector<std::array<std::size_t, 4>> GreedyRects(const std::vector<std::vector<bool>> &cov) {
    const std::size_t                       nx = cov.size(), ny = cov[0].size();
    std::vector<std::vector<bool>>          used(nx, std::vector<bool>(ny, false));
    std::vector<std::array<std::size_t, 4>> out;
    for (std::size_t i = 0; i < nx; i++) {
        for (std::size_t j = 0; j < ny; j++) {
            if (!cov[i][j] || used[i][j]) {
                continue;
            }
            std::size_t i1 = i;
            while (i1 + 1 < nx && cov[i1 + 1][j] && !used[i1 + 1][j]) {
                i1++;
            }
            std::size_t j1 = j;
            while (j1 + 1 < ny) {
                bool ok = true;
                for (std::size_t ii = i; ii <= i1; ii++) {
                    if (!(cov[ii][j1 + 1] && !used[ii][j1 + 1])) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) {
                    break;
                }
                j1++;
            }
            for (std::size_t ii = i; ii <= i1; ii++) {
                for (std::size_t jj = j; jj <= j1; jj++) {
                    used[ii][jj] = true;
                }
            }
            out.push_back({i, j, i1, j1});
        }
    }
    return out;
}

Tris PlaneQuad(const Vec &u, const Vec &v, const Vec &nn, double d, double a0, double b0, double a1, double b1) {
    auto P = [&](double a, double b) {
        return V(a * u[0] + b * v[0] + d * nn[0], a * u[1] + b * v[1] + d * nn[1], a * u[2] + b * v[2] + d * nn[2]);
    };
    const Vec qa = P(a0, b0), qb = P(a1, b0), qc = P(a1, b1), qd = P(a0, b1);
    if (Dot3(Normal(MakeTri(qa, qb, qc)), nn) < 0) {
        return {MakeTri(qa, qc, qb), MakeTri(qa, qd, qc)};
    }
    return {MakeTri(qa, qb, qc), MakeTri(qa, qc, qd)};
}

// SimplifyCoplanar: coplanar triangles (any orientation) merged into the fewest rectangles, holes
// kept; 2D coordinates snap to `snap`; vertical faces extended up to the group's top (from each
// column's lowest wall cell, or with keep_windows its highest), optionally to `top`; the merged
// plane set `outward` behind the outermost source point.
Tris SimplifyCoplanar(const Tris &tris, double snap = 5.0, double outward = 0.0, std::optional<double> top = std::nullopt, bool keep_windows = false) {
    auto sn = [&](double x) { return std::nearbyint(x / snap) * snap; };
    using GroupKey = std::array<double, 4>;
    std::vector<GroupKey> keys;

    struct Group {
        Vec  nn;
        Tris g;
    };

    std::map<GroupKey, Group> groups;
    Tris                      out;
    for (const Tri &t : tris) {
        const Vec n = Normal(t);
        if (std::sqrt(Dot3(n, n)) < 1e-9) {
            out.push_back(t);
            continue;
        }
        const Vec      nn = Unit3(n);
        const double   d = Dot3(nn, t[0]);
        const GroupKey key = {ExactRound(nn[0], 1), ExactRound(nn[1], 1), ExactRound(nn[2], 1), std::nearbyint(d / snap) * snap + 0.0};
        auto [it, inserted] = groups.try_emplace(key, Group{nn, {}});
        if (inserted) {
            keys.push_back(key);
        }
        it->second.g.push_back(t);
    }
    for (const GroupKey &key : keys) {
        const Vec   nn = groups[key].nn;
        const Tris &g = groups[key].g;
        if (g.size() < 2) {
            Append(out, g);
            continue;
        }
        double d = std::numeric_limits<double>::infinity();
        for (const Tri &t : g) {
            for (const Vec &p : t) {
                d = std::min(d, Dot3(nn, p));
            }
        }
        d -= outward;
        const double dup = nn[1];
        Vec          vp = {-dup * nn[0], -dup * nn[1], -dup * nn[2]};
        vp[1] += 1.0;
        Vec u, v;
        if (Dot3(vp, vp) < 1e-6) {
            u = Unit3({1.0 - nn[0] * nn[0], -nn[0] * nn[1], -nn[0] * nn[2]});
            v = Cross3(nn, u);
        } else {
            v = Unit3(vp);
            u = Unit3(Cross3(v, nn));
        }
        std::vector<std::array<std::pair<double, double>, 3>> tri2d;
        std::vector<double>                                   us, vs;
        for (const Tri &t : g) {
            std::array<std::pair<double, double>, 3> tt;
            for (std::size_t k = 0; k < 3; k++) {
                tt[k] = {sn(Dot3(t[k], u)), sn(Dot3(t[k], v))};
                us.push_back(tt[k].first);
                vs.push_back(tt[k].second);
            }
            tri2d.push_back(tt);
        }
        std::ranges::sort(us);
        us.erase(std::unique(us.begin(), us.end()), us.end());
        std::ranges::sort(vs);
        vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
        if (us.size() < 2 || vs.size() < 2) {
            Append(out, g);
            continue;
        }
        std::vector<std::vector<bool>> cov(us.size() - 1, std::vector<bool>(vs.size() - 1, false));
        for (std::size_t i = 0; i + 1 < us.size(); i++) {
            for (std::size_t j = 0; j + 1 < vs.size(); j++) {
                const double cu = (us[i] + us[i + 1]) / 2, cv = (vs[j] + vs[j + 1]) / 2;
                cov[i][j] = std::ranges::any_of(tri2d, [&](const auto &tt) { return PtInTri2(cu, cv, tt); });
            }
        }
        if (v[1] > 0.9) {
            for (auto &col : cov) {
                std::optional<std::size_t> lo, hi;
                for (std::size_t j = 0; j < col.size(); j++) {
                    if (col[j]) {
                        if (!lo) {
                            lo = j;
                        }
                        hi = j;
                    }
                }
                if (!lo) {
                    continue;
                }
                for (std::size_t j = keep_windows ? *hi : *lo; j < col.size(); j++) {
                    col[j] = true;
                }
            }
        }
        Tris fresh;
        for (const auto &[i0, j0, i1, j1] : GreedyRects(cov)) {
            double b1 = vs[j1 + 1];
            if (top && v[1] > 0.9 && std::abs(b1 - vs.back()) < 1e-6) {
                b1 = *top;
            }
            Append(fresh, PlaneQuad(u, v, nn, d, us[i0], vs[j0], us[i1 + 1], b1));
        }
        Append(out, fresh.size() < g.size() ? fresh : g);
    }
    return out;
}

using TriPred = std::function<bool(const Tri &)>;

// PlaneRegion: triangles on the plane coord[axis] = off (every corner within 2) whose centre lies
// in the ranges given.
TriPred PlaneRegion(int axis, double off, std::optional<std::pair<double, double>> x, std::optional<std::pair<double, double>> y,
                    std::optional<std::pair<double, double>> z = std::nullopt) {
    return [=](const Tri &t) {
        if (!All(t, [&](const Vec &p) { return std::abs(p[static_cast<std::size_t>(axis)] - off) < 2; })) {
            return false;
        }
        const Vec                                                     c = Centroid(t);
        const std::array<std::optional<std::pair<double, double>>, 3> rngs = {x, y, z};
        for (std::size_t i = 0; i < 3; i++) {
            if (rngs[i] && !(rngs[i]->first <= c[i] && c[i] <= rngs[i]->second)) {
                return false;
            }
        }
        return true;
    };
}

// CollisionMdt.KdSplit, median form: halve along the longest centroid axis until each leaf holds
// at most `max_tris`.
void KdSplitMedian(Tris ts, std::size_t max_tris, const std::vector<int> &axes, std::vector<Tris> &out) {
    if (ts.size() <= max_tris) {
        out.push_back(std::move(ts));
        return;
    }
    std::vector<Vec> cs;
    for (const Tri &t : ts) {
        cs.push_back(Centroid(t));
    }
    int    axis = axes[0];
    double best = -std::numeric_limits<double>::infinity();
    for (int a : axes) {
        auto [lo, hi] = std::ranges::minmax(cs, {}, [a](const Vec &c) { return c[static_cast<std::size_t>(a)]; });
        const double ext = hi[static_cast<std::size_t>(a)] - lo[static_cast<std::size_t>(a)];
        if (ext > best) {
            best = ext;
            axis = a;
        }
    }
    std::vector<std::size_t> order(ts.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return cs[i][static_cast<std::size_t>(axis)]; });
    const std::size_t mid = ts.size() / 2;
    Tris              left, right;
    for (std::size_t i = 0; i < order.size(); i++) {
        (i < mid ? left : right).push_back(ts[order[i]]);
    }
    KdSplitMedian(std::move(left), max_tris, axes, out);
    KdSplitMedian(std::move(right), max_tris, axes, out);
}

std::string FitNodeName(const std::string &name, std::set<std::string> &used, std::size_t maxlen = 15) {
    std::string cand = name.size() > maxlen ? name.substr(0, maxlen) : name;
    for (int k = 1; used.contains(cand); k++) {
        const std::string suf = std::format("~{}", k);
        cand = name.substr(0, std::min(name.size(), maxlen - suf.size())) + suf;
    }
    used.insert(cand);
    return cand;
}

std::vector<Named> PoolSplit(const Tris &pool, const std::string &prefix, std::set<std::string> &used, std::size_t max_tris = 100) {
    std::vector<Tris> leaves;
    KdSplitMedian(pool, max_tris, {0, 1, 2}, leaves);
    std::vector<Named> out;
    for (std::size_t i = 0; i < leaves.size(); i++) {
        out.push_back({FitNodeName(std::format("{}{}", prefix, i), used), std::move(leaves[i])});
    }
    return out;
}

// An MDS block's nodes with their parent-accumulated frames (SceneScn.Accum).
struct MdsNodes {
    std::vector<std::string>  names;
    std::vector<std::int32_t> mesh;
    std::vector<Mat>          world;
};

MdsNodes AccumNodes(const Bytes &b, std::size_t mds) {
    const std::size_t         cnt = U32(b, mds + 8), tbl = U32(b, mds + 12);
    MdsNodes                  out;
    std::vector<Mat>          local(cnt);
    std::vector<std::int32_t> parent(cnt);
    for (std::size_t i = 0; i < cnt; i++) {
        const std::size_t n = mds + tbl + i * 0x70;
        for (std::size_t k = 0; k < 16; k++) {
            local[i][k] = F32(b, n + 0x30 + k * 4);
        }
        out.names.push_back(NameAt(b, n + 8, 16));
        out.mesh.push_back(I32(b, n + 0x28));
        parent[i] = I32(b, n + 0x2c);
    }
    for (std::size_t i = 0; i < cnt; i++) {
        Mat m = local[i];
        int depth = 0;
        for (std::int32_t p = parent[i]; p >= 0 && static_cast<std::size_t>(p) < cnt; p = parent[static_cast<std::size_t>(p)]) {
            if (++depth > 64) {
                throw Mismatch("node parents loop");
            }
            m = Compose(local[static_cast<std::size_t>(p)], m);
        }
        out.world.push_back(m);
    }
    return out;
}

std::size_t ResolveMdt(const Bytes &b, std::size_t mds, std::int32_t mesh) {
    for (std::int64_t cand : {static_cast<std::int64_t>(mesh), static_cast<std::int64_t>(mds) + mesh}) {
        if (cand > 0 && static_cast<std::size_t>(cand) + 3 < b.size() && IsTag(b, static_cast<std::size_t>(cand), 'M', 'D', 'T')) {
            return static_cast<std::size_t>(cand);
        }
    }
    return kNone;
}

// ── Queens: the terrain rules (QueensTerrainRules) ─────────────────────────────────────────────

namespace md = map_data;

Tris WallTris(const Table &walls) {
    Tris out;
    for (std::size_t i = 0; i < walls.rows; i++) {
        const Vec    a0 = V(walls.At(i, 0), walls.At(i, 1), walls.At(i, 2)), b0 = V(walls.At(i, 3), walls.At(i, 4), walls.At(i, 5));
        const double h = walls.At(i, 6);
        const Vec    a1 = V(a0[0], a0[1] + h, a0[2]), b1 = V(b0[0], b0[1] + h, b0[2]);
        out.push_back(MakeTri(a0, b0, b1));
        out.push_back(MakeTri(a0, b1, a1));
    }
    return out;
}

Tris FlatGroundTris() {
    Tris         out;
    const Table &f = md::kQueensFlatRegions;
    for (std::size_t i = 0; i < f.rows; i++) {
        const double x0 = f.At(i, 0), x1 = f.At(i, 1), z0 = f.At(i, 2), z1 = f.At(i, 3), y = f.At(i, 4);
        out.push_back(MakeTri(V(x0, y, z0), V(x1, y, z0), V(x1, y, z1)));
        out.push_back(MakeTri(V(x0, y, z0), V(x1, y, z1), V(x0, y, z1)));
    }
    return out;
}

// Each canal pipe stub as its bounding box: four sides and the free end (the end in the wall open).
Tris PipeDrumTris() {
    Tris         out;
    const Table &p = md::kQueensPipeDrums;
    for (std::size_t i = 0; i < p.rows; i++) {
        const double cx = p.At(i, 0), cy = p.At(i, 1), z0 = p.At(i, 2), z1 = p.At(i, 3), r = p.At(i, 4);
        const double x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
        const bool   emb = std::abs(z0) > std::abs(z1);
        const double zf = emb ? z1 : z0;
        Append(out, DirQuad(V(x1, y0, z0), V(x1, y1, z0), V(x1, y1, z1), V(x1, y0, z1), V(1, 0, 0)));
        Append(out, DirQuad(V(x0, y0, z0), V(x0, y1, z0), V(x0, y1, z1), V(x0, y0, z1), V(-1, 0, 0)));
        Append(out, DirQuad(V(x0, y1, z0), V(x1, y1, z0), V(x1, y1, z1), V(x0, y1, z1), V(0, 1, 0)));
        Append(out, DirQuad(V(x0, y0, z0), V(x1, y0, z0), V(x1, y0, z1), V(x0, y0, z1), V(0, -1, 0)));
        Append(out, DirQuad(V(x0, y0, zf), V(x1, y0, zf), V(x1, y1, zf), V(x0, y1, zf), V(0, 0, emb ? 1 : -1)));
    }
    return out;
}

Tris BothWallTris() {
    Tris out = WallTris(md::kQueensBothWalls);
    Append(out, TrisFromTable(md::kQueensManualTris));
    Append(out, FlatGroundTris());
    Append(out, PipeDrumTris());
    return out;
}

// The canal containment, every wall's top pulled down to its ground there plus 5: the feet-level
// check still hits it, the higher camera clears it.
Tris InvisibleTris(double max_height = 5.0) {
    Tris                                        tris = TrisFromTable(md::kQueensCanalContainmentTris);
    auto                                        key = [](const Vec &p) { return std::pair{ExactRound(p[0], 1), ExactRound(p[2], 1)}; };
    std::map<std::pair<double, double>, double> ground;
    for (const Tri &t : tris) {
        for (const Vec &p : t) {
            auto [it, inserted] = ground.try_emplace(key(p), p[1]);
            if (!inserted) {
                it->second = std::min(it->second, p[1]);
            }
        }
    }
    for (Tri &t : tris) {
        for (Vec &p : t) {
            p[1] = std::min(p[1], ground[key(p)] + max_height);
        }
    }
    return tris;
}

Tris PerimeterWallTris(double y_bottom = 170.0, double y_top = 500.0) {
    const Table      &c = md::kQueensPerimeterCorners;
    const std::size_t n = c.rows;
    Tris              walls;
    for (std::size_t i = 0; i < n; i++) {
        const double ax = c.At(i, 0), az = c.At(i, 1), bx = c.At((i + 1) % n, 0), bz = c.At((i + 1) % n, 1);
        walls.push_back(MakeTri(V(ax, y_top, az), V(bx, y_top, bz), V(bx, y_bottom, bz)));
        walls.push_back(MakeTri(V(ax, y_top, az), V(bx, y_bottom, bz), V(ax, y_bottom, az)));
    }
    const std::set<TriKeyD> rem = KeySet(md::kQueensPerimeterPreRemoveTris, KeyInt);
    Tris                    out;
    for (const Tri &w : walls) {
        if (!rem.contains(KeyInt(w))) {
            out.push_back(w);
        }
    }
    Append(out, WallTris(md::kQueensPerimeterExtraWalls));
    return out;
}

bool InRemoveRegion(const Tri &t) {
    return PlaneZ(t, 1300, -400, 600, 0, 270) || PlaneX(t, 600, 200, 1300, 0, 272) || PlaneX(t, 600, 200, 1250, 262, 376) ||
           Box(t, -500, -400, 70, 170, -900, -200) || PlaneX(t, -400, -1000, -150, 70, 276) || PlaneX(t, 900, -1000, -150, 70, 276) ||
           PlaneZ(t, -1000, -400, 900, 70, 276) || (Box(t, -500, 1000, 260, 278, -1100, -1000) && Horiz(t)) ||
           (Box(t, -500, -350, 260, 278, -1000, -150) && Horiz(t)) || (Box(t, 850, 1000, 260, 278, -1000, -150) && Horiz(t)) ||
           PlaneX(t, 1500, 250, 1300, 170, 370) || PlaneZ(t, 1300, 650, 1500, 170, 370) || Box(t, 650, 1600, 370, 370, 100, 1400) ||
           Box(t, 1400, 1600, 64, 89, -50, 50) || PlaneX(t, 1400, -50, 50, 89, 170);
}

const std::set<TriKeyD> &ResidueKeys() {
    static const std::set<TriKeyD> keys = KeySet(md::kQueensRemoveResidueTris, KeyInt);
    return keys;
}

// The camera walls known to be wound backwards are turned round.
Tris FixCameraWinding(const Tris &tris) {
    static const std::set<TriKeyD> backface = KeySet(md::kQueensBackfaceTris, KeyWinding);
    Tris                           out;
    for (const Tri &t : tris) {
        out.push_back(backface.contains(KeyWinding(t)) ? MakeTri(t[0], t[2], t[1]) : t);
    }
    return out;
}

bool InFlatRegion(const Tri &t) {
    const Table &f = md::kQueensFlatRegions;
    for (std::size_t i = 0; i < f.rows; i++) {
        const double x0 = f.At(i, 0), x1 = f.At(i, 1), z0 = f.At(i, 2), z1 = f.At(i, 3), y = f.At(i, 4);
        if (All(t, [&](const Vec &p) { return x0 - 1 <= p[0] && p[0] <= x1 + 1 && z0 - 1 <= p[2] && p[2] <= z1 + 1 && std::abs(p[1] - y) <= 2; })) {
            return true;
        }
    }
    return false;
}

bool CanalWall(const Tri &t) {
    return (All(t, [](const Vec &p) { return std::abs(p[2] + 50) < 1; }) || All(t, [](const Vec &p) { return std::abs(p[2] - 50) < 1; })) &&
           All(t, [](const Vec &p) { return -1 <= p[1] && p[1] <= 71 && -201 <= p[0] && p[0] <= 901; });
}

// The shared terrain simplification: everything at or west of x -500 and wholly east of x 1600
// dropped, the removal regions and residue, the flat ground (replaced by quads) and the canal side
// walls between x -200 and 900.
Tris SimplifyTerrain(const Tris &tris) {
    Tris out;
    for (const Tri &t : tris) {
        if (MaxOf(t, 0) > -499.0 && MinOf(t, 0) < 1600.0 && !InRemoveRegion(t) && !ResidueKeys().contains(KeyInt(t)) && !InFlatRegion(t) &&
            !CanalWall(t)) {
            out.push_back(t);
        }
    }
    return out;
}

constexpr double kCanalWallTopY = 280.0;

// The major town walls: the vertical faces merged (holes kept, the ramparts forced to one height),
// the x -400 arcade's non-walkable parts filled solid, the three walkway tops, the z 200 staircase
// walls flattened, the x 1400 to 1500 sections capped at the obj33 ledge, the x 695 return moved
// flush to x 700, the west corner closed.
Tris TownWallTris() {
    const Tris all = TrisFromTable(md::kQueensTownWallTris);
    Tris       vert;
    for (const Tri &t : all) {
        const Vec n = Normal(t);
        double    l = std::sqrt(Dot3(n, n));
        if (l == 0) {
            l = 1.0;
        }
        if (std::abs(n[1]) / l < 0.5) {
            vert.push_back(t);
        }
    }
    constexpr std::array<std::array<double, 3>, 2> kX400Solid = {
        {{200.0, 500.0, 280.0}, {600.0, 1300.0, 270.0}}
    };
    auto in_solid = [&](const Tri &t) {
        if (!All(t, [](const Vec &p) { return std::abs(p[0] + 400) < 1; })) {
            return false;
        }
        const double cz = (t[0][2] + t[1][2] + t[2][2]) / 3;
        return std::ranges::any_of(kX400Solid, [&](const auto &s) { return s[0] - 5 <= cz && cz <= s[1] + 5; });
    };
    const std::array<std::pair<double, TriPred>, 2> forced = {
        {
         {280.0, [](const Tri &t) { return All(t, [](const Vec &p) { return std::abs(p[2] - 150) < 1; }) || All(t, [](const Vec &p) { return std::abs(p[0] - 695) < 1; }); }},
         {380.0, [](const Tri &t) { return All(t, [](const Vec &p) { return std::abs(p[2] - 250) < 1; }); }},
         }
    };
    Tris              out;
    std::vector<bool> done(vert.size(), false);
    for (const auto &[h, pred] : forced) {
        Tris grp;
        for (std::size_t i = 0; i < vert.size(); i++) {
            if (pred(vert[i]) && !in_solid(vert[i])) {
                grp.push_back(vert[i]);
                done[i] = true;
            }
        }
        Append(out, SimplifyCoplanar(grp, 10.0, 0.0, h, true));
    }
    Tris rest;
    for (std::size_t i = 0; i < vert.size(); i++) {
        if (!done[i] && !in_solid(vert[i])) {
            rest.push_back(vert[i]);
        }
    }
    Append(out, SimplifyCoplanar(rest, 10.0, 0.0, std::nullopt, true));
    for (const auto &[z0, z1, ty] : kX400Solid) {
        Append(out, DirQuad(V(-400, 0, z0), V(-400, 0, z1), V(-400, ty, z1), V(-400, ty, z0), V(1, 0, 0)));
    }
    Append(out, DirQuad(V(-400, 280, 150), V(700, 280, 150), V(700, 280, 200), V(-400, 280, 200), V(0, 1, 0)));
    Append(out, DirQuad(V(600, 380, 200), V(1350, 380, 200), V(1350, 380, 250), V(600, 380, 250), V(0, 1, 0)));
    Append(out, DirQuad(V(600, 380, 200), V(650, 380, 200), V(650, 380, 1300), V(600, 380, 1300), V(0, 1, 0)));
    auto drop = [&](auto &&pred) { std::erase_if(out, pred); };
    drop([](const Tri &t) {
        return All(t, [](const Vec &p) { return std::abs(p[2] - 200) < 1 && -1 <= p[0] && p[0] <= 601 && -1 <= p[1] && p[1] <= 281; }) && Normal(t)[2] > 0;
    });
    Append(out, DirQuad(V(0, 0, 200), V(600, 0, 200), V(600, 280, 200), V(0, 280, 200), V(0, 0, 1)));
    drop([](const Tri &t) {
        return All(t, [](const Vec &p) { return std::abs(p[2] - 200) < 1 && 599 <= p[0] && p[0] <= 1301 && 69 <= p[1] && p[1] <= 381; }) && Normal(t)[2] < 0;
    });
    Append(out, DirQuad(V(600, 70, 200), V(1300, 70, 200), V(1300, 380, 200), V(600, 380, 200), V(0, 0, -1)));
    auto x1400 = [](const Tri &t, double zplane, int nsign) {
        return All(t, [&](const Vec &p) { return 1399 <= p[0] && p[0] <= 1501 && 169 <= p[1] && p[1] <= 381 && std::abs(p[2] - zplane) < 1; }) &&
               Normal(t)[2] * nsign > 0;
    };
    drop([&](const Tri &t) { return x1400(t, 250, +1) || x1400(t, 200, -1); });
    Append(out, DirQuad(V(1400, 170, 250), V(1500, 170, 250), V(1500, 370, 250), V(1400, 370, 250), V(0, 0, 1)));
    Append(out, DirQuad(V(1400, 170, 200), V(1500, 170, 200), V(1500, 370, 200), V(1400, 370, 200), V(0, 0, -1)));
    Append(out, DirQuad(V(1400, 370, 200), V(1500, 370, 200), V(1500, 370, 250), V(1400, 370, 250), V(0, 1, 0)));
    drop([](const Tri &t) {
        return All(t, [](const Vec &p) { return std::abs(p[0] - 695) < 1 && 149 <= p[2] && p[2] <= 201 && 69 <= p[1] && p[1] <= 281; });
    });
    Append(out, DirQuad(V(700, 70, 150), V(700, 70, 200), V(700, 280, 200), V(700, 280, 150), V(1, 0, 0)));
    Append(out, DirQuad(V(-400, 70, 50), V(-400, 70, 150), V(-400, 270, 150), V(-400, 270, 50), V(1, 0, 0)));
    return out;
}

bool NorthFace(const Tri &t) {
    const double cx = (t[0][0] + t[1][0] + t[2][0]) / 3.0;
    return All(t, [](const Vec &p) { return std::abs(p[2] + 100) < 2; }) && MinOf(t, 1) >= 55 && MaxOf(t, 1) <= 290 && -450 <= cx && cx <= 1550;
}

bool WallCap(const Tri &t) {
    return MaxOf(t, 2) - MinOf(t, 2) > 40 && All(t, [](const Vec &p) { return -152 <= p[2] && p[2] <= -98; }) &&
           All(t, [](const Vec &p) { return -401 <= p[0] && p[0] <= 1501; }) && MinOf(t, 1) >= 258 && Normal(t)[1] > 0;
}

// CamMergeSelected: each directed job takes its triangles (a merge job merges them, a replace job
// swaps in authored quads); everything else goes through at full detail.
Tris CamMergeSelected(const Tris &tris) {
    static const std::set<TriKeyD> town = KeySet(md::kQueensTownWallTris, KeyInt);
    static const std::set<TriKeyD> arcade = KeySet(md::kQueensArcadeBackTris, KeyInt);
    static const std::set<TriKeyD> notch = KeySet(md::kQueensZ200NotchTris, KeyInt);

    struct Job {
        bool                  merge;
        TriPred               sel;
        std::function<Tris()> make;
    };

    const std::vector<Job> jobs = {
        {true, PlaneRegion(2, -150, std::pair{-400.0, 900.0}, std::pair{55.0, 290.0}), {}},
        {false, NorthFace,
         [] {
             Tris q = Quad(V(-400, 70, -100), V(200, 70, -100), V(200, kCanalWallTopY, -100), V(-400, kCanalWallTopY, -100));
             Append(q, Quad(V(300, 70, -100), V(1500, 70, -100), V(1500, kCanalWallTopY, -100), V(300, kCanalWallTopY, -100)));
             return q;
         }},
        {false, WallCap, [] { return Quad(V(-400, kCanalWallTopY, -100), V(1500, kCanalWallTopY, -100), V(1500, kCanalWallTopY, -150), V(-400, kCanalWallTopY, -150)); }},
        {false, [](const Tri &t) { return town.contains(KeyInt(t)); }, TownWallTris},
        {false, [](const Tri &t) { return arcade.contains(KeyInt(t)); }, [] { return Tris{}; }},
        {false, [](const Tri &t) { return notch.contains(KeyInt(t)); }, [] { return Tris{}; }},
    };
    Tris remaining = tris, out;
    for (const Job &job : jobs) {
        Tris picked, left;
        for (const Tri &t : remaining) {
            (job.sel(t) ? picked : left).push_back(t);
        }
        remaining = std::move(left);
        if (picked.empty()) {
            continue;
        }
        Append(out, job.merge ? SimplifyCoplanar(picked, 5.0, 0.0, kCanalWallTopY) : job.make());
    }
    Append(out, remaining);
    return out;
}

// GateKey: the corners, shifted by dx, to a tenth, sorted.
TriKeyD GateKey(const Tri &t, double dx = 0.0) {
    TriKeyD k;
    for (std::size_t i = 0; i < 3; i++) {
        k[i] = {ExactRound(t[i][0] + dx, 1), ExactRound(t[i][1], 1), ExactRound(t[i][2], 1)};
    }
    std::ranges::sort(k);
    return k;
}

// obj40 and obj44 (the same bridge-gate mesh): each corner torch post (y 77 and up) becomes a
// full-height corner column, the inner railings and old filler walls go, the walkway top and the
// outer face's top edge go up 8, the vaulted roof becomes 4 quads, and the z-end gaps close.
Tris GateTorchSimplify(const Tris &tris) {
    double xmin = std::numeric_limits<double>::infinity(), xmax = -xmin, zmin = xmin, zmax = -xmin;
    for (const Tri &t : tris) {
        for (const Vec &p : t) {
            xmin = std::min(xmin, p[0]);
            xmax = std::max(xmax, p[0]);
            zmin = std::min(zmin, p[2]);
            zmax = std::max(zmax, p[2]);
        }
    }
    const double      dx = ExactRound(xmin - md::kGateXMinRef, 0);
    std::set<TriKeyD> drop_keys, raise_keys, extend_keys, roof_keys;
    for (const Table *table : {&md::kGateRailingTris, &md::kGateOuterFillerTris}) {
        for (const Tri &t : TrisFromTable(*table)) {
            drop_keys.insert(GateKey(t, dx));
        }
    }
    for (const Tri &t : TrisFromTable(md::kGateWalkwayRaiseTris)) {
        raise_keys.insert(GateKey(t, dx));
    }
    for (const Tri &t : TrisFromTable(md::kGateOuterFaceExtendTris)) {
        extend_keys.insert(GateKey(t, dx));
    }
    constexpr double                   kY0 = 77.25, kXW = 7.0, kZW = 8.0, kDy = 8.0;
    std::vector<std::array<double, 4>> regions;
    for (int sx : {0, 1}) {
        for (int sz : {0, 1}) {
            const auto rx = sx == 0 ? std::pair{xmin - 0.5, xmin + kXW} : std::pair{xmax - kXW, xmax + 0.5};
            const auto rz = sz == 0 ? std::pair{zmin - 0.5, zmin + kZW} : std::pair{zmax - kZW, zmax + 0.5};
            regions.push_back({rx.first, rx.second, rz.first, rz.second});
        }
    }
    Tris              keep;
    std::vector<Tris> cap(regions.size());
    for (const Tri &t : tris) {
        const TriKeyD k = GateKey(t);
        if (drop_keys.contains(k)) {
            continue;
        }
        if (raise_keys.contains(k)) {
            Tri r = t;
            for (Vec &p : r) {
                p[1] += kDy;
            }
            keep.push_back(r);
            continue;
        }
        if (extend_keys.contains(k)) {
            Tri r = t;
            for (Vec &p : r) {
                p[1] += p[1] >= 65 ? kDy : 0.0;
            }
            keep.push_back(r);
            continue;
        }
        const Vec c = Centroid(t);
        int       r = -1;
        if (c[1] >= kY0) {
            for (std::size_t ri = 0; ri < regions.size(); ri++) {
                const auto &[x0, x1, z0, z1] = regions[ri];
                if (x0 <= c[0] && c[0] <= x1 && z0 <= c[2] && c[2] <= z1) {
                    r = static_cast<int>(ri);
                    break;
                }
            }
        }
        (r < 0 ? keep : cap[static_cast<std::size_t>(r)]).push_back(t);
    }
    Tris out = keep;
    for (const Tris &g : cap) {
        if (g.empty()) {
            continue;
        }
        double X0 = std::numeric_limits<double>::infinity(), X1 = -X0, YT = -X0, Z0 = X0, Z1 = -X0;
        for (const Tri &t : g) {
            for (const Vec &p : t) {
                X0 = std::min(X0, p[0]);
                X1 = std::max(X1, p[0]);
                YT = std::max(YT, p[1]);
                Z0 = std::min(Z0, p[2]);
                Z1 = std::max(Z1, p[2]);
            }
        }
        const double YB = 0.0;
        Append(out, DirQuad(V(X0, YT, Z0), V(X1, YT, Z0), V(X1, YT, Z1), V(X0, YT, Z1), V(0, 1, 0)));
        Append(out, DirQuad(V(X0, YB, Z0), V(X0, YB, Z1), V(X0, YT, Z1), V(X0, YT, Z0), V(-1, 0, 0)));
        Append(out, DirQuad(V(X1, YB, Z0), V(X1, YB, Z1), V(X1, YT, Z1), V(X1, YT, Z0), V(1, 0, 0)));
        Append(out, DirQuad(V(X0, YB, Z0), V(X1, YB, Z0), V(X1, YT, Z0), V(X0, YT, Z0), V(0, 0, -1)));
        Append(out, DirQuad(V(X0, YB, Z1), V(X1, YB, Z1), V(X1, YT, Z1), V(X0, YT, Z1), V(0, 0, 1)));
    }
    for (const Tri &t : TrisFromTable(md::kGateRoof24Tris)) {
        roof_keys.insert(GateKey(t, dx));
    }
    std::erase_if(out, [&](const Tri &t) { return roof_keys.contains(GateKey(t)); });
    const Table &q8 = md::kGateRoof8Quads;
    for (std::size_t q = 0; q < q8.rows; q++) {
        auto P = [&](std::size_t k) { return V(q8.At(q, k * 3) + dx, q8.At(q, k * 3 + 1), q8.At(q, k * 3 + 2)); };
        Append(out, DirQuad(P(0), P(1), P(2), P(3), V(0, 1, 0)));
    }
    for (const auto &[zc, wn] : {
             std::pair{-50.0, V(0, 0, -1)},
             std::pair{50.0,  V(0, 0, 1) }
    }) {
        Append(out, DirQuad(V(-68.0 + dx, 70.0, zc), V(-28.0 + dx, 70.0, zc), V(-28.0 + dx, 78.0, zc), V(-68.0 + dx, 78.0, zc), wn));
    }
    return out;
}

// ── Queens: the ground and camera collision (QueensCollision) ──────────────────────────────────

bool IsGroundSub(std::string_view n) {
    return n.size() == 6 && n.starts_with("e03g") && std::isdigit(static_cast<unsigned char>(n[4])) && std::isdigit(static_cast<unsigned char>(n[5]));
}

std::optional<int> ObjIndex(std::string_view n) {
    if (!n.starts_with("obj") || n.size() < 4 || !std::isdigit(static_cast<unsigned char>(n[3]))) {
        return std::nullopt;
    }
    int v = 0;
    for (std::size_t i = 3; i < n.size() && std::isdigit(static_cast<unsigned char>(n[i])); i++) {
        v = v * 10 + (n[i] - '0');
    }
    return v;
}

// grid3* and the obj40, 44, 6, 33, 34, 43, 45 and 42 structures (the obj1 and obj9 pipes are left
// out: solid drums replace them).
bool IsCameraStructureNode(std::string_view n) {
    if (n.starts_with("grid3")) {
        return true;
    }
    const auto i = ObjIndex(n);
    return i && (*i == 40 || *i == 44 || *i == 6 || *i == 33 || *i == 34 || *i == 43 || *i == 45 || *i == 42);
}

// The loading-zone trigger triangles in each ground _a: the ones whose colour entry carries a
// destination tag. They must survive into the rebuilt _a or the town exits stop working.
std::map<std::string, std::vector<Named>> TriggerNodes(const Bytes &scn) {
    std::map<std::string, std::vector<Named>> out;
    for (const DirEntry &e : Directory(scn)) {
        if (!IsGroundSub(e.name)) {
            continue;
        }
        const std::size_t vo = VariantOff(scn, e, e.name, "_a");
        if (vo == kNone) {
            continue;
        }
        const std::size_t  mds = e.off + vo;
        const MdsNodes     nodes = AccumNodes(scn, mds);
        std::vector<Named> found;
        for (std::size_t ni = 0; ni < nodes.names.size(); ni++) {
            if (nodes.mesh[ni] == 0) {
                continue;
            }
            const std::size_t fo = ResolveMdt(scn, mds, nodes.mesh[ni]);
            if (fo == kNone) {
                continue;
            }
            const std::size_t pos = U32(scn, fo + 0x10), dl = U32(scn, fo + 0x28), col = U32(scn, fo + 0x38);
            const std::size_t tc = U32(scn, fo + dl + 0x14), rb = fo + dl + 0x18;
            const Mat        &M = nodes.world[ni];
            Named             n{nodes.names[ni], {}, {}};
            for (std::size_t t = 0; t < tc; t++) {
                const std::int32_t i0 = I32(scn, rb + t * 0x14), i1 = I32(scn, rb + t * 0x14 + 4), i2 = I32(scn, rb + t * 0x14 + 8);
                const std::int32_t ci = I32(scn, rb + t * 0x14 + 12);
                if (!(col != 0 && ci >= 0)) {
                    continue;
                }
                const std::size_t eo = fo + col + static_cast<std::size_t>(ci) * 0x10;
                if (eo + 0x10 > scn.size() || (scn[eo] | (scn[eo + 1] << 8)) == 0) {
                    continue;
                }
                auto vv = [&](std::int32_t i) {
                    const std::size_t at = fo + pos + static_cast<std::size_t>(i) * 0x10;
                    return Xform(M, {F32(scn, at), F32(scn, at + 4), F32(scn, at + 8)});
                };
                n.tris.push_back({vv(i0), vv(i1), vv(i2)});
                n.attrs.emplace_back(scn.begin() + static_cast<std::ptrdiff_t>(eo), scn.begin() + static_cast<std::ptrdiff_t>(eo + 0x10));
            }
            if (!n.tris.empty()) {
                found.push_back(std::move(n));
            }
        }
        if (!found.empty()) {
            out[e.name] = std::move(found);
        }
    }
    return out;
}

struct QueensGrouped {
    std::vector<std::string>                  subs;
    std::map<std::string, std::vector<Named>> player;
    std::vector<Named>                        camera;
};

QueensGrouped GroupedCollision(const std::vector<PlacedMesh> &placed, const Bytes &scn, std::size_t max_tris = 100) {
    std::map<std::string, Tris> by_sub, cam_by_sub, cam_own;
    for (const PlacedMesh &pm : placed) {
        if (!IsCameraStructureNode(pm.name)) {
            continue;
        }
        Tris src;
        for (const auto &tr : pm.tris) {
            src.push_back({pm.verts.at(static_cast<std::size_t>(tr[0])), pm.verts.at(static_cast<std::size_t>(tr[1])), pm.verts.at(static_cast<std::size_t>(tr[2]))});
        }
        const Tris t = SimplifyTerrain(src);
        if (t.empty()) {
            continue;
        }
        Append(by_sub[pm.sub], t);
        if (pm.name == "obj40" || pm.name == "obj44") {
            Append(cam_own[pm.name], t);
        } else {
            Append(cam_by_sub[pm.sub], t);
        }
    }
    QueensGrouped g;
    for (const auto &[sub, _] : by_sub) {
        g.subs.push_back(sub); // std::map keeps them in ordinal order
    }
    const Tris perim = PerimeterWallTris(), bw = BothWallTris(), inv = InvisibleTris(), pw = WallTris(md::kQueensPlayerWalls);
    const auto triggers = TriggerNodes(scn);
    for (std::size_t i = 0; i < g.subs.size(); i++) {
        const std::string    &sub = g.subs[i];
        std::set<std::string> used;
        Tris                  pool = by_sub[sub];
        if (i == 0) {
            Append(pool, perim);
            Append(pool, bw);
            Append(pool, inv);
            Append(pool, pw);
        }
        std::vector<Named> named = PoolSplit(pool, "pcol", used, max_tris);
        if (auto it = triggers.find(sub); it != triggers.end()) {
            for (const Named &trig : it->second) {
                named.push_back({FitNodeName(trig.name, used), trig.tris, trig.attrs});
            }
        }
        g.player[sub] = std::move(named);
    }
    Tris structs;
    for (const std::string &sub : g.subs) {
        if (auto it = cam_by_sub.find(sub); it != cam_by_sub.end()) {
            Append(structs, it->second);
        }
    }
    Tris cam = FixCameraWinding(structs);
    Append(cam, FixCameraWinding(perim));
    Append(cam, FixCameraWinding(bw));
    Append(cam, TrisFromTable(md::kQueensCameraTris));
    std::set<std::string> cused;
    g.camera = PoolSplit(CamMergeSelected(cam), "ccol", cused, max_tris);
    for (const auto &[name, tris] : cam_own) {
        g.camera.push_back({FitNodeName("c" + name, cused), FixCameraWinding(GateTorchSimplify(tris))});
    }
    return g;
}

// The waterfall frames' Z-write back on (their "z" name flag becomes "x", which means nothing), so
// the player's body hides behind them.
int EnableWaterfallZwrite(Bytes &scn) {
    int n = 0;
    for (std::string_view pat : {"obj48__a01z", "taki2__a01z"}) {
        for (std::size_t j = Find(scn, pat); j != kNone; j = FindFrom(scn, pat, j + 1)) {
            if (scn[j + pat.size() - 1] == 'z') {
                scn[j + pat.size() - 1] = 'x';
                n++;
            }
        }
    }
    return n;
}

// BakeStructures: every ground sub's _a rebuilt, the camera _c gathered on the first ground that
// has one, and the waterfalls' Z-write on.
Bytes QueensBakeStructures(const Bytes &scene, const EDIT_MAP_INFO *info) {
    const std::vector<PlacedMesh> placed = PlacedMeshes(scene, info);
    if (placed.empty()) {
        throw Mismatch("no placed meshes (the mapinfo's GROUND entries)");
    }
    const QueensGrouped g = GroupedCollision(placed, scene);
    if (g.subs.empty()) {
        throw Mismatch("none of the structure meshes (grid3*, obj40 and so on) found");
    }
    Bytes       scn = scene;
    std::size_t player_nodes = 0, player_tris = 0, cam_tris = 0;
    for (const std::string &sub : g.subs) {
        const auto &named = g.player.at(sub);
        scn = ReplaceBlock(scn, sub, BuildFlatMds(named), "_a");
        player_nodes += named.size();
        for (const Named &n : named) {
            player_tris += n.tris.size();
        }
    }
    std::string host;
    for (const std::string &sub : g.subs) {
        const auto dir = Directory(scn);
        if (VariantOff(scn, Sub(dir, sub), sub, "_c") != kNone) {
            host = sub;
            break;
        }
    }
    if (!host.empty() && !g.camera.empty()) {
        scn = ReplaceBlock(scn, host, BuildFlatMds(g.camera), "_c");
        for (const Named &n : g.camera) {
            cam_tris += n.tris.size();
        }
    }
    const int falls = EnableWaterfallZwrite(scn);
    Note(std::format("e03: {} camera nodes ({} tris) on {}, {} player nodes ({} tris) on {} grounds, {} waterfall frames with Z-write, scene {} -> {} bytes",
                     host.empty() ? 0 : g.camera.size(), cam_tris, host.empty() ? "none" : host, player_nodes, player_tris, g.subs.size(), falls,
                     scene.size(), scn.size()));
    return scn;
}

// ── Queens: the snake statue (SnakeStatueCollision) ────────────────────────────────────────────

constexpr std::size_t kSnakeMaxPlayerTris = 1000; // one node for the whole model: fewer, larger nodes snag less
constexpr int         kSnakeHullSegments = 16;
constexpr double      kSnakeHullAxis[2] = {-13.80, 49.65}; // the centre of the two snake-head summit triangles

bool TriMatches(const Tri &t, const Tri &r, double tol = 0.05) {
    for (int rot = 0; rot < 3; rot++) {
        bool ok = true;
        for (int i = 0; i < 3 && ok; i++) {
            for (int j = 0; j < 3; j++) {
                if (std::abs(t[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] - r[static_cast<std::size_t>((i + rot) % 3)][static_cast<std::size_t>(j)]) > tol) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

Tris SnakeSurgery(const Tris &tris) {
    const Tris remove = TrisFromTable(md::kSnakeStatueRemoveTris);
    Tris       kept;
    int        removed = 0;
    for (const Tri &t : tris) {
        if (std::ranges::any_of(remove, [&](const Tri &r) { return TriMatches(t, r); })) {
            removed++;
            continue;
        }
        kept.push_back(t);
    }
    if (removed != static_cast<int>(remove.size())) {
        Note(std::format("e03h06: {} triangles removed but {} listed", removed, remove.size()));
    }
    Append(kept, TrisFromTable(md::kSnakeStatueAddTris));
    return kept;
}

// Every triangle of the part's visual mesh, all mesh nodes, in part space.
Tris FullVisualTris(const Bytes &sub) {
    const std::size_t mds0 = U32(sub, 0x48);
    const MdsNodes    nodes = AccumNodes(sub, mds0);
    Tris              out;
    for (std::size_t i = 0; i < nodes.names.size(); i++) {
        if (nodes.mesh[i] == 0) {
            continue;
        }
        const std::size_t fo = ResolveMdt(sub, mds0, nodes.mesh[i]);
        if (fo == kNone) {
            continue;
        }
        MdtMesh m;
        try {
            m = ParseMdt(sub, fo);
        } catch (const Mismatch &) {
            continue;
        }
        std::vector<Vec> wv;
        for (const Vec4 &p : m.pos) {
            wv.push_back(Xform(nodes.world[i], {p[0], p[1], p[2]}));
        }
        for (const MeshTri &t : MeshTriangles(m, false)) {
            out.push_back({wv.at(static_cast<std::size_t>(t.recs[0][0])), wv.at(static_cast<std::size_t>(t.recs[1][0])), wv.at(static_cast<std::size_t>(t.recs[2][0]))});
        }
    }
    return out;
}

// The camera hull as an open 16-sided cylinder round the snake's head: twice the vanilla hull's
// mean base radius, twice its height.
Tris SnakeCameraCylinder(const Bytes &sub) {
    const std::size_t c_off = U32(sub, 0xc0);
    const std::size_t cnt = U32(sub, c_off + 8), tbl = U32(sub, c_off + 12);
    std::vector<Vec>  vs;
    for (std::size_t i = 0; i < cnt; i++) {
        const std::int32_t mo = I32(sub, c_off + tbl + i * 0x70 + 0x28);
        if (mo == 0) {
            continue;
        }
        const std::size_t fo = c_off + static_cast<std::size_t>(mo);
        const std::size_t w4 = U32(sub, fo + 0x10), w3 = U32(sub, fo + 0x0c);
        for (std::size_t vi = 0; vi < w3; vi++) {
            const std::size_t at = fo + w4 + vi * 0x10;
            vs.push_back({F32(sub, at), F32(sub, at + 4), F32(sub, at + 8)});
        }
    }
    if (vs.empty()) {
        throw Mismatch("e03h06's camera hull has no vertices");
    }
    double ymax = -std::numeric_limits<double>::infinity();
    for (const Vec &v : vs) {
        ymax = std::max(ymax, v[1]);
    }
    std::vector<Vec> base;
    for (const Vec &v : vs) {
        if (v[1] < ymax * 0.5) {
            base.push_back(v);
        }
    }
    if (base.empty()) {
        throw Mismatch("e03h06's camera hull has no base ring");
    }
    double cx = 0, cz = 0, rad = 0;
    for (const Vec &v : base) {
        cx += v[0];
    }
    cx /= static_cast<double>(base.size());
    for (const Vec &v : base) {
        cz += v[2];
    }
    cz /= static_cast<double>(base.size());
    for (const Vec &v : base) {
        rad += std::sqrt((v[0] - cx) * (v[0] - cx) + (v[2] - cz) * (v[2] - cz));
    }
    rad /= static_cast<double>(base.size());
    const double     hx = kSnakeHullAxis[0], hz = kSnakeHullAxis[1], top_y = ymax * 2.0, r = rad * 2.0;
    std::vector<Vec> ring_b, ring_t;
    for (int i = 0; i < kSnakeHullSegments; i++) {
        const double a = 2 * std::numbers::pi * i / kSnakeHullSegments;
        ring_b.push_back({hx + r * std::cos(a), 0.0, hz + r * std::sin(a)});
        ring_t.push_back({hx + r * std::cos(a), top_y, hz + r * std::sin(a)});
    }
    Tris tris;
    for (int i = 0; i < kSnakeHullSegments; i++) {
        const auto j = static_cast<std::size_t>((i + 1) % kSnakeHullSegments);
        const auto ii = static_cast<std::size_t>(i);
        for (Tri tr : {MakeTri(ring_b[ii], ring_b[j], ring_t[j]), MakeTri(ring_b[ii], ring_t[j], ring_t[ii])}) {
            const Vec    n = Normal(tr);
            const double gx = (tr[0][0] + tr[1][0] + tr[2][0]) / 3 - hx, gz = (tr[0][2] + tr[1][2] + tr[2][2]) / 3 - hz;
            if (n[0] * gx + n[2] * gz < 0) {
                tr = MakeTri(tr[0], tr[2], tr[1]);
            }
            tris.push_back(tr);
        }
    }
    return tris;
}

// A fresh collision MDS: the old header, a root null node, then one identity-frame node per chunk
// (entries cloned from the old block's first mesh node).
Bytes BuildCollMds(const Bytes &old_mds, const std::vector<Tris> &chunks, std::string_view prefix) {
    const std::size_t cnt0 = U32(old_mds, 8), tbl0 = U32(old_mds, 12);
    Bytes             tmpl;
    for (std::size_t i = 0; i < cnt0; i++) {
        const std::size_t b = tbl0 + i * 0x70;
        if (I32(old_mds, b + 0x28) != 0) {
            Need(old_mds, b, 0x70, "template node");
            tmpl.assign(old_mds.begin() + static_cast<std::ptrdiff_t>(b), old_mds.begin() + static_cast<std::ptrdiff_t>(b + 0x70));
            break;
        }
    }
    if (tmpl.empty()) {
        throw Mismatch("collision MDS has no mesh node to clone");
    }
    auto entry = [&](std::uint32_t idx, std::string_view nm, std::uint32_t mo, std::int32_t par) {
        Bytes e = tmpl;
        PutU32(e, 0, idx);
        for (std::size_t k = 0; k < 16; k++) {
            e[8 + k] = k < nm.size() ? static_cast<std::uint8_t>(nm[k]) : 0;
        }
        PutU32(e, 0x28, mo);
        PutU32(e, 0x2c, static_cast<std::uint32_t>(par));
        static constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (std::size_t k = 0; k < 16; k++) {
            PutF32(e, 0x30 + k * 4, kIdentity[k]);
        }
        return e;
    };
    const std::size_t n = chunks.size();
    Bytes             out(old_mds.begin(), old_mds.begin() + 0x10);
    PutU32(out, 8, static_cast<std::uint32_t>(n + 1));
    const Bytes root = entry(0, "null1", 0, -1);
    out.insert(out.end(), root.begin(), root.end());
    std::vector<Bytes>       mdts;
    std::vector<std::size_t> offs;
    std::size_t              pos = 0x10 + (n + 1) * 0x70;
    for (const Tris &ch : chunks) {
        mdts.push_back(BuildCollisionMdt(ch));
        offs.push_back(pos);
        pos += Align16(mdts.back().size());
    }
    for (std::size_t k = 0; k < n; k++) {
        const Bytes e = entry(static_cast<std::uint32_t>(k + 1), std::format("{}{:02}", prefix, k), static_cast<std::uint32_t>(offs[k]), 0);
        out.insert(out.end(), e.begin(), e.end());
    }
    for (const Bytes &m : mdts) {
        out.insert(out.end(), m.begin(), m.end());
        Pad16(out);
    }
    return out;
}

// RebuildH06: the snake statue's _a becomes its full visual mesh with the authored surgery, and its
// _c the cylinder; both new blocks go at the sub-file's end with the record repointed.
Bytes RebuildSnakeStatue(const Bytes &scn) {
    const auto      dir = Directory(scn);
    const DirEntry &e = Sub(dir, "e03h06");
    Need(scn, e.off, e.size, "e03h06");
    Bytes             sub(scn.begin() + static_cast<std::ptrdiff_t>(e.off), scn.begin() + static_cast<std::ptrdiff_t>(e.off + e.size));
    const std::size_t a_off = U32(sub, 0x78), a_size = U32(sub, 0x7c), c_off = U32(sub, 0xc0), c_size = U32(sub, 0xc4);
    for (auto [off, size, tag] : {
             std::tuple{a_off, a_size, "_a"},
             std::tuple{c_off, c_size, "_c"}
    }) {
        if (size == 0 || off + size > sub.size() || !IsTag(sub, off, 'M', 'D', 'S')) {
            throw Mismatch(std::format("e03h06's {} block isn't where its record says", tag));
        }
    }
    if (!IsTag(sub, U32(sub, 0x48), 'M', 'D', 'S')) {
        throw Mismatch("e03h06's visual block isn't an MDS");
    }
    const Tris vis = FullVisualTris(sub);
    if (vis.empty()) {
        throw Mismatch("e03h06's visual mesh has no triangles");
    }
    const Bytes       new_c = BuildCollMds(Bytes(sub.begin() + static_cast<std::ptrdiff_t>(c_off), sub.begin() + static_cast<std::ptrdiff_t>(c_off + c_size)),
                                           {SnakeCameraCylinder(sub)}, "hc");
    std::vector<Tris> chunks;
    KdSplitMedian(SnakeSurgery(vis), kSnakeMaxPlayerTris, {0, 2}, chunks);
    const Bytes       new_a = BuildCollMds(Bytes(sub.begin() + static_cast<std::ptrdiff_t>(a_off), sub.begin() + static_cast<std::ptrdiff_t>(a_off + a_size)),
                                           chunks, "ha");
    const std::size_t old_size = sub.size();
    Pad16(sub);
    const std::size_t new_a_off = sub.size();
    sub.insert(sub.end(), new_a.begin(), new_a.end());
    Pad16(sub);
    const std::size_t new_c_off = sub.size();
    sub.insert(sub.end(), new_c.begin(), new_c.end());
    PutU32(sub, 0x78, static_cast<std::uint32_t>(new_a_off));
    PutU32(sub, 0x7c, static_cast<std::uint32_t>(new_a.size()));
    PutU32(sub, 0xc0, static_cast<std::uint32_t>(new_c_off));
    PutU32(sub, 0xc4, static_cast<std::uint32_t>(new_c.size()));
    Note(std::format("e03h06: snake statue collision rebuilt ({} visual tris, {} player nodes), sub {} -> {} bytes", vis.size(), chunks.size(), old_size,
                     sub.size()));
    return sub;
}

// ── Running the patches ────────────────────────────────────────────────────────────────────────

struct PatchStep {
    const char *name;
    void (*run)(Bytes &, const EDIT_MAP_INFO *);
};

void BrownbooCleanup(Bytes &scn, const EDIT_MAP_INFO *) {
    // The order is Expanded's: the corner removal finds the upper rings by their "__n" names.
    Bytes     work = scn;
    const int removed = RemoveRingCornerTris(work);
    CullUpperCraterWalls(work);
    CullBuildings(work);
    scn = std::move(work);
    Note(std::format("s04: crater walls and houses single-sided, {} corner tris removed", removed));
}

void BrownbooCameraHull(Bytes &scn, const EDIT_MAP_INFO *) {
    scn = RebuildCameraHull(scn);
}

void BrownbooRocks(Bytes &scn, const EDIT_MAP_INFO *) {
    scn = AddPondRocks(scn);
}

void YellowDropsWaterSheet(Bytes &scn, const EDIT_MAP_INFO *) {
    RaiseYellowDropsSurfaceMesh(scn);
    Note(std::format("s13: suimenn sheet raised to {}", kYellowDropsWaterY));
}

void YellowDropsWestBank(Bytes &scn, const EDIT_MAP_INFO *info) {
    scn = ReplaceSubInPlace(scn, "s1301", RebuildS1301(scn, info));
}

void QueensSnakeStatue(Bytes &scn, const EDIT_MAP_INFO *) {
    scn = ReplaceSubInPlace(scn, "e03h06", RebuildSnakeStatue(scn));
}

void QueensGroundAndCamera(Bytes &scn, const EDIT_MAP_INFO *info) {
    scn = QueensBakeStructures(scn, info);
}

constexpr PatchStep kBrownbooSteps[] = {
    {"cleanup",     BrownbooCleanup   },
    {"camera hull", BrownbooCameraHull},
    {"pond rocks",  BrownbooRocks     },
};

constexpr PatchStep kYellowDropsSteps[] = {
    {"water sheet", YellowDropsWaterSheet},
    {"west bank",   YellowDropsWestBank  },
};

// Expanded's order: the snake statue swap, then the ground and camera bake.
constexpr PatchStep kQueensSteps[] = {
    {"snake statue",                QueensSnakeStatue    },
    {"ground and camera collision", QueensGroundAndCamera},
};

struct TownPatches {
    std::string_view           map;
    std::span<const PatchStep> steps;
};

constexpr TownPatches kTowns[] = {
    {"s04", kBrownbooSteps   },
    {"s13", kYellowDropsSteps},
    {"e03", kQueensSteps     },
};

std::uint64_t Fnv(const std::uint8_t *data, std::size_t size) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; i++) {
        h = (h ^ data[i]) * 1099511628211ull;
    }
    return h;
}

// Each town's last scene patched, so a second visit copies it rather than baking again.
struct Cached {
    std::size_t   vanilla_size = 0;
    std::uint64_t vanilla_hash = 0;
    Bytes         patched;
    bool          valid = false;
};

std::map<std::string, Cached, std::less<>> g_cached;

bool g_where = false;

} // namespace

void TownMapInfoLoaded(const char *map_name, EDIT_MAP_INFO *info) {
    if (map_name == nullptr || info == nullptr || std::string_view(map_name) != "s13") {
        return;
    }
    // RaiseYellowDropsWaterPlane: the town-wide water surface's corner rows, from 0 to 4.25.
    int raised = 0;
    for (EDIT_WATER_INFO &w : info->water_surfaces) {
        if (w.grid_rows <= 0) {
            break;
        }
        if (w.corner_a[0] == -320.0f && w.corner_a[1] == 0.0f && w.corner_a[2] == -320.0f && w.corner_b[0] == 320.0f && w.corner_b[1] == 0.0f &&
            w.corner_b[2] == 320.0f) {
            w.corner_a[1] = kYellowDropsWaterY;
            w.corner_b[1] = kYellowDropsWaterY;
            raised++;
        }
    }
    if (raised == 0) {
        Note("s13: water surface skipped: no WATER_SURFACE spans -320, 0, -320 to 320, 0, 320");
    } else {
        Note(std::format("s13: water surface raised to {} ({} surface)", kYellowDropsWaterY, raised));
    }
}

void TownSceneLoaded(const char *map_name, u_int *scene, int *size, std::size_t capacity) {
    if (map_name == nullptr || scene == nullptr || size == nullptr || *size <= 0) {
        return;
    }
    const std::string_view map(map_name);
    const TownPatches     *town = nullptr;
    for (const TownPatches &t : kTowns) {
        if (t.map == map) {
            town = &t;
        }
    }
    if (town == nullptr) {
        return;
    }
    auto               *bytes = reinterpret_cast<std::uint8_t *>(scene);
    const std::size_t   vanilla_size = static_cast<std::size_t>(*size);
    const std::uint64_t hash = Fnv(bytes, vanilla_size);
    Cached             &cached = g_cached[std::string(map)];

    if (!(cached.vanilla_size == vanilla_size && cached.vanilla_hash == hash)) {
        Bytes scn(bytes, bytes + vanilla_size);
        int   applied = 0;
        for (const PatchStep &step : town->steps) {
            try {
                step.run(scn, EditMapInfo);
                applied++;
            } catch (const std::exception &error) {
                Note(std::format("{}: {} skipped: {}", map, step.name, error.what()));
            }
        }
        cached = {vanilla_size, hash, std::move(scn), applied > 0};
    }
    if (!cached.valid) {
        return;
    }

    const Bytes &patched = cached.patched;
    if (patched.size() > capacity) {
        Note(std::format("{}: patched scene is {} bytes but only {} fit, left vanilla", map, patched.size(), capacity));
        return;
    }
    std::memcpy(bytes, patched.data(), patched.size());
    *size = static_cast<int>(patched.size());
    Note(std::format("{}: scene {} -> {} bytes, {} bytes of room left after it", map, vanilla_size, patched.size(), capacity - patched.size()));
}

void TownWhereEnable(bool on) {
    g_where = on;
}

void TownWhereStep() {
    if (!g_where || DebugMode != 0 || GameMode != ED_MODE_WALK || Chara == nullptr || pEditGround == nullptr) {
        return;
    }
    if (GamePad.Down(PAD_R3) == 0) {
        return;
    }
    sceVu0FVECTOR pos;
    Chara->GetPosition(pos);

    // The player collision straight down from just above your head, as CheckEditToWalk looks.
    static std::array<CCPoly, 4096> polys;
    CBoxVu0                         box;
    box.max[0] = pos[0] + 2.0f;
    box.min[0] = pos[0] - 2.0f;
    box.max[2] = pos[2] + 2.0f;
    box.min[2] = pos[2] - 2.0f;
    box.max[1] = 20000.0f;
    box.min[1] = -20000.0f;
    box.max[3] = box.min[3] = 0.0f;
    const int count = pEditGround->PickUpPoly(polys.data(), box, 0);

    sceVu0FVECTOR from = {pos[0], pos[1] + 30.0f, pos[2], 1.0f};
    sceVu0FVECTOR to = {pos[0], pos[1] - 1000.0f, pos[2], 1.0f};
    sceVu0FVECTOR hit = {0.0f, 0.0f, 0.0f, 1.0f};
    std::string   floor = "none";
    if (count > 0 && CheckHit(polys.data(), count, from, to, hit, 1, 0) >= 0) {
        floor = std::format("{:.2f}", hit[1]);
    }
    const float       grid = pEditGround->GetAlt(pos[0], pos[1], pos[2]);
    const std::string line = std::format("map {} ({}): x {:.2f}, y {:.2f}, z {:.2f}; floor under you {}; ground grid {:.2f}; {} polys",
                                         MapNo, EditMapName, pos[0], pos[1], pos[2], floor, grid, count);
    LogLine("where.txt", line);
    std::printf("[Where] %s\n", line.c_str());
}

} // namespace dc::game::expanded

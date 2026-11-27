#include "expanded_town_maps.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
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
Bytes BuildCollisionMdt(const std::vector<Tri> &tris) {
    std::map<std::array<std::int64_t, 3>, std::uint32_t> index;
    std::vector<Vec>                                     verts;
    std::vector<std::array<std::uint32_t, 3>>            idx;
    for (const Tri &t : tris) {
        std::array<std::uint32_t, 3> tri{};
        for (int k = 0; k < 3; k++) {
            std::array<std::int64_t, 3> key = {Step(t[k][0], 1000.0), Step(t[k][1], 1000.0), Step(t[k][2], 1000.0)};
            auto [it, inserted] = index.try_emplace(key, static_cast<std::uint32_t>(verts.size()));
            if (inserted) {
                verts.push_back(t[k]);
            }
            tri[k] = it->second;
        }
        idx.push_back(tri);
    }
    const std::size_t   vc = verts.size(), tc = idx.size(), pos = 0x40;
    const std::size_t   dl = Align16(pos + vc * 16);
    const std::size_t   dl_size = 6 * 4 + tc * 0x14;
    const std::size_t   total = Align16(dl + dl_size);
    Bytes               mdt(total, 0);
    const std::uint32_t header[16] = {0x0054444d, 0x40, static_cast<std::uint32_t>(total), static_cast<std::uint32_t>(vc),
                                      static_cast<std::uint32_t>(pos), static_cast<std::uint32_t>(2 * tc), 0, 0, 0, 0,
                                      static_cast<std::uint32_t>(dl), 0, 0, 1, 0, 0};
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
    for (const auto &tri : idx) {
        for (std::uint32_t v : tri) {
            PutU32(mdt, o, v);
            o += 4;
        }
        o += 8; // colour index 0, pad
    }
    return mdt;
}

struct Named {
    std::string      name;
    std::vector<Tri> tris;
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
        Bytes mdt = BuildCollisionMdt(named[i].tris);
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

// ── Running the patches ────────────────────────────────────────────────────────────────────────

struct PatchStep {
    const char *name;
    void (*run)(Bytes &);
};

void BrownbooCleanup(Bytes &scn) {
    // The order is Expanded's: the corner removal finds the upper rings by their "__n" names.
    Bytes     work = scn;
    const int removed = RemoveRingCornerTris(work);
    CullUpperCraterWalls(work);
    CullBuildings(work);
    scn = std::move(work);
    Note(std::format("s04: crater walls and houses single-sided, {} corner tris removed", removed));
}

void BrownbooCameraHull(Bytes &scn) {
    scn = RebuildCameraHull(scn);
}

void BrownbooRocks(Bytes &scn) {
    scn = AddPondRocks(scn);
}

constexpr PatchStep kBrownbooSteps[] = {
    {"cleanup",     BrownbooCleanup   },
    {"camera hull", BrownbooCameraHull},
    {"pond rocks",  BrownbooRocks     },
};

std::uint64_t Fnv(const std::uint8_t *data, std::size_t size) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; i++) {
        h = (h ^ data[i]) * 1099511628211ull;
    }
    return h;
}

// The last scene patched, so a second visit copies it rather than baking again.
struct Cached {
    std::string   map;
    std::size_t   vanilla_size = 0;
    std::uint64_t vanilla_hash = 0;
    Bytes         patched;
    bool          valid = false;
};

Cached g_cached;

bool g_where = false;

} // namespace

void TownSceneLoaded(const char *map_name, u_int *scene, int *size, std::size_t capacity) {
    if (map_name == nullptr || scene == nullptr || size == nullptr || *size <= 0) {
        return;
    }
    const std::string_view map(map_name);
    if (map != "s04") {
        return;
    }
    auto               *bytes = reinterpret_cast<std::uint8_t *>(scene);
    const std::size_t   vanilla_size = static_cast<std::size_t>(*size);
    const std::uint64_t hash = Fnv(bytes, vanilla_size);

    if (!(g_cached.valid && g_cached.map == map && g_cached.vanilla_size == vanilla_size && g_cached.vanilla_hash == hash)) {
        Bytes scn(bytes, bytes + vanilla_size);
        int   applied = 0;
        for (const PatchStep &step : kBrownbooSteps) {
            try {
                step.run(scn);
                applied++;
            } catch (const Mismatch &error) {
                Note(std::format("s04: {} skipped: {}", step.name, error.what()));
            }
        }
        g_cached = {std::string(map), vanilla_size, hash, std::move(scn), applied > 0};
        if (applied == 0) {
            return;
        }
    }

    const Bytes &patched = g_cached.patched;
    if (patched.size() > capacity) {
        Note(std::format("s04: patched scene is {} bytes but only {} fit, left vanilla", patched.size(), capacity));
        return;
    }
    std::memcpy(bytes, patched.data(), patched.size());
    *size = static_cast<int>(patched.size());
    Note(std::format("s04: scene {} -> {} bytes, {} bytes of room left after it", vanilla_size, patched.size(), capacity - patched.size()));
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

#pragma once

/* Dark Cloud Expanded's generated town collision geometry (github.com/Tororoi/DarkCloud-Expanded,
 * IsoPatch/Data/), as C++ tables. BSD 2-Clause, see the notice in expanded_town_map_data.cpp. */

#include <cstddef>

namespace dc::game::expanded::map_data {

/**
 * A list of triangles, one row of nine coordinates (x, y, z of each corner) per triangle.
 */
struct TriTable {
    const double (*rows)[9];
    std::size_t count;
};

/** Brownboo's iwa01 tunnel rock camera hull: the cylinder shell minus the flared tunnel (CSG). */
extern const TriTable kBrownbooIwa01Hull;
/** The iwa01 selection's triangles that stay (the walkway and bank strip through the tunnel). */
extern const TriTable kBrownbooIwa01KeepTris;
/** The iwa01 visual bounding box plus 8 of padding: x0, x1, z0, z1. */
extern const double kBrownbooIwa01SelBox[4];
/** The three hand-simplified pond rocks added to Brownboo's player collision. */
extern const TriTable kBrownbooRockIwa01;
extern const TriTable kBrownbooRockIwa02;
extern const TriTable kBrownbooRockIwa03;

} // namespace dc::game::expanded::map_data

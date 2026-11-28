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

/**
 * A table of rows of `cols` numbers each (triangles are 9: x, y, z of each corner).
 */
struct Table {
    const double *data;
    std::size_t   rows;
    std::size_t   cols;

    double At(std::size_t row, std::size_t col) const { return data[row * cols + col]; }
};

/** Queens (e03) collision tables (IsoPatch/Data/QueensCollisionData.cs): the authored walls, the
 * directed camera simplification's triangles and the bridge-gate geometry. Wall rows are x0, y0,
 * z0, x1, y1, z1, height; flat regions x0, x1, z0, z1, y; pipe drums cx, cy, z0, z1, r. */
extern const Table kQueensBothWalls;
extern const Table kQueensManualTris;
extern const Table kQueensCameraTris;
extern const Table kQueensFlatRegions;
extern const Table kQueensPipeDrums;
extern const Table kQueensPlayerWalls;
extern const Table kQueensCanalContainmentTris;
extern const Table kQueensPerimeterCorners;
extern const Table kQueensPerimeterPreRemoveTris;
extern const Table kQueensPerimeterExtraWalls;
extern const Table kQueensRemoveResidueTris;
extern const Table kQueensBackfaceTris;
extern const Table kQueensTownWallTris;
extern const Table kQueensArcadeBackTris;
extern const Table kQueensZ200NotchTris;
extern const Table kGateRailingTris;
extern const Table kGateWalkwayRaiseTris;
extern const Table kGateOuterFaceExtendTris;
extern const Table kGateOuterFillerTris;
extern const Table kGateRoof24Tris;
extern const Table kGateRoof8Quads;
/** obj44's min x, the bridge-gate tables' reference. */
constexpr double kGateXMinRef = -73.93;

/** Queens' snake statue (e03h06) player-collision surgery (IsoPatch/Data/SceneCollisionData.cs). */
extern const Table kSnakeStatueRemoveTris;
extern const Table kSnakeStatueAddTris;

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

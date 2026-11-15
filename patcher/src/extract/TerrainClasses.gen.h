// GENERATED FILE -- do not edit by hand.
// Written by zelda3ds/tools/gen-terrain-tables.py from its
// TERRAIN_FAMILIES table; edit that and rerun the script.
#pragma once
#include <stdint.h>

// Live-map terrain class per overworld tileRef (0x00-0x3F):
// 0 water, 1 forest, 2 mountain, 3 grave, 4 sand, 5 ground.
static const uint8_t kTerrainClassOfTile[64] = {
    5, 5, 2, 2, 5, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 1, 3, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 2, 1, 1, 1, 3, 3, 3, 3, 2, 5, 5,
    5, 5, 1, 1, 1, 1, 2, 4, 5, 5, 5, 5, 5, 5, 5, 5
};

static const int kTerrainClassCount = 6;

// RGB per class, in the order above.
static const uint8_t kTerrainClassRgb[6][3] = {
    { 0x3E, 0x6F, 0xA6 },  // water
    { 0x3F, 0x7A, 0x3C },  // forest
    { 0x8A, 0x6E, 0x52 },  // mountain
    { 0x86, 0x8C, 0x84 },  // grave
    { 0xC7, 0xB0, 0x78 },  // sand
    { 0x58, 0x94, 0x4E },  // ground
};

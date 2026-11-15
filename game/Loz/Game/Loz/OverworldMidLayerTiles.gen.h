// GENERATED FILE -- do not edit by hand.
// Written by zelda3ds/tools/gen-terrain-tables.py from its
// TERRAIN_FAMILIES/MID_LAYER_EXTRA tables; edit those and rebuild
// (python3 zelda3ds/tools/gen-terrain-tables.py).
//
// true = this overworld tileRef is drawn into the Mid stereoscopic
// depth layer (it stands on the terrain) rather than Ground (it is
// the terrain). Indexed by tileRef, 0x00-0x3F. See
// WorldImpl::DrawMap()/IsOverworldTreeTile() in World.cpp.
#pragma once

static const bool kOverworldMidLayerTile[64] = {
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, true , true , false, false, false,
    false, true , true , true , true , true , true , true ,
    true , false, false, false, false, false, true , true ,
    true , false, false, false, true , false, false, false,
    false, false, true , true , true , true , false, false,
    false, false, false, false, false, false, false, false
};

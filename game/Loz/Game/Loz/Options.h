/*
   Global, cross-slot game options (phase 11.1/11.2). Persisted separately
   from Profile (sdmc:/3ds/zelda3ds/options.dat on 3DS, alongside lastslot.dat)
   since these apply regardless of which save slot is active.
*/
#pragma once


struct GameOptions
{
    uint8_t TextSpeed;       // 0 = Normal (classic NES pace), 1 = Fast
    uint8_t LowHealthBeep;   // 0 = Normal (classic repeating alarm), 1 = Off
    // 0 = this port's existing default: sword beam allowed at full-minus-
    // half-heart, not just pixel-perfect full health. 1 = Strict: classic
    // exact-full-health-only requirement.
    uint8_t SwordBeamStrict;
    // 0 = original text, 1 = clarified rewrites for a few notoriously bad
    // NES translations (phase 11.5) -- only one string done so far, see
    // World.cpp's GetString().
    uint8_t TextVariant;
    // 0 = show the title screen on every boot (the default, so new players
    // always see it), 1 = skip straight to the
    // existing auto-continue/file-menu decision like before this existed.
    uint8_t SkipTitleScreen;
    // phase 10.1: 0 = off (classic, no map hints -- the default, matches
    // every other QoL option's "preserve classic behaviour unless opted
    // in" convention). 1 = the overworld map shows a small pulsing marker
    // at each known secret location (armos-revealed stairs/items, buried
    // items -- see LiveMapGeometry.h's kSecretRooms). This is a single on/
    // off toggle, not a full three-tier (pure/guide/full) hint
    // system (which would also need a "reveal exact secret type" mode) --
    // see phase 10.1 for why that's scoped down for now.
    uint8_t ShowSecretHints;
    // 3DS-only: 0 = off (the default), 1 = show a live FPS
    // counter in the bottom-screen header (Graphics_3ds.cpp's DrawHeader),
    // visible on Map/Items/Options alike -- see Options screen's FPS row.
    uint8_t ShowFpsCounter;
    // phase 14.1: 0 = quest 2 not yet unlocked (the default -- every new
    // install starts here), 1 = unlocked. Set once, permanently, the moment
    // *any* save slot's quest-1 file is won (WorldImpl::GotoWinGame(), only
    // when that profile's Quest==0) -- global/cross-slot like the rest of
    // GameOptions: finishing quest 1 in any slot unlocks quest 2 everywhere,
    // not per slot. Replaces the old "hold Select while starting a new game"
    // hidden-combo trick for reaching quest 2 on the SAVE SLOTS overlay
    // (LiveMap_state.cpp): options gated behind holding button combos are
    // hard to discover and uncomfortable to hold on a 3DS.
    uint8_t Quest2Unlocked;
};


class Options
{
public:
    // Loads the persisted options once; safe to call repeatedly (a no-op
    // after the first call). Call before anything reads Options::Get().
    static void Init();

    static GameOptions& Get();

    // Persists the current in-memory options immediately.
    static void Save();
};

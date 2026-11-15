#ifdef __3DS__
/*
   3DS entry point + main loop. Replaces Loz.cpp's Allegro main/Run.
*/
// Avoid the umbrella <3ds.h> and citro2d here: their friend-service header
// (frd.h) typedefs a `Profile` that clashes with Loz's struct Profile. All
// citro2d frame plumbing is behind Video_* (implemented in Graphics_3ds.cpp).
// The individual libctru headers lack extern "C" (that lives in the umbrella
// <3ds.h>, which we avoid for the frd.h Profile clash), so wrap them ourselves.
extern "C" {
#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/gfx.h>
#include <3ds/romfs.h>
#include <3ds/svc.h>
#include <3ds/os.h>
#include <3ds/services/apt.h>
#include <3ds/services/hid.h>
#include <3ds/console.h>
}
#include <unistd.h>
#include <cstdio>
#include <cstdarg>
#include "Common.h"
#include "Graphics.h"
#include "Input.h"
#include "Sound.h"
#include "AssetStore.h"
#include "World.h"
#include "platform_3ds_video.h"
#include "livemap.h"

// --- frametime instrumentation: once a second, append the average and worst
// per-frame CPU work time (update + draw, excluding the vsync wait) to the SD so
// performance can be read back objectively from the host. The per-second MAX is
// what catches load spikes (e.g. a song load stalling one frame).
// Per-frame SD logging is OFF unless sdmc:/zelda-frametrace.txt exists. This
// is dev instrumentation, and on real hardware it was itself a major cause of
// slowness: SlowFrameLog below fopen/fprintf/fclose's the SD card *every* frame
// whose own-code time tops 5 ms, and an SD write on a 3DS costs several ms
// (FAT update + flush). Once the game was already a bit slow, every frame
// tripped the threshold -> 60 SD writes/sec -> a feedback loop that dragged it
// into a slideshow. Azahar backs "SD" with the host filesystem, so it never
// showed there. Checked once at startup (SetFrameTrace) into this flag.
static bool s_frameTrace = false;
static void SetFrameTrace()
{
    FILE* f = fopen( "sdmc:/zelda-frametrace.txt", "rb" );
    if ( f ) { fclose( f ); s_frameTrace = true; }
}

static void FrameLog( const char* fmt, ... )
{
    if ( !s_frameTrace ) return;
    FILE* f = fopen( "sdmc:/3ds/zelda3ds/frametime.txt", "a" );
    if ( !f ) return;
    va_list ap; va_start( ap, fmt );
    vfprintf( f, fmt, ap );
    va_end( ap );
    fclose( f );
}

// Per-frame phase profiler: any single frame whose TOTAL time exceeds
// SlowFrameThresholdMs gets its full phase breakdown dumped here, so a slow
// frame's cause (which phase, not just "some frame was slow") is visible
// without wading through a per-frame log for the whole run. Added to pin down a stall the "work"/"total" summary couldn't
// explain (occurred while just standing still, not on a level transition).
// Applied to time our own code spends (everything except the mandatory
// vsync wait, which is always ~16.6 ms even on a perfect frame and isn't
// "our" budget to blow). A 1986 NES game's logic + draw should never come
// anywhere near 10 ms on this hardware — any hit here is a real regression.
static const double SlowFrameThresholdMs = 5.0;
static void SlowFrameLog( const char* fmt, ... )
{
    if ( !s_frameTrace ) return;
    FILE* f = fopen( "sdmc:/3ds/zelda3ds/slowframes.txt", "a" );
    if ( !f ) return;
    va_list ap; va_start( ap, fmt );
    vfprintf( f, fmt, ap );
    va_end( ap );
    fclose( f );
}

static inline double TicksToMs( u64 ticks )
{
    return ticks * 1000.0 / SYSCLOCK_ARM11;
}

static uint32_t frameCounter = 0;

uint32_t GetFrameCounter()
{
    return frameCounter;
}

// Temporary fine-grained profiler for World::LoadLevel() (shared code calls
// this via the platform-agnostic Global.h declaration; see phase 5.1).
// Logs elapsed ms since the previous mark, so a sequence of marks around each
// LoadLevel step shows exactly where a level-load spike goes.
void ProfileMark( const char* label )
{
    // phase 15.3 release hygiene: no current call site wires this up (it's
    // declared for a shared-code hook that isn't invoked anywhere right
    // now), but it wrote to the SD completely unconditionally if it ever
    // were -- gated behind the same opt-in flag as every other *-debug.txt
    // channel as cheap insurance against that changing unnoticed.
    if ( !s_frameTrace ) return;

    static u64 lastTick = 0;
    u64 now = svcGetSystemTick();
    if ( lastTick != 0 )
    {
        double ms = TicksToMs( now - lastTick );
        FILE* f = fopen( "sdmc:/3ds/zelda3ds/loadlevel-debug.txt", "a" );
        if ( f )
        {
            fprintf( f, "  +%6.2f ms  -> %s\n", ms, label );
            fclose( f );
        }
    }
    lastTick = now;
}

// The shared code declares these (Loz.cpp provides them on desktop).
ALLEGRO_CONFIG* GetConfig()
{
    return nullptr;
}

void ResizeView( int, int )
{
}

// Sleep (clamshell close) / HOME-menu suspend, and clean HOME-exit (phase
// 9.1/9.2). aptMainLoop() alone degrades gracefully when the system stops
// calling us (the vsync wait inside Video_BeginFrame() just stalls), but
// without these hooks the background audio thread + ndsp channels are never
// told to pause — on some firmware/DSP states that risks audio glitching or
// drifting out of sync with the (also paused) game clock once the system
// resumes. ONEXIT forces a final unthrottled save (World::SaveNow()) so a
// clean HOME-exit never loses progress since the last room-entry autosave
// (phase 8.3) — the periodic autosave already covers the power-loss case.
static aptHookCookie s_aptHookCookie;
// Tracks whether *this* hook was the one that paused audio, so a wake-up
// doesn't unpause audio the player had already paused themselves (in-game
// Select-pause) before the system went to sleep — see Sound::IsPaused().
static bool s_hookPausedAudio = false;

static void AptHookCallback( APT_HookType hook, void* )
{
    switch ( hook )
    {
    case APTHOOK_ONSUSPEND:
    case APTHOOK_ONSLEEP:
        if ( !Sound::IsPaused() )
        {
            s_hookPausedAudio = true;
            Sound::Pause();
        }
        break;
    case APTHOOK_ONRESTORE:
    case APTHOOK_ONWAKEUP:
        if ( s_hookPausedAudio )
        {
            s_hookPausedAudio = false;
            Sound::Unpause();
        }
        break;
    case APTHOOK_ONEXIT:
        World::SaveNow();
        break;
    default:
        break;
    }
}

// Phase 15.1: enable the New 3DS CPU/L2-cache speedup (268MHz ->
// ~804MHz). Old 3DS has no such feature; APT_CheckNew3DS() gates this so the
// call is simply never made there.
//
// Briefly flipped to opt-in, then back to opt-out the same day, after
// enemies were seen moving visibly faster in Azahar with this on. Root cause:
// this game's whole loop is vsync-locked with no delta-time scaling (one
// World::Update() per C3D_FrameBegin(C3D_FRAME_SYNCDRAW) iteration, fixed
// per-frame movement increments, faithfully mirroring the original NES's
// own PPU-vblank-driven timing) -- so gameplay speed is directly tied to
// how often that loop actually runs. citro3d's own source (renderqueue.c)
// confirms real hardware can't have this problem: C3D_FrameSync() blocks on
// real GSPGPU_EVENT_VBlank0/1 hardware interrupts, and its frame counter can
// only advance once per genuine (fixed 60Hz, CPU-clock-independent) vblank.
// Azahar's own vblank emulation apparently doesn't keep that same
// independence from its clock-speed simulation, which is what showed up there.
// Fixed properly rather than just distrusted: see the real-time governor on
// World::Update() itself, right where it's called below -- gated on
// osGetTime() (RTC-backed, confirmed via libctru's own source to
// recalibrate its ms-per-tick conversion from real RTC measurements, so it
// stays correct regardless of the actual CPU clock) rather than
// svcGetSystemTick() (which ticks at the CPU's own rate and would silently
// misreport elapsed time under this exact speedup). That governor caps
// World::Update() to at most 60Hz of real wall-clock time on any platform,
// closing the actual mechanism behind the bug rather than just avoiding it.
// Confirmed in Azahar with the speedup forced on: enemies read at normal
// speed again, Azahar's own speed readout stayed ~100%. Debug escape hatch,
// restored to its original opt-out sense: if
// sdmc:/zelda3ds-debug-noboost.txt exists, skip the speedup even on a real
// New 3DS, for representative old-268MHz-clock frametime measurements.
static void ApplyNew3DSSpeedup()
{
    FILE* f = fopen( "sdmc:/zelda3ds-debug-noboost.txt", "rb" );
    if ( f )
    {
        fclose( f );
        return;
    }

    bool isNew3DS = false;
    if ( R_SUCCEEDED( APT_CheckNew3DS( &isNew3DS ) ) && isNew3DS )
        osSetSpeedupEnable( true );
}

// Shown when AssetStore_Init() finds no usable ROM. Uses libctru's built-in
// text console, so it needs none of the game's assets.
static void ShowRomMissingScreen( const char* detail )
{
    consoleInit( GFX_TOP, nullptr );
    printf( "\n  The Legend of Zelda - 3DS Edition\n\n" );
    printf( "  This game needs your own copy of the\n" );
    printf( "  original NES ROM to run. Either build\n" );
    printf( "  it in with the PC patcher, or:\n\n" );
    printf( "  Copy  The Legend of Zelda (USA)\n" );
    printf( "  (PRG0, the first release) as a .nes\n" );
    printf( "  file to this folder on the SD card:\n\n" );
    printf( "      sd:/3ds/zelda3ds/\n\n" );
    printf( "  and start the game again.\n\n" );
    printf( "  Problem found:\n" );
    printf( "  %.300s\n\n", detail );
    printf( "  Press START to exit.\n" );

    while ( aptMainLoop() )
    {
        hidScanInput();
        if ( hidKeysDown() & KEY_START )
            break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

int main( int argc, char* argv[] )
{
    gfxInitDefault();
    ApplyNew3DSSpeedup();
    romfsInit();
    // Assets are referenced by bare filename; make romfs the working directory.
    chdir( "romfs:/" );

    // Build the game's assets from the player's own ROM. Without it there is
    // nothing to play, so explain where it goes and stop.
    char romError[256] = "";
    if ( !AssetStore_Init( romError, sizeof( romError ) ) )
    {
        ShowRomMissingScreen( romError );
        romfsExit();
        gfxExit();
        return 0;
    }

    Video_Init();

    Graphics::Init();
    Sound::Init();
    Input::Init();
    // hidScanInput()'s first-ever call pays a one-time HID shared-memory/IPC
    // setup cost (~2 ms, seen on frame 0 in slowframes.txt); absorb it here,
    // before the loop, instead of on the first real frame. See phase 5.1.
    Input::Update();
    World::Init();

    // Registered once World::Init() has a profile ready to save on ONEXIT —
    // see AptHookCallback() above (phase 9.1/9.2).
    aptHook( &s_aptHookCookie, AptHookCallback, nullptr );

    LiveMapState mapState = {};

    // Enable per-frame SD profiling only if the opt-in flag file is present
    // (see s_frameTrace). Off by default so normal play never touches the SD
    // mid-frame. When on, start each run's logs fresh.
    SetFrameTrace();
    if ( s_frameTrace )
    {
        remove( "sdmc:/3ds/zelda3ds/frametime.txt" );
        remove( "sdmc:/3ds/zelda3ds/slowframes.txt" );
        remove( "sdmc:/3ds/zelda3ds/vram-debug.txt" );
        // How long building the assets took at startup (ROM-on-SD route).
        if ( FILE* f = fopen( "sdmc:/3ds/zelda3ds/frametime.txt", "a" ) )
        {
            fprintf( f, "%s\n", AssetStore_TimingReport() );
            fclose( f );
        }
    }
    double sumWork = 0, maxWork = 0;
    double sumTotal = 0, maxTotal = 0;
    uint32_t maxFrame = 0, maxTotalFrame = 0, statCount = 0;

    // Live FPS for the Options screen. True frame-to-frame wall-clock time,
    // including the vsync wait -- what actually reaches the screen, not just
    // "our own code" time. Smoothed (EMA) so it reads steadily instead of
    // jittering frame to frame; no SD I/O, cheap enough to always run.
    u64   prevFrameTick = svcGetSystemTick();
    float smoothedFps = 60.0f;

    while ( aptMainLoop() )
    {
        u64 t0 = svcGetSystemTick();
        double frameToFrameMs = TicksToMs( t0 - prevFrameTick );
        prevFrameTick = t0;
        if ( frameToFrameMs > 0.001 )
        {
            float instFps = (float) (1000.0 / frameToFrameMs);
            smoothedFps = smoothedFps * 0.9f + instFps * 0.1f;
        }
        mapState.fps = smoothedFps;

        // Game logic — the CPU work we control. Measured up to Video_BeginFrame,
        // whose C3D_FRAME_SYNCDRAW blocks on vsync (~16.6 ms) and would otherwise
        // swamp the number. This isolates real headroom and spikes (e.g. a
        // synchronous song load stalling World::Update).
        Input::Update();
        u64 tInput = svcGetSystemTick();

        // Phase 15.1 follow-up: real-time governor on
        // World::Update() specifically -- added after enemies were seen
        // moving visibly faster in Azahar with the New 3DS speedup on. Game
        // speed is tied to the frame rate: World::Update() runs once per
        // loop iteration with no delta-time
        // scaling at all, exactly like the original NES's own vblank-driven
        // timing, so however often this loop actually iterates per real
        // second literally *is* the game's speed). The loop's iteration
        // rate is supposed to be hard-locked to 60Hz by Video_BeginFrame()'s
        // vsync wait -- true on real hardware (a physical vblank interrupt
        // is a fixed 60Hz signal regardless of CPU clock), but Azahar's own
        // vblank emulation apparently doesn't stay independent of its
        // clock-speed simulation (see DEVELOPMENT.md's phase 15.1 write-up
        // for the citro3d-source analysis that led here). This makes
        // World::Update() itself immune to that regardless of platform:
        // gated on osGetTime() (RTC-backed real wall-clock milliseconds --
        // deliberately NOT svcGetSystemTick(), which ticks at the CPU's
        // *own* clock rate and would silently misreport elapsed time under
        // this exact speedup), capped to at most once per ~16.667ms of real
        // time. No catch-up if the loop falls behind -- a slow real-device
        // frame just runs logic once, same as today, rather than bursting
        // multiple updates to "catch up" (slower is acceptable, faster is
        // not). Only World::Update() is
        // gated: Input/Sound/LiveMap_Update() all still run every loop
        // iteration regardless (no reason to throttle input polling or
        // audio servicing, and recomputing the same unchanged map state on
        // a throttled iteration is harmless).
        static u64   s_lastLogicTimeMs = 0;
        static double s_logicAccumMs = 0.0;
        const double LogicIntervalMs = 1000.0 / 60.0;
        u64 nowRealMs = osGetTime();
        bool runLogic;
        if ( s_lastLogicTimeMs == 0 )
        {
            runLogic = true;   // first iteration: always run
        }
        else
        {
            s_logicAccumMs += (double) ( nowRealMs - s_lastLogicTimeMs );
            runLogic = s_logicAccumMs + 0.5 >= LogicIntervalMs;
            if ( runLogic )
            {
                s_logicAccumMs -= LogicIntervalMs;
                if ( s_logicAccumMs < 0.0 || s_logicAccumMs > LogicIntervalMs )
                    s_logicAccumMs = 0.0;   // clamp -- no catch-up burst on a real stall
            }
        }
        s_lastLogicTimeMs = nowRealMs;

        if ( runLogic )
            World::Update();
        u64 tWorldUpdate = svcGetSystemTick();

        // Temporary diagnostic (verifying the governor above
        // actually holds World::Update() to 60Hz -- judging it by eye isn't
        // reliable enough either way). Counts real logic updates in
        // each real 1-second window (again via osGetTime(), not
        // svcGetSystemTick()) and logs the count once that window closes.
        // Gated behind the same sdmc:/zelda-frametrace.txt opt-in as every
        // other diagnostic channel. Confirmed in Azahar: logicHz reads
        // 56-60, never above, with the speedup on by default -- keeping this
        // in place until the same number is checked on real hardware too,
        // then remove.
        if ( s_frameTrace )
        {
            static u64 s_logicCountWindowStartMs = 0;
            static int s_logicCountThisWindow = 0;
            if ( s_logicCountWindowStartMs == 0 )
                s_logicCountWindowStartMs = nowRealMs;
            if ( runLogic )
                s_logicCountThisWindow++;
            if ( nowRealMs - s_logicCountWindowStartMs >= 1000 )
            {
                FrameLog( "logicHz=%d\n", s_logicCountThisWindow );
                s_logicCountThisWindow = 0;
                s_logicCountWindowStartMs = nowRealMs;
            }
        }

        Sound::Update();
        u64 tSound = svcGetSystemTick();

        LiveMap_Update( mapState );
        u64 tLiveMap = svcGetSystemTick();

        double workMs = TicksToMs( tLiveMap - t0 );

        Video_BeginFrame();   // blocks here on vsync (C3D_FRAME_SYNCDRAW)
        u64 tBeginFrame = svcGetSystemTick();

        World::Draw();
        u64 tDraw = svcGetSystemTick();

        // phase 6.1: blit the offscreen 2x top-screen buffer World::Draw()
        // just populated onto the real top screen (bilinear + 8:7 PAR).
        Video_BlitTopScreen();

        // phase 5.1b: skip entirely once the (100% static) load-screen
        // content has already been drawn into both physical buffers -- see
        // livemap.h's skipBottomMapDraw comment. citro3d doesn't require a
        // render target to be touched every frame; an untouched target's
        // buffer is simply presented unchanged.
        if ( !mapState.skipBottomMapDraw )
            Video_DrawBottomMap( mapState, frameCounter );
        u64 tBottomMap = svcGetSystemTick();

        sumWork += workMs;
        if ( workMs > maxWork ) { maxWork = workMs; maxFrame = frameCounter; }

        Video_EndFrame();
        u64 tEndFrame = svcGetSystemTick();

        // Total wall time for the whole loop body (logic + vsync wait inside
        // Video_BeginFrame + draw + present) — this is what should line up
        // with Azahar's own on-screen "Frame:" HUD counter, unlike `work`
        // above which deliberately excludes draw/vsync. See phase 5.1.
        double totalMs = TicksToMs( tEndFrame - t0 );
        sumTotal += totalMs;
        if ( totalMs > maxTotal ) { maxTotal = totalMs; maxTotalFrame = frameCounter; }

        // Any single frame where OUR code (not the vsync wait) runs long
        // gets its full phase breakdown dumped, so the cause is visible
        // without scanning a per-frame log for the whole run.
        double vsyncWaitMs = TicksToMs( tBeginFrame - tLiveMap );
        double ownCodeMs = totalMs - vsyncWaitMs;
        if ( ownCodeMs > SlowFrameThresholdMs )
        {
            SlowFrameLog(
                "f%-7lu total=%6.2f ms  ownCode=%6.2f ms  input=%5.2f  "
                "worldUpdate=%6.2f  sound=%5.2f  liveMap=%5.2f  "
                "vsyncWait=%6.2f  worldDraw=%6.2f  bottomMap=%5.2f  "
                "endFrame=%6.2f\n",
                (unsigned long) frameCounter, totalMs, ownCodeMs,
                TicksToMs( tInput - t0 ),
                TicksToMs( tWorldUpdate - tInput ),
                TicksToMs( tSound - tWorldUpdate ),
                TicksToMs( tLiveMap - tSound ),
                vsyncWaitMs,
                TicksToMs( tDraw - tBeginFrame ),
                TicksToMs( tBottomMap - tDraw ),
                TicksToMs( tEndFrame - tBottomMap ) );
        }

        if ( ++statCount >= 60 )
        {
            FrameLog( "f%-7lu work avg=%5.2f ms max=%6.2f ms (@f%lu)  "
                      "total avg=%5.2f ms max=%6.2f ms (@f%lu)  [budget 16.6]\n",
                      (unsigned long) frameCounter, sumWork / statCount, maxWork,
                      (unsigned long) maxFrame, sumTotal / statCount, maxTotal,
                      (unsigned long) maxTotalFrame );
            sumWork = 0; maxWork = 0; sumTotal = 0; maxTotal = 0; statCount = 0;
        }

        frameCounter++;
    }

    aptUnhook( &s_aptHookCookie );
    World::Uninit();
    Video_Fini();
    romfsExit();
    gfxExit();
    return 0;
}

#endif // __3DS__

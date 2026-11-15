#ifdef __3DS__
/*
   3DS sound backend (ndsp).

   Mirrors the desktop Allegro Sound.cpp: two song "streams" (main music +
   an event/jingle stream that ducks the music) plus five effect instances,
   driven by the same Songs.dat / Effects.dat lists.

   Playback model:
   * Effects are small — preloaded whole into linear memory, one wave buffer each
     (unchanged from the original design; these already play correctly).
   * Songs are synthesised **live** from the 21 KB NSF (Tools/nsfextract emits
     romfs:/zelda.nsf) using the bundled Game_Music_Emu (GME) Nsf_Emu, one
     instance per stream. This replaces an earlier whole-WAV-file design: songs
     are multi-MB, and loading one per area/dungeon change stalled a frame
     (~333 ms, see frametime.txt) *and* multi-MB ndsp wave buffers
     played silent on the emulator. Live synthesis needs neither: only the
     21 KB NSF is ever loaded (once, at Init), and each stream refills a small
     ring of ndsp wave buffers (a few hundred ms of audio total) from a
     dedicated background thread, not from Sound::Update() on the main thread.
     (An earlier version refilled from Update(); that ties refill cadence to
     the *game*'s frame rate, but the DSP consumes queued samples in real wall-
     clock time regardless of how fast the game/host is actually running —
     when Azahar itself ran below real-time (observed during its first ~20s,
     e.g. JIT warm-up), Update() was called too infrequently in wall-clock
     time to keep the ring fed, so it starved. A background thread polls on
     its own real-time cadence and isn't affected by game-frame pacing.)
   * Looping songs: the NSF's own driver code loops the tune forever on the
     NES side, so a looping stream is simply never cut off.
   * Non-looping songs (jingles, victory/death/etc): cut off once the stream's
     own rendered-sample count reaches the [Begin,End] frame markers already
     in Songs.dat (same markers the old WAV-based code used to size its
     buffer), then let the queued ndsp buffers drain naturally.
   * Event/jingle ducking (PushSong) pauses the main stream (ndsp keeps its
     position) and starts the jingle on the second stream; when it ends the
     main stream resumes — unchanged from the original design.

   The individual libctru headers are wrapped in extern "C" (as in main_3ds.cpp)
   to avoid the <3ds.h> umbrella, whose frd.h typedefs a `Profile` that clashes
   with Loz's struct Profile.
*/
#include "Common.h"
#include "Sound.h"
#include "AssetStore.h"
#include "SoundId.h"
#include "Nsf_Emu.h"

extern "C" {
#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/allocator/linear.h>
#include <3ds/ndsp/ndsp.h>
#include <3ds/ndsp/channel.h>
#include <3ds/services/dsp.h>
#include <3ds/synchronization.h>
#include <3ds/thread.h>
#include <3ds/svc.h>
}
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdint>

// --- diagnostics: appends to sdmc:/3ds/zelda3ds/sound-debug.txt. Release-
// hygiene fix (phase 15.3): this used to be an unconditional
// `#define SND_DEBUG 1`, logging every song/effect event (incl. underruns,
// which can repeat rapidly) to the SD on every real playthrough -- the same
// class of always-on-SD-write problem main_3ds.cpp's s_frameTrace comment
// documents (FAT update + flush cost per write; frequent enough calls can
// snowball into a slowdown). Now OFF unless sdmc:/zelda-frametrace.txt
// exists, same opt-in convention as every other *-debug.txt channel.
static bool SoundDebugEnabled()
{
    static int enabled = -1;
    if ( enabled < 0 )
    {
        FILE* f = fopen( "sdmc:/zelda-frametrace.txt", "rb" );
        enabled = f ? 1 : 0;
        if ( f ) fclose( f );
    }
    return enabled != 0;
}
static void SndLog( const char* fmt, ... )
{
    if ( !SoundDebugEnabled() ) return;
    FILE* f = fopen( "sdmc:/3ds/zelda3ds/sound-debug.txt", "a" );
    if ( !f ) return;
    va_list ap; va_start( ap, fmt );
    vfprintf( f, fmt, ap );
    va_end( ap );
    fclose( f );
}

enum
{
    Instances    = 5,
    Streams      = 2,
    LoPriStreams = Streams - 1,
    Songs        = Song_Max,
    Effects      = SEffect_Max,
    NoSound      = 0xFF,

    // ndsp channel layout: songs on 0..1, effect instances on 2..6.
    SongChBase   = 0,
    FxChBase     = Streams,

    SampleRate      = 44100,
    SamplesPerFrame = SampleRate / 60,   // 735

    // Streaming ring per song stream: 6 x ~46 ms = ~280 ms buffered ahead of
    // playback, refilled by a background thread (AudioThreadFunc) on its own
    // ~5 ms real-time cadence — far below the size where the emulator's
    // large-wave-buffer silence quirk kicks in (the preloaded effect WAVs, up
    // to ~300 KB / ~150k samples, play fine). The extra margin (vs. an
    // earlier 4-buffer/185ms ring) absorbs room-transition CPU bursts on the
    // main thread (see AudioThreadPrio below).
    ChunkSamples = 2048,
    NumWaveBufs  = 6,

    AudioThreadStackSize = 8 * 1024,
    // 20 ms: comfortably under one chunk's 46 ms playtime (several buffers stay
    // queued ahead at all times) while waking 4x less often than an earlier
    // 5 ms poll. Azahar may not give homebrew genuine multicore parallelism
    // (New3DS extra cores need exheader permission .3dsx doesn't carry), so
    // this thread's wakeups plausibly time-slice the *same* emulated core as
    // the main thread on that emulator — fewer, cheaper wakeups reduces
    // context-switch overhead on the host regardless of which model applies.
    AudioThreadSleepNs = 20 * 1000 * 1000,
};

enum SoundFlags
{
    SoundFlag_PlayIfQuietSlot = 1,
};

// 28-byte on-disk record shared with the desktop build (Songs.dat/Effects.dat).
struct SoundInfo
{
    int16_t Begin;
    int16_t End;
    int8_t  Slot;
    int8_t  Priority;
    int8_t  Flags;
    int8_t  Reserved;
    char    Filename[20];
};

struct EffectRequest
{
    uint8_t SoundId;
    bool    Loop;
};

struct FxSample
{
    int16_t* pcm;           // preloaded, kept for the whole session
    uint32_t nsamples;
};

// A song stream, synthesised live from the shared NSF into a small ring of
// ndsp wave buffers.
struct SongStream
{
    int16_t*    chunks[NumWaveBufs];    // linear, allocated once at Init
    ndspWaveBuf wavebufs[NumWaveBufs];
    bool        active;
    bool        loops;                  // true => restart at endSample instead of stopping
    int         songId;                 // currently loaded track, for the loop restart
    uint32_t    samplesRendered;        // since the current start_track()
    uint32_t    endSample;
};

// Render at a fixed gain low enough that nothing clips int16, then
// software-scale to just under full scale (see nsfextract's RenderTrack for
// the full explanation, and its own printed "Global peak ... at gain ...").
// Values below must match Loz/Tools/nsfextract/main.cpp's RenderGain/
// TargetPeak/measured global peak, so live music and the preloaded effect
// WAVs stay balanced against each other.
static const double LiveGain  = 1.0;
static const double LiveScale = 29000.0 / 11606.0;

static bool          initialized;
static bool          paused;
static SoundInfo     songs[Songs];
static SoundInfo     effects[Effects];
static Nsf_Emu       nsfEmu[Streams];
static SongStream    streams[Streams];
static FxSample      fxSamples[Effects];
static ndspWaveBuf   fxWaveBuf[Instances];
static EffectRequest effectRequests[Instances];
static bool          eventActive;        // event/jingle stream is playing
static bool          mainWasPlaying;     // main stream paused under an event

// Guards streams[]/nsfEmu[] state shared between the main thread (PlaySong/
// PushSong/StopSongs, and the event-finished check in Update()) and the
// background audio thread (RefillStream). Internal helpers (PlaySongInternal,
// StopStream, RefillStream, RefillOneBuffer) assume the caller already holds
// this lock; only the public Sound:: entry points and AudioThreadFunc take it.
static LightLock     songLock;
static Thread        audioThread;
static volatile bool audioThreadRunning;


// ---- WAV helpers (effects only) --------------------------------------------

// Open a mono 16-bit WAV and locate its "data" chunk. Returns true and fills
// *dataStart (file offset) / *dataSamples on success; leaves the file positioned
// at the start of the PCM.
static bool OpenWav( FILE* f, long* dataStart, uint32_t* dataSamples )
{
    char riff[12];
    if ( fread( riff, 1, 12, f ) != 12 || memcmp( riff, "RIFF", 4 ) != 0 )
        return false;
    char id[4]; uint32_t sz;
    while ( fread( id, 1, 4, f ) == 4 && fread( &sz, 4, 1, f ) == 1 )
    {
        if ( memcmp( id, "data", 4 ) == 0 )
        {
            *dataStart   = ftell( f );
            *dataSamples = sz / sizeof( int16_t );
            return true;
        }
        fseek( f, ( sz + 1 ) & ~1u, SEEK_CUR );
    }
    return false;
}

// Load a whole mono 16-bit WAV into linear memory (for effects). Returns sample
// count or 0; *out receives the buffer.
static uint32_t LoadWav( const char* filename, int16_t** out )
{
    *out = nullptr;
    FILE* f = AssetStore_Open( filename, "rb" );
    if ( !f ) return 0;
    long start; uint32_t n;
    if ( !OpenWav( f, &start, &n ) || n == 0 ) { fclose( f ); return 0; }

    int16_t* pcm = (int16_t*) linearAlloc( n * sizeof( int16_t ) );
    if ( !pcm ) { fclose( f ); return 0; }
    fseek( f, start, SEEK_SET );
    if ( fread( pcm, sizeof( int16_t ), n, f ) != n ) { linearFree( pcm ); fclose( f ); return 0; }
    fclose( f );
    DSP_FlushDataCache( pcm, n * sizeof( int16_t ) );
    *out = pcm;
    return n;
}

static bool LoadList( const char* filename, SoundInfo* out, int count )
{
    FILE* f = AssetStore_Open( filename, "rb" );
    if ( !f ) return false;
    size_t got = fread( out, sizeof( SoundInfo ), count, f );
    fclose( f );
    return got == (size_t) count;
}

static void ConfigChannel( int ch )
{
    ndspChnReset( ch );
    ndspChnSetInterp( ch, NDSP_INTERP_LINEAR );
    ndspChnSetRate( ch, (float) SampleRate );
    ndspChnSetFormat( ch, NDSP_FORMAT_MONO_PCM16 );
}


// ---- songs (live GME synthesis) --------------------------------------------

static inline int16_t ClampS16( double v )
{
    if ( v >  32767.0 ) return  32767;
    if ( v < -32768.0 ) return -32768;
    return (int16_t) ( v >= 0 ? v + 0.5 : v - 0.5 );
}

// Renders `n` mono samples from `emu` into `dst` (linear memory), applying the
// same gain/scale as nsfextract so live music matches the effect WAVs' loudness.
// GME emits interleaved stereo (identical L/R for NSF); keep the left sample.
static void RenderChunk( Nsf_Emu& emu, int16_t* dst, int n )
{
    static int16_t stereo[ChunkSamples * 2];
    emu.play( n * 2, stereo );
    for ( int i = 0; i < n; i++ )
        dst[i] = ClampS16( stereo[i * 2] * LiveScale );
}

// Fills one wave buffer slot with the next chunk and queues it. `nsfextract`
// never renders any track past its authored End frame (every WAV, including
// looping ones, is captured exactly [0,End) then hardware-looped) — so the
// NES driver code past that point is unvalidated territory, not something to
// trust to "just keep looping". Reaching endSample therefore always restarts
// the same track from the top for looping streams; non-looping streams just
// stop feeding (status stays FREE/DONE) so the queue drains and the channel
// stops on its own, unchanged from the original design.
static void RefillOneBuffer( SongStream& st, int streamIdx, int bufIdx )
{
    if ( st.samplesRendered >= st.endSample )
    {
        if ( !st.loops )
            return;
        nsfEmu[streamIdx].start_track( st.songId );
        st.samplesRendered = 0;
    }

    uint32_t remaining = ChunkSamples;
    uint32_t left = st.endSample - st.samplesRendered;
    if ( left < remaining ) remaining = left;
    if ( remaining == 0 )
        return;

    RenderChunk( nsfEmu[streamIdx], st.chunks[bufIdx], (int) remaining );
    DSP_FlushDataCache( st.chunks[bufIdx], remaining * sizeof( int16_t ) );

    ndspWaveBuf& wb = st.wavebufs[bufIdx];
    memset( &wb, 0, sizeof( wb ) );
    wb.data_pcm16 = st.chunks[bufIdx];
    wb.nsamples   = remaining;
    wb.looping    = false;
    ndspChnWaveBufAdd( SongChBase + streamIdx, &wb );

    st.samplesRendered += remaining;
}

static uint32_t underrunCount[Streams];

// Refills every buffer that's free/done. Called only from AudioThreadFunc,
// with songLock held.
static void RefillStream( int streamIdx )
{
    SongStream& st = streams[streamIdx];
    if ( !st.active ) return;

    bool shouldBePlaying = st.loops || st.samplesRendered < st.endSample;
    if ( shouldBePlaying && !ndspChnIsPlaying( SongChBase + streamIdx )
        && !ndspChnIsPaused( SongChBase + streamIdx ) )
    {
        underrunCount[streamIdx]++;
        SndLog( "UNDERRUN stream=%d count=%lu samplesRendered=%lu endSample=%lu\n",
                streamIdx, (unsigned long) underrunCount[streamIdx],
                (unsigned long) st.samplesRendered, (unsigned long) st.endSample );
    }

    for ( int b = 0; b < NumWaveBufs; b++ )
    {
        uint8_t status = st.wavebufs[b].status;
        if ( status == NDSP_WBUF_FREE || status == NDSP_WBUF_DONE )
            RefillOneBuffer( st, streamIdx, b );
    }
}

// Background refill thread: polls on its own real-time cadence so the ndsp
// ring stays fed even if the game/host is running below real-time (see the
// file header comment) — refilling from Sound::Update() ties refill cadence
// to the game's frame rate, which starved when the host fell behind.
static void AudioThreadFunc( void* )
{
    while ( audioThreadRunning )
    {
        LightLock_Lock( &songLock );
        for ( int i = 0; i < Streams; i++ )
            RefillStream( i );
        LightLock_Unlock( &songLock );
        svcSleepThread( AudioThreadSleepNs );
    }
}

static void StopStream( int streamId )
{
    SongStream& st = streams[streamId];
    ndspChnWaveBufClear( SongChBase + streamId );
    for ( int b = 0; b < NumWaveBufs; b++ )
        memset( &st.wavebufs[b], 0, sizeof( st.wavebufs[b] ) );
    st.active = false;
}

static void PlaySongInternal( int songId, int streamId, bool loop, bool play )
{
    int ch = SongChBase + streamId;
    SongStream& st = streams[streamId];

    ndspChnWaveBufClear( ch );
    for ( int b = 0; b < NumWaveBufs; b++ )
        memset( &st.wavebufs[b], 0, sizeof( st.wavebufs[b] ) );

    blargg_err_t err = nsfEmu[streamId].start_track( songId );
    if ( err )
    {
        SndLog( "start_track FAILED id=%d stream=%d: %s\n", songId, streamId, err );
        st.active = false;
        return;
    }

    st.active          = true;
    st.loops           = loop;
    st.songId          = songId;
    st.samplesRendered = 0;
    st.endSample       = songs[songId].End >= 0
                       ? (uint32_t) songs[songId].End * SamplesPerFrame
                       : 0xFFFFFFFFu;

    // Prime just one buffer synchronously so sound starts immediately without
    // stalling the transition frame; Sound::Update()'s budget-capped refill
    // tops up the rest of the ring over the next couple of frames.
    RefillOneBuffer( st, streamId, 0 );

    ndspChnSetPaused( ch, !play );

    SndLog( "PlaySong id=%d stream=%d loop=%d play=%d endSample=%lu\n",
            songId, streamId, loop, play, (unsigned long) st.endSample );
}

static void UpdateSongs()
{
    if ( paused || !eventActive )
        return;

    LightLock_Lock( &songLock );

    // Event jingle finished (non-looping, queue drained) -> resume the ducked music.
    if ( !ndspChnIsPlaying( SongChBase + Sound::EventSongStream ) )
    {
        eventActive = false;
        StopStream( Sound::EventSongStream );
        if ( mainWasPlaying )
            for ( int i = 0; i < LoPriStreams; i++ )
                ndspChnSetPaused( SongChBase + i, false );
        SndLog( "Event song finished, resumed main=%d\n", mainWasPlaying );
    }

    LightLock_Unlock( &songLock );
}

void Sound::PlaySong( int songId, int streamId, bool loop )
{
    if ( !initialized ) return;
    if ( streamId < 0 || streamId >= LoPriStreams ) return;
    if ( songId < 0 || songId >= Songs ) return;
    LightLock_Lock( &songLock );
    PlaySongInternal( songId, streamId, loop, !eventActive );
    LightLock_Unlock( &songLock );
}

void Sound::PushSong( int songId )
{
    if ( !initialized ) return;
    if ( songId < 0 || songId >= Songs ) return;

    LightLock_Lock( &songLock );

    mainWasPlaying = false;
    for ( int i = 0; i < LoPriStreams; i++ )
    {
        int ch = SongChBase + i;
        if ( streams[i].active && ndspChnIsPlaying( ch ) && !ndspChnIsPaused( ch ) )
        {
            ndspChnSetPaused( ch, true );
            mainWasPlaying = true;
        }
    }

    PlaySongInternal( songId, EventSongStream, false, true );
    eventActive = true;

    LightLock_Unlock( &songLock );
}

void Sound::StopSongs()
{
    if ( !initialized ) return;
    LightLock_Lock( &songLock );
    eventActive = false;
    for ( int i = 0; i < Streams; i++ )
        StopStream( i );
    LightLock_Unlock( &songLock );
}


// ---- effects ---------------------------------------------------------------

static void UpdateEffects()
{
    for ( int i = 0; i < Instances; i++ )
    {
        int id = effectRequests[i].SoundId;
        effectRequests[i].SoundId = NoSound;
        if ( id == NoSound )
            continue;

        int ch = FxChBase + i;
        bool playing = ndspChnIsPlaying( ch );

        if ( fxSamples[id].pcm != nullptr
            && ( ( effects[id].Flags & SoundFlag_PlayIfQuietSlot ) == 0 || !playing ) )
        {
            // phase 11.3: same narrow single-effect gain cut as the desktop
            // side (Sound.cpp) -- the low-HP alarm still reads harsh at full
            // volume even throttled to a beep cadence. Must reset to full
            // mix for every other effect since channels are reused across
            // different sound IDs by slot.
            static float kFullMix[12] = { 1.0f, 1.0f };
            static float kSoftMix[12] = { 0.55f, 0.55f };
            ndspChnSetMix( ch, id == SEffect_low_hp ? kSoftMix : kFullMix );

            ndspChnWaveBufClear( ch );
            memset( &fxWaveBuf[i], 0, sizeof( fxWaveBuf[i] ) );
            fxWaveBuf[i].data_pcm16 = fxSamples[id].pcm;
            fxWaveBuf[i].nsamples   = fxSamples[id].nsamples;
            fxWaveBuf[i].looping    = effectRequests[i].Loop;
            ndspChnSetPaused( ch, false );
            ndspChnWaveBufAdd( ch, &fxWaveBuf[i] );
        }
    }
}

void Sound::PlayEffect( int id, bool loop, int instance )
{
    if ( !initialized ) return;
    if ( id < 0 || id >= Effects ) return;
    if ( instance < -1 || instance >= Instances ) return;

    int index = ( instance == -1 ) ? effects[id].Slot - 1 : instance;
    if ( index < 0 || index >= Instances ) return;

    int prevId = effectRequests[index].SoundId;
    if ( prevId == NoSound || effects[id].Priority < effects[prevId].Priority )
    {
        effectRequests[index].SoundId = id;
        effectRequests[index].Loop    = loop;
    }
}

void Sound::StopEffect( int instance )
{
    if ( !initialized ) return;
    if ( instance < 0 || instance >= Instances ) return;
    ndspChnWaveBufClear( FxChBase + instance );
    effectRequests[instance].SoundId = NoSound;
    effectRequests[instance].Loop    = false;
}

void Sound::StopEffects()
{
    for ( int i = 0; i < Instances; i++ )
        StopEffect( i );
}


// ---- init / teardown -------------------------------------------------------

// Load the whole (tiny, 21 KB) NSF into memory for the duration of this call
// only: Nsf_Emu::load_mem() copies the ROM data into its own owned buffer
// (Rom_Data), so the source bytes don't need to outlive the call.
static bool InitNsf()
{
    FILE* f = AssetStore_Open( "zelda.nsf", "rb" );
    if ( !f ) { SndLog( "zelda.nsf: open FAILED\n" ); return false; }
    fseek( f, 0, SEEK_END );
    long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    uint8_t* buf = (uint8_t*) malloc( size );
    bool ok = buf && fread( buf, 1, size, f ) == (size_t) size;
    fclose( f );
    if ( !ok ) { SndLog( "zelda.nsf: read FAILED (%ld bytes)\n", size ); free( buf ); return false; }

    for ( int i = 0; i < Streams; i++ )
    {
        nsfEmu[i].ignore_silence();
        nsfEmu[i].set_gain( LiveGain );                 // must precede set_sample_rate
        blargg_err_t err = nsfEmu[i].set_sample_rate( SampleRate );
        if ( !err ) err = nsfEmu[i].load_mem( buf, size );
        if ( err )
        {
            SndLog( "nsfEmu[%d] init FAILED: %s\n", i, err );
            free( buf );
            return false;
        }
    }
    free( buf );
    SndLog( "zelda.nsf loaded (%ld bytes), %d Nsf_Emu instances ready\n", size, Streams );
    return true;
}

bool Sound::Init()
{
    for ( int i = 0; i < Instances; i++ )
        effectRequests[i].SoundId = NoSound;

    remove( "sdmc:/3ds/zelda3ds/sound-debug.txt" );
    SndLog( "Sound::Init entered\n" );

    // Audio is ON by default. Safety valve: if sdmc:/zelda-no-audio.txt exists, skip
    // ndsp entirely and run silent -- so if audio ever hard-crashes on a
    // given console it can be recovered WITHOUT a rebuild, just by dropping
    // that file on the SD. Background on why this matters: as a CIA we grant
    // dsp::DSP, so ndspInit() spawns libctru's internal ndsp thread, which
    // once hard-crashed (ndspUpdateCapture -> ndspiReadChnState, luma
    // crash_dump_7, far=0x100f) on a much earlier, otherwise-broken build
    // (missing romfs, read-only VRAM mapping) -- likely a downstream symptom
    // of that mess rather than a real DSP-firmware problem, but if it does
    // turn out the console's DSP firmware isn't dumped, the "DSP1" homebrew
    // dumps it, and this no-audio file keeps the game playable meanwhile.
    {
        FILE* off = fopen( "sdmc:/zelda-no-audio.txt", "rb" );
        if ( off )
        {
            fclose( off );
            SndLog( "no-audio flag present; skipping ndsp, running silent\n" );
            return true;   // initialized stays false -> Update/StopAll are no-ops
        }
    }

    // Non-fatal: if the DSP firmware isn't available (e.g. not dumped on the
    // real console, or no dspfirm.cdc placeholder in the emulator) the game
    // still runs, just silent.
    Result rc = ndspInit();
    SndLog( "ndspInit rc=0x%08lX %s\n", (unsigned long) rc,
            R_FAILED( rc ) ? "FAILED" : "ok" );
    if ( R_FAILED( rc ) )
        return true;

    ndspSetOutputMode( NDSP_OUTPUT_STEREO );

    for ( int i = 0; i < Streams; i++ )
        ConfigChannel( SongChBase + i );
    for ( int i = 0; i < Instances; i++ )
        ConfigChannel( FxChBase + i );

    // Diagnostic: sdmc:/zelda-audio-test.txt -> loop a 440 Hz square on ch 0 and
    // nothing else. A continuous beep proves the ndsp output path end-to-end.
    {
        FILE* t = fopen( "sdmc:/zelda-audio-test.txt", "rb" );
        if ( t )
        {
            fclose( t );
            const int n = SampleRate / 10;
            static int16_t* tone = (int16_t*) linearAlloc( n * sizeof( int16_t ) );
            const int period = SampleRate / 440;
            for ( int i = 0; i < n; i++ )
                tone[i] = ( i % period ) < ( period / 2 ) ? 9000 : -9000;
            DSP_FlushDataCache( tone, n * sizeof( int16_t ) );
            static ndspWaveBuf wb;
            memset( &wb, 0, sizeof( wb ) );
            wb.data_pcm16 = tone; wb.nsamples = n; wb.looping = true;
            ndspChnSetPaused( SongChBase, false );
            ndspChnWaveBufAdd( SongChBase, &wb );
            SndLog( "TEST TONE active on ch%d\n", SongChBase );
            return true;   // leave initialized=false; game audio stays out of the way
        }
    }

    if ( !LoadList( "Songs.dat", songs, Songs ) )
        return true;
    if ( !LoadList( "Effects.dat", effects, Effects ) )
        return true;
    if ( !InitNsf() )
        return true;

    // Preload every effect sample (all small) into linear memory.
    for ( int i = 0; i < Effects; i++ )
        fxSamples[i].nsamples = LoadWav( effects[i].Filename, &fxSamples[i].pcm );

    // Song stream chunk buffers (small, linear; freed at Uninit).
    for ( int s = 0; s < Streams; s++ )
        for ( int b = 0; b < NumWaveBufs; b++ )
            streams[s].chunks[b] = (int16_t*) linearAlloc( ChunkSamples * sizeof( int16_t ) );

    SndLog( "ready: %d songs, %d effects (fx0=%lu samp)\n",
            Songs, Effects, (unsigned long) fxSamples[0].nsamples );

    LightLock_Init( &songLock );
    audioThreadRunning = true;
    s32 mainPrio = 0x30;
    svcGetThreadPriority( &mainPrio, CUR_THREAD_HANDLE );
    // Higher priority than main (lower number = higher priority on 3DS), not
    // lower: this thread shares a core with main, and a same-or-lower-priority
    // thread can be starved outright while main is CPU-bound (room/area
    // transitions do a bursty synchronous load) rather than just time-sliced.
    // Its own CPU usage is tiny (a handful of ms every ~5 ms poll, then it
    // sleeps), so preempting main briefly is cheap insurance against the ndsp
    // ring starving audibly during those bursts.
    s32 audioPrio = mainPrio > 0x18 ? mainPrio - 1 : mainPrio;
    audioThread = threadCreate( AudioThreadFunc, nullptr, AudioThreadStackSize,
                                 audioPrio, -1, false );
    SndLog( "audio thread %s (mainPrio=%d audioPrio=%d)\n",
            audioThread ? "created" : "FAILED (falling back to silent refill)",
            (int) mainPrio, (int) audioPrio );

    initialized = true;
    return true;
}

void Sound::Uninit()
{
    if ( !initialized )
        return;

    if ( audioThread )
    {
        audioThreadRunning = false;
        threadJoin( audioThread, U64_MAX );
        threadFree( audioThread );
        audioThread = nullptr;
    }

    for ( int s = 0; s < Streams; s++ )
    {
        StopStream( s );
        for ( int b = 0; b < NumWaveBufs; b++ )
            if ( streams[s].chunks[b] ) { linearFree( streams[s].chunks[b] ); streams[s].chunks[b] = nullptr; }
    }
    for ( int i = 0; i < Effects; i++ )
        if ( fxSamples[i].pcm ) { linearFree( fxSamples[i].pcm ); fxSamples[i].pcm = nullptr; }

    ndspExit();
    initialized = false;
}


// ---- global ----------------------------------------------------------------

void Sound::Update()
{
    if ( !initialized ) return;
    UpdateSongs();
    UpdateEffects();
}

void Sound::StopAll()
{
    StopSongs();
    StopEffects();
}

void Sound::Pause()
{
    if ( !initialized ) return;
    paused = true;
    for ( int i = 0; i < Streams; i++ )   ndspChnSetPaused( SongChBase + i, true );
    for ( int i = 0; i < Instances; i++ ) ndspChnSetPaused( FxChBase + i, true );
}

void Sound::Unpause()
{
    if ( !initialized ) return;
    paused = false;
    for ( int i = 0; i < Streams; i++ )
    {
        if ( eventActive && i < LoPriStreams && mainWasPlaying )
            continue;   // stays ducked until the jingle ends
        ndspChnSetPaused( SongChBase + i, false );
    }
    for ( int i = 0; i < Instances; i++ ) ndspChnSetPaused( FxChBase + i, false );
}

bool Sound::IsPaused()
{
    return paused;
}

#endif // __3DS__

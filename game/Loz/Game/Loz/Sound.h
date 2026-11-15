/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once


class Sound
{
public:
    enum
    {
        MainSongStream,
        EventSongStream,
    };

    enum
    {
        AmbientInstance = 4,
    };

public:
    static bool Init();
    static void Uninit();

    static void Update();

    static void PlaySong( int id, int stream, bool loop );
    static void PushSong( int id );
    static void StopSongs();

    static void PlayEffect( int id, bool loop = false, int instance = -1 );
    static void StopEffect( int instance );
    static void StopEffects();

    static void StopAll();
    static void Pause();
    static void Unpause();
    // True between a Pause() and the matching Unpause(). Lets a caller that
    // pauses conditionally (3DS sleep/suspend hook, phase 9.1) tell whether
    // audio was already paused for another reason (the player's own in-game
    // Select-pause) before deciding whether it's the one that should
    // Unpause() again on resume.
    static bool IsPaused();
};

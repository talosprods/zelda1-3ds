/*
   Global, cross-slot game options (phase 11.1/11.2). See Options.h.
*/
#include "Common.h"
#include "Options.h"
#include "SaveFolder.h"

static GameOptions s_options = { 0 };
static bool        s_loaded = false;

void Options::Init()
{
    if ( s_loaded )
        return;
    s_loaded = true;

    if ( !SaveFolder::ReadOptions( s_options ) )
        s_options = GameOptions{ 0 };
}

GameOptions& Options::Get()
{
    return s_options;
}

void Options::Save()
{
    SaveFolder::WriteOptions( s_options );
}

/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#include "Common.h"
#include "GameMenu.h"
#include "Graphics.h"
#include "Input.h"
#include "ItemObj.h"
#include "Profile.h"
#include "SaveFolder.h"
#include "Sound.h"
#include "SoundId.h"
#include "World.h"


const static uint8_t SelectStr[] = 
{ 0x62, 0x24, 0x1C, 0x24, 0x0E, 0x24, 0x15, 0x24, 0x0E, 0x24, 0x0C, 0x24, 0x1D, 0x24, 0x62 };

const static uint8_t NameStr[] = 
{ 0x24, 0x17, 0x0A, 0x16, 0x0E, 0x24 };

const static uint8_t LifeStr[] = 
{ 0x24, 0x15, 0x12, 0x0F, 0x0E, 0x24 };

const static uint8_t EliminateStr[] =
{ 0x0E, 0x15, 0x12, 0x16, 0x12, 0x17, 0x0A, 0x1D, 0x12, 0x18, 0x17, 0x24, 0x16, 0x18, 0x0D, 0x0E };


GameMenu::GameMenu( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
    :   summaries( summaries ),
        selectedIndex( -1 )
{
    static const uint8_t palettes[PaletteCount][4] = 
    {
        { 0x0F, 0x30, 0x00, 0x12 },
        { 0x0F, 0x16, 0x27, 0x36 },
        { 0x0F, 0x0C, 0x1C, 0x2C },
        { 0x0F, 0x12, 0x1C, 0x2C },
        { 0x00, 0x29, 0x27, 0x07 },
        { 0x00, 0x29, 0x27, 0x07 },
        { 0x00, 0x29, 0x27, 0x07 },
        { 0x00, 0x15, 0x27, 0x30 }
    };

    for ( int i = 0; i < PaletteCount; i++ )
    {
        Graphics::SetPaletteIndexed( i, palettes[i] );
    }

    // So that characters are fully opaque.
    Graphics::SetColor( 0, 0, 0xFF000000 );
    Graphics::UpdatePalettes();

    SelectNext();
}

void GameMenu::SelectNext()
{
    // No name typing, just slot 1/2/3 you can pick directly -- the
    // cursor used to skip inactive (empty) slots entirely, forcing you
    // through the separate "REGISTER YOUR NAME" row (RegisterMenu's
    // character-grid typing screen) to create one. Now every slot is
    // directly selectable regardless of active state; Start (below) either
    // continues an active one or creates a blank one on the spot. That
    // makes the old REGISTER row redundant, so the cycle is just the 3
    // slots + ELIMINATION MODE (index 3, was 4).
    selectedIndex++;
    if ( selectedIndex >= 4 )
        selectedIndex = 0;
}

void StartWorld( int fileIndex )
{
    Profile profile;
    SaveFolder::ReadProfile( fileIndex, profile );
    SaveFolder::WriteLastSlot( fileIndex );
    World::Start( fileIndex, profile );
}

void GameMenu::Update()
{
    if ( Input::IsButtonPressing( InputButtons::Select ) )
    {
        SelectNext();
        Sound::PlayEffect( SEffect_cursor );
    }
    else if ( Input::IsButtonPressing( InputButtons::Start ) )
    {
        if ( selectedIndex < 3 )
        {
            if ( summaries->Summaries[selectedIndex].IsActive() )
                StartWorld( selectedIndex );
            else
            {
                // Hold Select while starting a new game for quest 2 -- see
                // World::StartNewGameInSlot()'s comment for why this exists
                // (replaces RegisterMenu's old "name it ZELDA" trick, now
                // unreachable since this screen no longer opens that typing
                // screen at all).
                int quest = Input::IsButtonDown( InputButtons::Select ) ? 1 : 0;
                World::StartNewGameInSlot( selectedIndex, quest );
            }
        }
        else // selectedIndex == 3
            World::EliminateFile( summaries );
    }
}

void GameMenu::Draw()
{
    Graphics::Begin();

    al_clear_to_color( al_map_rgb( 0, 0, 0 ) );

    DrawBox( 0x18, 0x40, 0xD0, 0x90 );

    DrawString( SelectStr, sizeof SelectStr, 0x40, 0x28, 0 );
    DrawString( NameStr, sizeof NameStr, 0x50, 0x40, 0 );
    DrawString( LifeStr, sizeof LifeStr, 0x98, 0x40, 0 );
    DrawString( EliminateStr, sizeof EliminateStr, 0x30, 0xA8, 0 );

    int y = 0x58;
    for ( int i = 0; i < 3; i++ )
    {
        ProfileSummary& summary = summaries->Summaries[i];
        if ( summary.IsActive() )
        {
            uint8_t numBuf[3] = "";
            NumberToStringR( summary.Deaths, NumberSign_None, numBuf, sizeof numBuf );
            DrawString( numBuf, sizeof numBuf, 0x48, y + 8, 0 );
            DrawString( summary.Name, summary.NameLength, 0x48, y, 0 );
            uint totalHearts = summary.HeartContainers;
            uint heartsValue = Profile::GetMaxHeartsValue( totalHearts );
            DrawHearts( heartsValue, totalHearts, 0x90, y + 8 );
            DrawFileIcon( 0x30, y, summary.Quest );
        }
        DrawChar( Char_Minus, 0x88, y, 0 );
        y += 24;
    }

    if ( selectedIndex < 3 )
        y = 0x58 + selectedIndex * 24 + 5;
    else
        y = 0xA8;   // ELIMINATION MODE, the only row left past the 3 slots
    DrawChar( Char_FullHeart, 0x28, y, 7 );

    Graphics::End();
}

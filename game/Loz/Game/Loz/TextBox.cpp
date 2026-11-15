/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#include "Common.h"
#include "TextBox.h"
#include "ItemObj.h"
#include "Sound.h"
#include "SoundId.h"
#include "Input.h"
#include "Options.h"


TextBox::TextBox( const uint8_t* str, int delay )
    :   left( StartX ),
        top( StartY ),
        height( 8 ),
        charDelay( delay ),
        charTimer( 0 ),
        drawingDialog( true ),
        startCharPtr( str ),
        curCharPtr( str )
{
}

void TextBox::Reset( const uint8_t* str )
{
    drawingDialog = true;
    charTimer = 0;
    startCharPtr = str;
    curCharPtr = startCharPtr;
}

bool TextBox::IsDone()
{
    return !drawingDialog;
}

int TextBox::GetHeight()
{
    return height;
}

int TextBox::GetX()
{
    return left;
}

int TextBox::GetY()
{
    return top;
}

void TextBox::SetX( int x )
{
    left = x;
}

void TextBox::SetY( int y )
{
    top = y;
}

int TextBox::EffectiveDelay() const
{
    // Phase 11.2: the "Fast" text-speed option halves the per-character
    // delay rather than replacing it with a fixed value, so it scales
    // proportionally for every caller (the default 6-frame dialogue and the
    // 8-frame textBox2 in World.cpp alike) without needing to touch each
    // call site.
    if ( Options::Get().TextSpeed != 0 )
    {
        int fast = charDelay / 2;
        return (fast < 1) ? 1 : fast;
    }
    return charDelay;
}

void TextBox::Update()
{
    if ( !drawingDialog )
        return;

    // Phase 11.2: holding A fast-forwards through the rest of this text box
    // in a single frame instead of waiting out the letter-by-letter reveal.
    // Skips the per-character blip sound while doing so (dozens of chars in
    // one frame would otherwise machine-gun the same sound effect).
    bool revealAll = Input::IsButtonDown( InputButtons::A );

    do
    {
        if ( charTimer == 0 || revealAll )
        {
            uint8_t attr;
            uint8_t ch;

            do
            {
                ch   = *curCharPtr & 0x3F;
                attr = *curCharPtr & 0xC0;
                if ( attr == 0xC0 )
                    drawingDialog = false;
                else if ( attr != 0 )
                    height += 8;

                curCharPtr++;
                if ( ch != Char_JustSpace && !revealAll )
                    Sound::PlayEffect( SEffect_character );
            } while ( drawingDialog && ch == Char_JustSpace );
            charTimer = revealAll ? 0 : (EffectiveDelay() - 1);
        }
        else
        {
            charTimer--;
        }
    } while ( revealAll && drawingDialog );
}

void TextBox::Draw()
{
    int x = left;
    int y = top;

    for ( const uint8_t* charPtr = startCharPtr; charPtr != curCharPtr; charPtr++ )
    {
        uint8_t attr = *charPtr & 0xC0;
        uint8_t ch   = *charPtr & 0x3F;

        if ( ch != Char_JustSpace )
            DrawChar( ch, x, y, 0 );

        if ( attr == 0 )
        {
            x += 8;
        }
        else
        {
            x = StartX;
            y += 8;
        }
    }
}

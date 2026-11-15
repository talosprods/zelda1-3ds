/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once


enum ButtonState
{
    ButtonState_Lifted     = 0,
    ButtonState_Pressing   = 1,
    ButtonState_Releasing  = 2,
    ButtonState_Held       = 3,
};


struct InputButtons
{
    enum Button
    {
        None    = 0,
        A       = 0x80,
        B       = 0x40,
        Select  = 0x20,
        Start   = 0x10,
        Up      = 8,
        Down    = 4,
        Left    = 2,
        Right   = 1,
        Y       = 0x100,    // second item-use button (3DS only, phase 8.1); never
                             // set on desktop, so existing behavior is unaffected
        X       = 0x200,    // bottom-screen Map/Items view toggle (3DS only,
                             // phase 9.x); never set on desktop, unused by any
                             // shared game code
        Zoom    = 0x400,    // New 3DS ZL/ZR: alternate trigger for the bottom-
                             // screen map detail zoom (phase 13.5), alongside
                             // Select. Dedicated bit rather than reusing
                             // Select's own bit, since Select already has
                             // distinct meaning elsewhere (boot slot-select
                             // hold, continue-screen cursor) that this must
                             // not also fire. Never set on desktop or on an
                             // Old 3DS (no ZL/ZR hardware -- libctru simply
                             // never sets those key bits there).
    };

    uint16_t    Buttons;

    InputButtons( uint value );
    bool Has( uint value ) const;
    void Mask( uint value );
    void Clear( uint value );
};


class Input
{
public:
    static void Init();

    static InputButtons GetButtons();

    static bool IsKeyDown( int keyCode );
    static bool IsKeyPressing( int keyCode );
    static ButtonState GetKey( int keyCode );

    static bool IsButtonDown( int buttonCode );
    static bool IsButtonPressing( int buttonCode );
    static ButtonState GetButton( int buttonCode );
    static Direction GetInputDirection();

    // Bottom-screen touch (3DS only; phase 9.x legend popup). Edge-triggered —
    // true only on the frame the touch began, with the raw touch-screen pixel
    // position (0..320, 0..240) written to x/y. Always returns false on
    // desktop (no touchscreen there), so shared code that calls this behaves
    // identically to "never tapped" off-3DS.
    static bool GetTouchPressed( int& x, int& y );

    static void Update();
};

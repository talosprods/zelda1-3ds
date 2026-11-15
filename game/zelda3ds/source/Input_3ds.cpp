#ifdef __3DS__
/*
   3DS input backend (libctru). Maps the console's buttons onto Loz's NES-style
   InputButtons and reproduces the desktop edge/held semantics.
*/
#include <3ds.h>
#include "Common.h"
#include "Input.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static InputButtons oldInputState( 0 );
static InputButtons inputState( 0 );

// Bottom-screen touch, edge-triggered (phase 9.x legend popup). Captured once
// per Update() from libctru; GetTouchPressed() just reports what was read
// this frame, matching the InputButtons "Pressing" edge semantics.
static bool touchPressedThisFrame = false;
static int  touchX = 0, touchY = 0;

//---------------------------------------------------------------------------
// Scripted input for automated testing: reads romfs:/input.txt if present.
// Each line: "<frames> [button ...]"  buttons: Up Down Left Right A B Y X Start Select ZL ZR
//---------------------------------------------------------------------------
namespace {
struct ScriptStep { uint16_t buttons; int frames; };
std::vector<ScriptStep> scriptSteps;
size_t scriptIndex = 0;
int scriptRemaining = 0;
bool scriptLoaded = false;
unsigned long scriptSum = 0;   // checksum of last-loaded script (hot reload)
uint32_t lastReloadCheck = 0;

uint16_t ParseButton( const char* t )
{
    if ( strcasecmp( t, "up" ) == 0 )     return InputButtons::Up;
    if ( strcasecmp( t, "down" ) == 0 )   return InputButtons::Down;
    if ( strcasecmp( t, "left" ) == 0 )   return InputButtons::Left;
    if ( strcasecmp( t, "right" ) == 0 )  return InputButtons::Right;
    if ( strcasecmp( t, "a" ) == 0 )      return InputButtons::A;
    if ( strcasecmp( t, "b" ) == 0 )      return InputButtons::B;
    if ( strcasecmp( t, "y" ) == 0 )      return InputButtons::Y;
    if ( strcasecmp( t, "x" ) == 0 )      return InputButtons::X;
    if ( strcasecmp( t, "start" ) == 0 )  return InputButtons::Start;
    if ( strcasecmp( t, "select" ) == 0 ) return InputButtons::Select;
    if ( strcasecmp( t, "zl" ) == 0 )     return InputButtons::Zoom;
    if ( strcasecmp( t, "zr" ) == 0 )     return InputButtons::Zoom;
    return 0;
}

// Test driver only: lives on the SD card, so release builds/hardware without
// the file get normal input. Hot-reloadable: once the current script has run
// out, a changed file is picked up and played from the top (lets an external
// tool feed moves incrementally: observe screen -> write next move).
void LoadScript()
{
    scriptLoaded = true;

    FILE* f = fopen( "sdmc:/zelda3ds-input.txt", "r" );
    if ( f == nullptr ) return;

    unsigned long sum = 0;
    std::vector<ScriptStep> steps;
    char line[256];
    while ( fgets( line, sizeof line, f ) )
    {
        for ( const char* p = line; *p; p++ ) sum = sum * 31 + (unsigned char) *p;
        char* hash = strchr( line, '#' ); if ( hash ) *hash = 0;
        char* tok = strtok( line, " \t\r\n" );
        if ( !tok ) continue;
        int frames = atoi( tok );
        if ( frames <= 0 ) continue;
        uint16_t b = 0;
        while ( (tok = strtok( nullptr, " \t\r\n" )) != nullptr ) b |= ParseButton( tok );
        ScriptStep s = { b, frames };
        steps.push_back( s );
    }
    fclose( f );

    if ( sum == scriptSum )
        return;                    // unchanged -> keep finished state

    scriptSum = sum;
    scriptSteps.swap( steps );
    scriptIndex = 0;
    scriptRemaining = scriptSteps.empty() ? 0 : scriptSteps[0].frames;
}

uint16_t ScriptButtons()
{
    if ( !scriptLoaded ) LoadScript();

    if ( scriptIndex >= scriptSteps.size() )
    {
        // Exhausted: poll for a changed script every ~1.5s.
        uint32_t now = GetFrameCounter();
        if ( now - lastReloadCheck > 90 )
        {
            lastReloadCheck = now;
            LoadScript();
        }
        if ( scriptIndex >= scriptSteps.size() )
            return 0;
    }

    uint16_t b = scriptSteps[scriptIndex].buttons;
    if ( --scriptRemaining <= 0 )
    {
        if ( ++scriptIndex < scriptSteps.size() )
            scriptRemaining = scriptSteps[scriptIndex].frames;
    }
    return b;
}
} // namespace

InputButtons::InputButtons( uint value )
    :   Buttons( value )
{
}

bool InputButtons::Has( uint value ) const   { return (Buttons & value) != 0; }
void InputButtons::Mask( uint value )         { Buttons &= value; }
void InputButtons::Clear( uint value )        { Buttons = Buttons ^ value; }

InputButtons Input::GetButtons()
{
    uint buttons = (oldInputState.Buttons ^ inputState.Buttons)
        & inputState.Buttons
        | (inputState.Buttons & 0xF);
    return (InputButtons) buttons;
}

// No physical keyboard on the 3DS; the desktop debug hotkeys simply never fire.
bool Input::IsKeyDown( int )        { return false; }
bool Input::IsKeyPressing( int )    { return false; }
ButtonState Input::GetKey( int )    { return ButtonState_Lifted; }

bool Input::IsButtonDown( int buttonCode )     { return inputState.Buttons & buttonCode; }
bool Input::IsButtonPressing( int buttonCode ) { return GetButton( buttonCode ) == ButtonState_Pressing; }

ButtonState Input::GetButton( int buttonCode )
{
    int isDown  = (inputState.Buttons & buttonCode) ? 1 : 0;
    int wasDown = (oldInputState.Buttons & buttonCode) ? 1 : 0;
    return (ButtonState) ((wasDown << 1) | isDown);
}

Direction Input::GetInputDirection()
{
    if ( IsButtonDown( InputButtons::Left ) )  return Dir_Left;
    if ( IsButtonDown( InputButtons::Right ) ) return Dir_Right;
    if ( IsButtonDown( InputButtons::Up ) )    return Dir_Up;
    if ( IsButtonDown( InputButtons::Down ) )  return Dir_Down;
    return Dir_None;
}

bool Input::GetTouchPressed( int& x, int& y )
{
    if ( !touchPressedThisFrame )
        return false;
    x = touchX;
    y = touchY;
    return true;
}

void Input::Init()
{
}

// Scripted touch (added phase 11.1, verifying the new Options tab): the
// zelda3ds-input.txt button-script format has no touch support (§4/§8,
// DEVELOPMENT.md), so touch-only UI was previously only testable via real
// touch or a one-off hook built and torn down each time. This one's kept as
// a standing test channel instead. If sdmc:/zelda3ds-debug-touch.txt exists
// ("<frame> <x> <y>"), synthesizes one touch-press at that frame/position.
// Test-only like the other sdmc:/zelda3ds-*.txt channels -- absent on a
// normal SD card, so releases are unaffected.
//
// Hot-reloadable (phase save-slots UI testing): a multi-step touch flow (tap
// a button -> screenshot -> tap the next thing) used to need one full Azahar
// relaunch per touch, since the file was only ever read once per process.
// Same checksum-based reload ScriptButtons() above already uses for the
// button-script channel -- rewriting the file with a new "<frame> <x> <y>"
// mid-run now gets picked up and fires again, letting one running process
// serve a whole "observe -> write next move" sequence like the button
// channel already does.
static void CheckDebugTouch( bool& pressed, int& tx, int& ty )
{
    static unsigned long lastSum = 0;
    static bool     active = false;
    static bool     fired = false;
    static int      targetFrame = -1, targetX = 0, targetY = 0;
    static int      counter = 0;
    static uint32_t lastReloadCheck = 0;

    uint32_t now = GetFrameCounter();
    if ( !active || fired )
    {
        if ( now - lastReloadCheck > 30 )   // cheap single-line read, poll often
        {
            lastReloadCheck = now;
            FILE* f = fopen( "sdmc:/zelda3ds-debug-touch.txt", "r" );
            if ( f != nullptr )
            {
                int fFrame = -1, fx = 0, fy = 0;
                bool ok = fscanf( f, "%d %d %d", &fFrame, &fx, &fy ) == 3;
                fclose( f );
                unsigned long sum = (unsigned long) fFrame * 1000003UL
                                   + (unsigned long) fx * 1009UL + (unsigned long) fy;
                if ( ok && sum != lastSum )
                {
                    lastSum = sum;
                    targetFrame = fFrame;
                    targetX = fx;
                    targetY = fy;
                    counter = 0;
                    active = true;
                    fired = false;
                }
            }
        }
    }
    if ( active && !fired )
    {
        counter++;
        if ( counter == targetFrame )
        {
            pressed = true;
            tx = targetX;
            ty = targetY;
            fired = true;
        }
    }
}

void Input::Update()
{
    oldInputState = inputState;

    hidScanInput();
    u32 held = hidKeysHeld();

    touchPressedThisFrame = (hidKeysDown() & KEY_TOUCH) != 0;
    if ( touchPressedThisFrame )
    {
        touchPosition pos;
        hidTouchRead( &pos );
        touchX = pos.px;
        touchY = pos.py;
    }
    CheckDebugTouch( touchPressedThisFrame, touchX, touchY );

    uint b = 0;
    if ( held & (KEY_DUP    | KEY_CPAD_UP)    ) b |= InputButtons::Up;
    if ( held & (KEY_DDOWN  | KEY_CPAD_DOWN)  ) b |= InputButtons::Down;
    if ( held & (KEY_DLEFT  | KEY_CPAD_LEFT)  ) b |= InputButtons::Left;
    if ( held & (KEY_DRIGHT | KEY_CPAD_RIGHT) ) b |= InputButtons::Right;
    if ( held & KEY_A )      b |= InputButtons::A;       // sword
    if ( held & KEY_B )      b |= InputButtons::B;       // item slot 1
    if ( held & KEY_Y )      b |= InputButtons::Y;       // item slot 2 (phase 8.1)
    if ( held & KEY_X )      b |= InputButtons::X;       // bottom-screen Map/Items toggle
    if ( held & KEY_START )  b |= InputButtons::Start;   // inventory
    if ( held & KEY_SELECT ) b |= InputButtons::Select;
    if ( held & (KEY_ZL | KEY_ZR) ) b |= InputButtons::Zoom;   // New 3DS map-zoom shortcut (phase 13.5); reads as 0 on Old 3DS, no special-case needed

    b |= ScriptButtons();   // autonomous test driver (romfs:/input.txt)

    inputState = (InputButtons) b;
}

#endif // __3DS__

#pragma once
// Frame/video plumbing lives in Graphics_3ds.cpp (which owns citro2d). main_3ds
// stays free of citro2d/citro3d so it can include the game headers without the
// libctru frd.h `Profile` typedef clashing with Loz's struct Profile.
void Video_Init();
void Video_Fini();
void Video_BeginFrame();
// phase 6.1: blits the top screen's offscreen 2x buffer (populated by
// World::Draw(), between BeginFrame/EndFrame) onto the real top screen with
// bilinear filtering + 8:7 PAR correction. Call after World::Draw(), before
// EndFrame — see Graphics_3ds.cpp's s_topOffscreen comment. On a frame where
// World::Draw() drew nothing (an engine mode's blank first frame), this
// leaves the top screen untouched so the previous frame persists — see the
// s_topContentDrawn comment in the .cpp (phase 7.5a resolution).
void Video_BlitTopScreen();
void Video_EndFrame();

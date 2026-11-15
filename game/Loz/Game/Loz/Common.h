/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once

// Platform graphics/system layer.
#ifdef __3DS__
// On the 3DS there is no Allegro; a small compat header provides the handful of
// ALLEGRO_* types and al_ helpers that the shared game code still references.
#include "platform_3ds_compat.h"
#else
#include <allegro5/allegro5.h>
#ifdef _WIN32
#include <allegro5/allegro_windows.h>
#endif
#endif

// C
#ifdef _WIN32
#include <crtdbg.h>
#else
// MSVC debug-CRT assertion macros: no-ops off Windows.
#ifndef _ASSERT
#define _ASSERT(expr)  ((void)0)
#endif
#ifndef _ASSERTE
#define _ASSERTE(expr) ((void)0)
#endif
#endif
#include <stdio.h>

// C++
#include <cmath>
#include <memory>
#include <array>

// This project
#include "Global.h"
#include "Util.h"

#undef GetObject

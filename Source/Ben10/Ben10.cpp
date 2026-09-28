// Alien Museum - main game module.
//
// The OpenXR layer that keeps frame synthesis off lives in the Ben10XR module, which loads earlier.

#include "Ben10.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Ben10, "Ben10");

DEFINE_LOG_CATEGORY(LogAlienMuseum);

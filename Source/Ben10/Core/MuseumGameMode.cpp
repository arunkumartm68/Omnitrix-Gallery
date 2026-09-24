// Alien Museum - game mode for the mixed reality museum level.

#include "Core/MuseumGameMode.h"
#include "Interaction/MuseumPawn.h"

AMuseumGameMode::AMuseumGameMode()
{
	DefaultPawnClass = AMuseumPawn::StaticClass();
}

// Alien Museum - interface for anything the hand/controller pointer can hover and select.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MuseumInteractable.generated.h"

class AMuseumPawn;
class UPrimitiveComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMuseumInteractable : public UInterface
{
	GENERATED_BODY()
};

class BEN10_API IMuseumInteractable
{
	GENERATED_BODY()

public:
	/** Pointer ray started or stopped hovering HitComponent on this actor. */
	virtual void OnPointerHover(UPrimitiveComponent* HitComponent, bool bHovered) {}

	/** Trigger / pinch pressed while pointing at HitComponent. Return true if the press was used. */
	virtual bool OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn) { return false; }
};

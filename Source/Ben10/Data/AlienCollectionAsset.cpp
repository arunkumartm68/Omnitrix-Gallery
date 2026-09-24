// Alien Museum - the list of aliens shown in the Alien Collection panel.

#include "Data/AlienCollectionAsset.h"
#include "Data/AlienDataAsset.h"

UAlienDataAsset* UAlienCollectionAsset::FindById(FName AlienId) const
{
	for (UAlienDataAsset* Alien : Aliens)
	{
		if (Alien && Alien->AlienId == AlienId)
		{
			return Alien;
		}
	}
	return nullptr;
}

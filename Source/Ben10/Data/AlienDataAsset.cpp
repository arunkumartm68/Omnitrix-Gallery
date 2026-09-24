// Alien Museum - data describing one alien species.

#include "Data/AlienDataAsset.h"

FPrimaryAssetId UAlienDataAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("AlienData"), GetFName());
}

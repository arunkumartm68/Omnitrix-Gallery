// Alien Museum - central list of default assets used by the C++ placeholder visuals.
//
// Every path here lives in /Engine/BasicShapes or /Game/AlienMuseum. Both folders are listed in
// DirectoriesToAlwaysCook (Config/DefaultGame.ini) so these string references survive packaging.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

namespace MuseumAssets
{
	BEN10_API UStaticMesh* SphereMesh();
	BEN10_API UStaticMesh* CylinderMesh();
	BEN10_API UStaticMesh* ConeMesh();
	BEN10_API UStaticMesh* CubeMesh();
	BEN10_API UStaticMesh* PlaneMesh();

	BEN10_API UMaterialInterface* GlassMaterial();
	BEN10_API UMaterialInterface* MetalMaterial();
	BEN10_API UMaterialInterface* EmissiveMaterial();
	BEN10_API UMaterialInterface* HologramMaterial();
	BEN10_API UMaterialInterface* OccluderMaterial();
	BEN10_API UMaterialInterface* AlienSkinMaterial();
	BEN10_API UMaterialInterface* BlobShadowMaterial();
	BEN10_API UMaterialInterface* AlienTranslucentMaterial();

	/** Material parameter names shared by the /Game/AlienMuseum/Materials assets. */
	namespace Params
	{
		inline const FName Color(TEXT("Color"));
		inline const FName Intensity(TEXT("Intensity"));
		inline const FName Opacity(TEXT("Opacity"));
		inline const FName RimColor(TEXT("RimColor"));
		inline const FName SelfIllum(TEXT("SelfIllum"));
		inline const FName Tint(TEXT("Tint"));
		inline const FName EdgeColor(TEXT("EdgeColor"));
	}
}

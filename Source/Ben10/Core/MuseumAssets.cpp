// Alien Museum - central list of default assets used by the C++ placeholder visuals.

#include "Core/MuseumAssets.h"
#include "Ben10.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
	template <typename T>
	T* LoadMuseumAsset(const TCHAR* Path)
	{
		T* Asset = LoadObject<T>(nullptr, Path);
		if (!Asset)
		{
			UE_LOG(LogAlienMuseum, Warning, TEXT("Missing museum asset: %s"), Path);
		}
		return Asset;
	}
}

UStaticMesh* MuseumAssets::SphereMesh() { return LoadMuseumAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere")); }
UStaticMesh* MuseumAssets::CylinderMesh() { return LoadMuseumAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }
UStaticMesh* MuseumAssets::ConeMesh() { return LoadMuseumAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone.Cone")); }
UStaticMesh* MuseumAssets::CubeMesh() { return LoadMuseumAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube")); }
UStaticMesh* MuseumAssets::PlaneMesh() { return LoadMuseumAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane")); }

UMaterialInterface* MuseumAssets::GlassMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_MuseumGlass.M_MuseumGlass")); }
UMaterialInterface* MuseumAssets::MetalMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_MuseumMetal.M_MuseumMetal")); }
UMaterialInterface* MuseumAssets::EmissiveMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_MuseumEmissive.M_MuseumEmissive")); }
UMaterialInterface* MuseumAssets::HologramMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_MuseumHologram.M_MuseumHologram")); }
UMaterialInterface* MuseumAssets::OccluderMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_MuseumOccluder.M_MuseumOccluder")); }
UMaterialInterface* MuseumAssets::AlienSkinMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_AlienSkin.M_AlienSkin")); }
UMaterialInterface* MuseumAssets::BlobShadowMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_BlobShadow.M_BlobShadow")); }
UMaterialInterface* MuseumAssets::AlienTranslucentMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_AlienTranslucent.M_AlienTranslucent")); }

UMaterialInterface* MuseumAssets::FXGlowMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_FXGlow.M_FXGlow")); }
UMaterialInterface* MuseumAssets::EnvLitMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_EnvLit.M_EnvLit")); }
UMaterialInterface* MuseumAssets::EnvLavaMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_EnvLava.M_EnvLava")); }
UMaterialInterface* MuseumAssets::EnvWaterMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_EnvWater.M_EnvWater")); }
UMaterialInterface* MuseumAssets::EnvMistMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_EnvMist.M_EnvMist")); }
UMaterialInterface* MuseumAssets::FXRingMaterial() { return LoadMuseumAsset<UMaterialInterface>(TEXT("/Game/AlienMuseum/Materials/M_FXRing.M_FXRing")); }

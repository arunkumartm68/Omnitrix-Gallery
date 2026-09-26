// Alien Museum - builds and animates the alien body from an AlienDataAsset.
//
// The body is either the data asset's imported model (ModelMesh: one static mesh, scaled to the
// alien's height and stood on its feet) or made of engine basic shapes (spheres, cylinders, cones,
// cubes): one of the built-in shapes (Blob / Tall / Squat) or fully described by the Parts list
// (BodyShape = Custom). Animation is procedural (bob, squash & stretch, walk waddle / swing, head
// look-at, blink, wing flaps, flame flicker), which is far cheaper on Quest than skeletal meshes +
// animation blueprints.
// A Blueprint subclass of AAlienCharacter with a skeletal mesh also works; this component then stays empty.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Data/AlienDataAsset.h"
#include "AlienAppearanceComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS(ClassGroup = (AlienMuseum), meta = (BlueprintSpawnableComponent))
class BEN10_API UAlienAppearanceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UAlienAppearanceComponent();

	/** (Re)builds the body parts. The component origin is the alien's feet. */
	UFUNCTION(BlueprintCallable, Category = "Alien")
	void BuildAppearance(const UAlienDataAsset* Data);

	/** Removes all generated parts. */
	UFUNCTION(BlueprintCallable, Category = "Alien")
	void ClearAppearance();

	/**
	 * Advances the procedural animation. Call once per frame from the owner.
	 * @param SpeedAlpha  0 = standing, 1 = walking at full speed.
	 * @param LookTarget  Optional world-space point the head should look at.
	 * @param bExcited    Faster, bigger motion (reacting to the player).
	 */
	void UpdateAnimation(float DeltaSeconds, float SpeedAlpha, const FVector* LookTarget, bool bExcited);

	/** Starts the "materialize" pop-in (scale from nothing with a little overshoot). */
	void PlayMaterialize(float Duration = 0.6f);

	/** Advances the materialize effect. Returns true while it is still playing. */
	bool UpdateMaterialize(float DeltaSeconds);

	/** Height of the model at scale 1 (cm). */
	float GetModelHeight() const { return ModelHeight; }

	/** Approximate horizontal radius of the model at scale 1 (cm). */
	float GetModelRadius() const { return ModelRadius; }

	/** Radius for the collision capsule at scale 1 (cm). */
	float GetCollisionRadius() const { return CollisionRadius; }

	/** Radius of the fake contact shadow at scale 1 (cm): the feet area, not outstretched arms. */
	float GetShadowRadius() const { return ShadowRadius; }

	bool HasParts() const { return Parts.Num() > 0; }

	/** True when the body is an imported model rather than basic shapes. */
	bool IsModel() const { return bIsModel; }

	/** Optional material overrides (default: /Game/AlienMuseum/Materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	TObjectPtr<UMaterialInterface> SkinMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	TObjectPtr<UMaterialInterface> GlowMaterialOverride;

	/** Dynamic shadows on the body parts. Off by default: a contact shadow is much cheaper on Quest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	bool bCastShadows = false;

private:
	/** A data part that moves on its own (swinging limb, flapping wing, flickering flame...). */
	struct FAnimatedPart
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FVector BaseLocation = FVector::ZeroVector;
		FQuat BaseRotation = FQuat::Identity;
		FVector BaseScale = FVector::OneVector;
		FVector Pivot = FVector::ZeroVector; // parent space
		EAlienPartMotion Motion = EAlienPartMotion::None;
		float Amount = 0.f;
		float Speed = 1.f;
		float Phase = 0.f;
	};

	void CreateMaterials(const UAlienDataAsset* Data);
	void BuildBuiltInBody(const UAlienDataAsset* Data);
	void BuildModelBody(const UAlienDataAsset* Data, UStaticMesh* Mesh);
	void AddDataPart(const FAlienBodyPart& Part, bool bMirrored);
	UMaterialInterface* GetPartMaterial(const FAlienBodyPart& Part);
	UStaticMesh* GetShapeMesh(EAlienPartShape Shape) const;
	void AnimateDataParts(float SpeedAlpha);

	UStaticMeshComponent* AddPart(const TCHAR* BaseName, UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative, UMaterialInterface* Material);
	USceneComponent* AddPivot(const TCHAR* BaseName, USceneComponent* Parent, const FTransform& Relative);

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> BodyPivot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HeadPivot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> AntennaPivots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Eyes;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FootL;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FootR;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkinMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DarkMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EyeMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	/** One material instance per custom colour. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> CustomMIDs;

	TArray<FAnimatedPart> AnimatedParts;

	float ModelHeight = 32.f;
	float ModelRadius = 10.f;
	float CollisionRadius = 8.f;
	float ShadowRadius = 10.f;
	float Energy = 0.5f;
	bool bIsModel = false;
	bool bHovers = false;
	float HoverHeight = 0.f;
	FVector2D HeadBaseXY = FVector2D::ZeroVector;
	float HeadBaseZ = 0.f;

	FVector FootLBase = FVector::ZeroVector;
	FVector FootRBase = FVector::ZeroVector;
	TArray<FVector> EyeBaseScales;
	TArray<float> AntennaBaseRoll;

	float AnimTime = 0.f;
	float MaterializeTime = -1.f;
	float MaterializeDuration = 0.6f;
	float BlinkTimer = 2.f;
	float BlinkPhase = -1.f;
	FRotator HeadRotation = FRotator::ZeroRotator;
};

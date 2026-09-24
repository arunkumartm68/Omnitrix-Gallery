// Alien Museum - builds and animates the placeholder alien body from an AlienDataAsset.
//
// The body is made of engine basic shapes (spheres, cylinders), so there is no copyrighted or
// high-poly content. Animation is procedural (bob, squash & stretch, feet, head look-at, blink)
// which is far cheaper on Quest than a skeletal mesh + animation blueprint.
// When real alien art exists, give the AAlienCharacter a skeletal mesh instead; this component
// then stays empty.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "AlienAppearanceComponent.generated.h"

class UAlienDataAsset;
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

	bool HasParts() const { return Parts.Num() > 0; }

	/** Optional material overrides (default: /Game/AlienMuseum/Materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	TObjectPtr<UMaterialInterface> SkinMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	TObjectPtr<UMaterialInterface> GlowMaterialOverride;

	/** Dynamic shadows on the body parts. Off by default: a blob shadow is much cheaper on Quest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	bool bCastShadows = false;

private:
	UStaticMeshComponent* AddPart(const TCHAR* BaseName, class UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative, UMaterialInterface* Material);
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
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	float ModelHeight = 32.f;
	float ModelRadius = 10.f;
	float Energy = 0.5f;
	bool bHovers = false;
	float HoverHeight = 0.f;
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

// Alien Museum - builds and animates the alien body from an AlienDataAsset.
//
// The body is either the data asset's imported model (ModelMesh: one static mesh, scaled to the
// alien's height and stood on its feet) or made of engine basic shapes (spheres, cylinders, cones,
// cubes): one of the built-in shapes (Blob / Tall / Squat) or fully described by the Parts list
// (BodyShape = Custom). Animation is procedural (bob, squash & stretch, walk waddle / swing, head
// look-at, blink, wing flaps, flame flicker; models lean into turns and when speeding up), which is
// far cheaper on Quest than skeletal meshes + animation blueprints. Signature moves
// (UAlienActionComponent) bend the whole body through the action transform and extra lift.
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

	/** Where the body's feet sit relative to the parent (the owner places it). Moves build on it. */
	void SetBaseLocation(const FVector& Location);

	/** Extra scale / rotation / offset of the whole body for signature moves (pivot: the feet). */
	void SetActionTransform(const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Offset = FVector::ZeroVector);
	void ClearActionTransform() { SetActionTransform(FVector::OneVector); }

	/** Lifts the whole body off the ground (flying). The capsule and the contact shadow stay down. */
	void SetExtraLift(float Lift);
	float GetExtraLift() const { return ExtraLift; }

	/** Hides / shows the body parts (effects attached to them are left alone). */
	void SetBodyVisible(bool bShow);
	bool IsBodyVisible() const { return bBodyVisible; }

	/** The imported model's mesh component (nullptr for shape-built bodies). */
	UStaticMeshComponent* GetModelComponent() const { return ModelComponent; }

	/** Shows a second pose of the model (same scale, feet on the ground) until EndPose(). */
	bool ShowPose(UStaticMesh* PoseMesh);
	void EndPose();

	/** The component head effects (flames) attach to, and the top of the head relative to it. */
	USceneComponent* GetHeadTop(FVector& OutOffset) const;

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
	void ApplyRootTransform();

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
	TObjectPtr<UStaticMeshComponent> ModelComponent;

	/** The model's normal pose (while ShowPose swaps the mesh). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RestMesh;

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

	/** A model part that swings around its hinge (wings). */
	struct FModelPartMotion
	{
		TWeakObjectPtr<USceneComponent> Pivot;
		FVector Axis = FVector::ForwardVector; // body pivot space
		float Amount = 15.f;
		float Offset = 0.f;
		float Speed = 6.f;
		float FlyingAmount = 32.f;
		float FlyingOffset = 0.f;
		float FlyingSpeed = 12.f;
		float Phase = 0.f;
		float Cycle = 0.f;                     // own clock, so speed changes never jump
	};
	TArray<FModelPartMotion> ModelPartMotions;

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
	float HeadTopOffset = 0.f;   // top of the head above the head pivot (shape bodies)

	FQuat ModelFix = FQuat::Identity;
	FVector ModelOffset = FVector::ZeroVector;
	float ModelScale = 1.f;

	// Whole-body transform: owner placement + materialize pop-in + signature moves.
	FVector BaseLocation = FVector::ZeroVector;
	bool bHasBaseLocation = false;
	float MaterializeScale = 1.f;
	FVector ActionScale = FVector::OneVector;
	FRotator ActionRotation = FRotator::ZeroRotator;
	FVector ActionOffset = FVector::ZeroVector;
	float ExtraLift = 0.f;
	bool bBodyVisible = true;

	// Weight: models lean into turns and pitch when they speed up or slow down.
	float LastParentYaw = 0.f;
	bool bHasLastYaw = false;
	float LastSpeedAlpha = 0.f;
	float LeanRoll = 0.f;
	float LeanPitch = 0.f;

	FVector FootLBase = FVector::ZeroVector;
	FVector FootRBase = FVector::ZeroVector;
	TArray<FVector> EyeBaseScales;
	TArray<float> AntennaBaseRoll;

	float AnimTime = 0.f;
	float BreathTime = 0.f;
	float MaterializeTime = -1.f;
	float MaterializeDuration = 0.6f;
	float BlinkTimer = 2.f;
	float BlinkPhase = -1.f;
	FRotator HeadRotation = FRotator::ZeroRotator;
};

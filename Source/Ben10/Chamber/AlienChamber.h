// Alien Museum - a glass museum pod that holds one alien.
//
//  AAlienChamber
//   ├── Base (metal, the alien walks on it)
//   ├── FloorGlow / LightPanel (emissive, fake interior lighting)
//   ├── Glass (translucent, no collision)
//   ├── Frame pillars (one instanced mesh = one draw call) + TopCap
//   ├── ContainmentWalls (8 invisible boxes + ceiling, block only pawns)
//   ├── Obstacles (rock + crystal the alien walks around)
//   ├── MovementBounds (where the alien may walk)
//   ├── SpawnPoint
//   ├── SelectVolume (what the pointer ray hits)
//   ├── InfoRoot (holographic info panel)
//   └── InteriorLight (optional real point light, off by default for Quest performance)
//
// The actor origin is the centre of the base at floor level, so placing it on a surface only
// needs the hit location. Spatial anchors are added by UMuseumPersistenceComponent.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MuseumInteractable.h"
#include "AlienChamber.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UBoxComponent;
class UCapsuleComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAlienDataAsset;
class AAlienCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAlienChamberEvent, AAlienChamber*, Chamber);

UCLASS()
class BEN10_API AAlienChamber : public AActor, public IMuseumInteractable
{
	GENERATED_BODY()

public:
	AAlienChamber();

	// ---------- Occupant ----------

	/** Replaces the current occupant (if any) with a new alien of this type. */
	UFUNCTION(BlueprintCallable, Category = "Chamber")
	AAlienCharacter* SpawnAlien(UAlienDataAsset* Data);

	UFUNCTION(BlueprintCallable, Category = "Chamber")
	void RemoveAlien();

	UFUNCTION(BlueprintPure, Category = "Chamber")
	AAlienCharacter* GetOccupant() const { return Occupant; }

	UFUNCTION(BlueprintPure, Category = "Chamber")
	UAlienDataAsset* GetOccupantData() const { return OccupantData; }

	UFUNCTION(BlueprintPure, Category = "Chamber")
	bool IsOccupied() const { return Occupant != nullptr; }

	// ---------- Movement boundary (used by the alien) ----------

	/** True if WorldLocation is inside the walkable disc. Margin shrinks (positive) or grows (negative) it. */
	bool IsInsideMovementBounds(const FVector& WorldLocation, float Margin = 0.f) const;

	/** Closest point inside the walkable disc (kept at the chamber floor height). */
	FVector ClampToMovementBounds(const FVector& WorldLocation, float Margin = 0.f) const;

	/** Random reachable point for the alien, checked against obstacles with a capsule sweep. */
	bool FindRandomWanderPoint(const AAlienCharacter* Alien, FRandomStream& Rng, FVector& OutPoint) const;

	/** Where an alien with the given (world) capsule half height should be spawned. */
	FVector GetAlienSpawnLocation(float CapsuleHalfHeight) const;

	/** World Z of the surface the alien walks on. */
	float GetFloorZ() const;

	/** World-space radius of the outer shell, used to keep chambers from overlapping. */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	float GetOuterRadius() const;

	// ---------- Grabbing (driven by AMuseumPawn) ----------

	UFUNCTION(BlueprintCallable, Category = "Chamber|Grab")
	void BeginGrab();

	/** Target pose while held. The chamber follows it smoothly and always stays upright. */
	UFUNCTION(BlueprintCallable, Category = "Chamber|Grab")
	void UpdateGrab(const FVector& TargetLocation, float TargetYaw, float TargetScale);

	UFUNCTION(BlueprintCallable, Category = "Chamber|Grab")
	void EndGrab();

	UFUNCTION(BlueprintPure, Category = "Chamber|Grab")
	bool IsBeingGrabbed() const { return bGrabbed; }

	UFUNCTION(BlueprintPure, Category = "Chamber|Grab")
	float GetChamberScale() const;

	UFUNCTION(BlueprintCallable, Category = "Chamber|Grab")
	void SetChamberScale(float NewScale);

	/** Distance from a world point to the chamber shell (0 when inside). Used for "near grab". */
	float DistanceToChamber(const FVector& WorldPoint) const;

	/** Called by the director once the chamber is settled on a surface (spawn or release). */
	void NotifyPlaced();

	// ---------- Visual state ----------

	UFUNCTION(BlueprintCallable, Category = "Chamber")
	void SetHighlighted(bool bHighlighted, bool bValidTarget = true);

	UFUNCTION(BlueprintCallable, Category = "Chamber")
	void SetInfoPanelVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Chamber")
	void ToggleInfoPanel();

	// ---------- Persistence ----------

	const FGuid& GetChamberId() const { return ChamberId; }
	void SetChamberId(const FGuid& InId) { ChamberId = InId; }

	// ---------- Events ----------

	/** Chamber was spawned or put down (after snapping). */
	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnAlienChamberEvent OnPlaced;

	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnAlienChamberEvent OnGrabbed;

	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnAlienChamberEvent OnOccupantChanged;

	// ---------- IMuseumInteractable ----------

	virtual void OnPointerHover(UPrimitiveComponent* HitComponent, bool bHovered) override;
	virtual bool OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---------- Designer settings ----------

	/** Inner radius of the glass (cm, at scale 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 15, ClampMax = 80))
	float Radius = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 20, ClampMax = 150))
	float GlassHeight = 56.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 4, ClampMax = 30))
	float BaseHeight = 10.f;

	/** Allowed uniform scale while resizing (min, max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size")
	FVector2D ScaleRange = FVector2D(0.6f, 2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	FLinearColor LightColor = FLinearColor(0.25f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	FLinearColor FrameColor = FLinearColor(0.10f, 0.11f, 0.14f);

	/** Adds the rock and crystal obstacles inside the chamber. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	bool bShowObstacles = true;

	/** Optional material overrides. Empty = the /Game/AlienMuseum/Materials defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	TObjectPtr<UMaterialInterface> GlassMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	TObjectPtr<UMaterialInterface> MetalMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	TObjectPtr<UMaterialInterface> EmissiveMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	TObjectPtr<UMaterialInterface> HologramMaterialOverride;

	/** Real dynamic point light inside the chamber. Costs GPU time on Quest, so it is off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Lighting")
	bool bUseRealInteriorLight = false;

	/** Alien class used when the data asset does not name one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Alien")
	TSubclassOf<AAlienCharacter> DefaultAlienClass;

	/** How quickly the chamber catches up with the hand while held (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Grab")
	float GrabFollowSpeed = 18.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> Base;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> FloorGlow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> Glass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> TopCap;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> LightPanel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UInstancedStaticMeshComponent> FramePillars;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TArray<TObjectPtr<UBoxComponent>> ContainmentWalls;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UBoxComponent> Ceiling;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> ObstacleRock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> ObstacleCrystal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UBoxComponent> MovementBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<USceneComponent> SpawnPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UCapsuleComponent> SelectVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UPointLightComponent> InteriorLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<USceneComponent> InfoRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> InfoBackground;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UTextRenderComponent> InfoTitle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UTextRenderComponent> InfoBody;

private:
	void BuildLayout();
	void ApplyMaterials();
	void RefreshInfoText();
	void UpdateVisualState();
	void FaceInfoPanelToViewer(float DeltaSeconds);
	float GetMovementRadiusLocal() const { return Radius - 3.f; }

	UPROPERTY(Transient)
	TObjectPtr<AAlienCharacter> Occupant;

	UPROPERTY(Transient)
	TObjectPtr<UAlienDataAsset> OccupantData;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MetalMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlassMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HologramMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrystalMID;

	FGuid ChamberId;
	float PulseTime = 0.f; // > 0 while the "new occupant" light pulse plays
	bool bGrabbed = false;
	bool bHighlighted = false;
	bool bValidTargetHighlight = true;
	bool bInfoVisible = false;
	FVector GrabTargetLocation = FVector::ZeroVector;
	float GrabTargetYaw = 0.f;
	float GrabTargetScale = 1.f;
};

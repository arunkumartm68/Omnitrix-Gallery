// Alien Museum - a glass museum case that holds one alien (square display case or round pod).
//
//  AAlienChamber
//   ├── Base (metal, the alien walks on it)
//   ├── FloorGlow / LightPanel (emissive, fake interior lighting)
//   ├── Glass (translucent, no collision)
//   ├── Frame posts (one instanced mesh = one draw call) + TopCap
//   ├── ContainmentWalls (4 or 8 invisible boxes + ceiling, block only pawns)
//   ├── Obstacles (rock + crystal the alien walks around)
//   ├── MovementBounds (where the alien may walk)
//   ├── SpawnPoint
//   ├── SelectBox (what the pointer ray hits)
//   ├── HoverGlow (anti-gravity glow under the base while floating in the air)
//   ├── InfoRoot (holographic info panel)
//   └── InteriorLight (optional real point light, off by default for Quest performance)
//
// The actor origin is the centre of the underside of the base, so placing it on a surface only
// needs the hit location. Chambers have no gravity: they can also float in mid-air.
// Spatial anchors are added by UMuseumPersistenceComponent.

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
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAlienChamberEvent, AAlienChamber*, Chamber);

UENUM(BlueprintType)
enum class EChamberShape : uint8
{
	Square UMETA(ToolTip = "Box-shaped glass display case"),
	Round UMETA(ToolTip = "Cylindrical glass pod")
};

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

	/** True if WorldLocation is inside the walkable area. Margin shrinks (positive) or grows (negative) it. */
	bool IsInsideMovementBounds(const FVector& WorldLocation, float Margin = 0.f) const;

	/** Closest point inside the walkable area (kept at the chamber floor height). */
	FVector ClampToMovementBounds(const FVector& WorldLocation, float Margin = 0.f) const;

	/** Random reachable point for the alien, checked against obstacles with a capsule sweep. */
	bool FindRandomWanderPoint(const AAlienCharacter* Alien, FRandomStream& Rng, FVector& OutPoint) const;

	/** Where an alien with the given (world) capsule half height should be spawned. */
	FVector GetAlienSpawnLocation(float CapsuleHalfHeight) const;

	/** World Z of the surface the alien walks on. */
	float GetFloorZ() const;

	/** World-space radius around the outer shell (half diagonal for square cases), used to keep chambers apart. */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	float GetOuterRadius() const;

	/** World-space half width of the outer shell (the radius for a round pod). */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	float GetHalfWidth() const;

	/** World-space height from the underside of the base to the top of the cap. */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	float GetTotalHeight() const;

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

	/** True if the last grab actually moved, turned or resized the chamber (false for a simple click). */
	UFUNCTION(BlueprintPure, Category = "Chamber|Grab")
	bool WasMovedByLastGrab() const { return bLastGrabMoved; }

	/** Called by the director once the chamber is settled (spawn or release). */
	void NotifyPlaced();

	/** Shows the anti-gravity glow under the base (chamber floating in the air). */
	UFUNCTION(BlueprintCallable, Category = "Chamber")
	void SetFloating(bool bInFloating);

	UFUNCTION(BlueprintPure, Category = "Chamber")
	bool IsFloating() const { return bFloating; }

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

	/** Chamber was spawned or put down. */
	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnAlienChamberEvent OnPlaced;

	/** Fires once per grab, when the held chamber actually starts to move (not for a simple click). */
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

	/** Square display case or round pod. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size")
	EChamberShape Shape = EChamberShape::Square;

	/** Inner half-width of the glass for square cases, inner radius for round pods (cm, at scale 1). */
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
	TObjectPtr<UBoxComponent> SelectBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UStaticMeshComponent> HoverGlow;

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
	/** Chamber colour: the occupant's ChamberLightColor if it has one, else LightColor. */
	FLinearColor GetDesiredLightColor() const;
	void ApplyLightColor();
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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoverMID;

	/** Basic shapes used to switch between the square and round look. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	FGuid ChamberId;
	FLinearColor ActiveLightColor = FLinearColor(0.25f, 0.85f, 1.0f);
	float PulseTime = 0.f; // > 0 while the "new occupant" light pulse plays
	float HoverTime = 0.f;
	bool bFloating = false;
	bool bGrabbed = false;
	bool bMovedSinceGrab = false;
	bool bLastGrabMoved = false;
	FVector GrabStartLocation = FVector::ZeroVector;
	float GrabStartYaw = 0.f;
	float GrabStartScale = 1.f;
	bool bHighlighted = false;
	bool bValidTargetHighlight = true;
	bool bInfoVisible = false;
	FVector GrabTargetLocation = FVector::ZeroVector;
	float GrabTargetYaw = 0.f;
	float GrabTargetScale = 1.f;
};

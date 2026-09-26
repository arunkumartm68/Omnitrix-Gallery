// Alien Museum - a glass museum case that holds one alien (tall rectangular display case or round pod).
//
//  AAlienChamber
//   ├── Base (metal, the alien walks on it)
//   ├── FloorGlow / LightPanel (emissive, fake interior lighting)
//   ├── Glass (translucent, no collision)
//   ├── Frame posts (one instanced mesh = one draw call) + TopCap
//   ├── ContainmentWalls (4 or 8 invisible boxes + ceiling, block only pawns)
//   ├── Obstacles (optional rock + crystal the alien walks around)
//   ├── MovementBounds (where the alien may walk)
//   ├── SpawnPoint
//   ├── SelectBox (what the pointer ray hits)
//   ├── Resize handles (top = height, sides = width, base front = depth; shown while pointed at)
//   ├── HoverGlow (anti-gravity glow under the base while floating in the air)
//   ├── InfoRoot (holographic info panel) + SizeLabel (shown while resizing)
//   └── InteriorLight (optional real point light, off by default for Quest performance)
//
// Width, depth and glass height are independent and can change at runtime (resize handles), so
// every part is laid out from them in BuildLayout(). The actor scale stays uniform: it scales the
// whole exhibit, alien included, while the size only gives the alien more or less room.
//
// The actor origin is the centre of the underside of the base, so placing it on a surface only
// needs the hit location. Chambers have no gravity: they can also float in mid-air.
// Local axes: +X = front (faces the viewer when placed), Y = width, Z = up.
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
class USphereComponent;
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
	Square UMETA(DisplayName = "Box", ToolTip = "Rectangular glass display case"),
	Round UMETA(ToolTip = "Cylindrical glass pod (Width is its diameter)")
};

/** Which size a resize handle changes. */
UENUM(BlueprintType)
enum class EChamberResizeAxis : uint8
{
	None,
	Width UMETA(ToolTip = "Left-right; both sides move"),
	Depth UMETA(ToolTip = "Front-back; both sides move"),
	Height UMETA(ToolTip = "Glass height; the base stays put")
};

/** One grabbable resize knob with two arrows showing the drag direction. */
USTRUCT()
struct FChamberResizeHandle
{
	GENERATED_BODY()

	/** Invisible sphere the pointer ray hits (only while the handles are shown). */
	UPROPERTY()
	TObjectPtr<USphereComponent> Hit;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Knob;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ArrowOut;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ArrowIn;

	EChamberResizeAxis Axis = EChamberResizeAxis::None;

	/** +1 / -1: which side of the case the handle sits on along its axis. */
	float Side = 1.f;
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

	// ---------- Size ----------

	/** Inside size of the glass in cm at scale 1: X = depth, Y = width, Z = glass height. */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	FVector GetInnerSize() const { return FVector(Depth, Width, GlassHeight); }

	/** Changes depth / width / glass height (clamped so the occupant still fits) and rebuilds the case. */
	UFUNCTION(BlueprintCallable, Category = "Chamber|Size")
	void SetInnerSize(const FVector& NewSize);

	/** World-space size of the whole case (frame, base and cap included): X = depth, Y = width, Z = height. */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	FVector GetOuterSize() const;

	/** World-space half size of the outer footprint (X = depth, Y = width), used to keep chambers apart. */
	FVector2D GetFootprintHalfSize() const;

	/** World-space radius around the outer footprint (half its diagonal). */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	float GetOuterRadius() const;

	/** World-space height from the underside of the base to the top of the cap. */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	float GetTotalHeight() const;

	// ---------- Resize handles (driven by AMuseumPawn) ----------

	/** Which size a component changes when dragged (None if it is not a resize handle). */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	EChamberResizeAxis GetResizeAxis(const UPrimitiveComponent* Component) const;

	/** The resize handle whose knob is within MaxDistance of WorldPoint (hand-tracking pinch), if any. */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	UPrimitiveComponent* FindResizeHandleNear(const FVector& WorldPoint, float MaxDistance) const;

	/** World direction in which dragging this handle makes the chamber bigger. */
	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	FVector GetResizeDirection(const UPrimitiveComponent* Handle) const;

	UFUNCTION(BlueprintCallable, Category = "Chamber|Size")
	void BeginResize(const UPrimitiveComponent* Handle);

	/** DragDistance: how far (world cm) the hand moved along GetResizeDirection since BeginResize. */
	UFUNCTION(BlueprintCallable, Category = "Chamber|Size")
	void UpdateResize(float DragDistance);

	UFUNCTION(BlueprintCallable, Category = "Chamber|Size")
	void EndResize();

	UFUNCTION(BlueprintPure, Category = "Chamber|Size")
	bool IsBeingResized() const { return ResizeAxis != EChamberResizeAxis::None; }

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

	/** A resize handle was let go after changing the size. */
	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnAlienChamberEvent OnResized;

	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnAlienChamberEvent OnOccupantChanged;

	// ---------- IMuseumInteractable ----------

	virtual void OnPointerHover(UPrimitiveComponent* HitComponent, bool bHovered) override;
	virtual bool OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---------- Designer settings ----------

	/** Rectangular display case or round pod. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size")
	EChamberShape Shape = EChamberShape::Square;

	/** Inside width of the glass, left to right as seen from the front (cm at scale 1). Round pods: the diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 30, ClampMax = 250, Units = "cm"))
	float Width = 80.f;

	/** Inside depth of the glass, front to back (cm at scale 1). Round pods ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 30, ClampMax = 250, Units = "cm"))
	float Depth = 64.f;

	/** Height of the glass (cm at scale 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 30, ClampMax = 250, Units = "cm"))
	float GlassHeight = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size", meta = (ClampMin = 4, ClampMax = 30, Units = "cm"))
	float BaseHeight = 10.f;

	/** Allowed uniform scale of the whole exhibit (min, max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size")
	FVector2D ScaleRange = FVector2D(0.4f, 2.0f);

	/** Smallest and largest inside size a resize handle allows (cm at scale 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Size")
	FVector2D SizeRange = FVector2D(30.f, 250.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	FLinearColor LightColor = FLinearColor(0.25f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	FLinearColor FrameColor = FLinearColor(0.10f, 0.11f, 0.14f);

	/** Colour of the resize handles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	FLinearColor HandleColor = FLinearColor(0.35f, 0.95f, 1.0f);

	/** Adds the rock and crystal obstacles inside the chamber (they get in the way of big model aliens). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber|Look")
	bool bShowObstacles = false;

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

	/** "HEIGHT 105 cm" read-out next to the handle while resizing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UTextRenderComponent> SizeLabel;

	UPROPERTY(VisibleAnywhere, Category = "Chamber")
	TArray<FChamberResizeHandle> ResizeHandles;

private:
	void BuildLayout();
	void LayoutResizeHandles(const FVector2f& Half, float TopZ);
	void ApplyMaterials();
	/** Chamber colour: the occupant's ChamberLightColor if it has one, else LightColor. */
	FLinearColor GetDesiredLightColor() const;
	void ApplyLightColor();
	void RefreshInfoText();
	void UpdateVisualState();
	void UpdateHandleVisuals();
	void RefreshSizeLabel();
	void FaceViewer(USceneComponent* Component, float DeltaSeconds, bool bInstant = false) const;

	/** Inside half size of the glass in chamber space (X = depth, Y = width; a round pod uses its radius). */
	FVector2f GetInnerHalfLocal() const;

	/** Half size of the walkable floor in chamber space. */
	FVector2f GetMoveHalfLocal() const;

	const FChamberResizeHandle* FindHandle(const UPrimitiveComponent* Component) const;
	float GetSizeAlong(EChamberResizeAxis Axis) const;
	void SetSizeAlong(EChamberResizeAxis Axis, float NewSize);

	/** Smallest size along an axis that still fits the occupant (cm at scale 1). */
	float GetMinSizeAlong(EChamberResizeAxis Axis) const;

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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HandleMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HandleHotMID;

	/** Basic shapes used to switch between the box and round look. */
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

	/** Pointers currently over the chamber (both hands can hover it). */
	int32 HoverCount = 0;
	TWeakObjectPtr<const UPrimitiveComponent> HotHandle;
	TWeakObjectPtr<const UPrimitiveComponent> ResizeHandle;
	EChamberResizeAxis ResizeAxis = EChamberResizeAxis::None;
	float ResizeStartSize = 0.f;
};

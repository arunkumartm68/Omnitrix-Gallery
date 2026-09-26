// Alien Museum - the player in mixed reality.
//
// Controls (Touch controllers / hand tracking / desktop PIE):
//   Point + Trigger / Pinch / Left mouse   select UI, place chambers and aliens
//   Trigger or Grip on a chamber           grab and carry it (works up close or by ray)
//   Quick trigger / pinch on a chamber     show or hide its info panel
//   Both hands on one chamber              scale and rotate it
//   Thumbstick while carrying / Z C Q E    rotate (X) and resize (Y)
//   Y / B / Menu / Tab                     open or close the Alien Collection
//   Left-hand pinch and hold (1 s)         open or close the Alien Collection (hand tracking)
//   Desktop only: mouse look, WASD move

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/MuseumTypes.h"
#include "MuseumPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UInputMappingContext;
class UInputAction;
class UMuseumHandInteractor;
class UAlienDataAsset;
class AAlienChamber;
class AMuseumDirector;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EMuseumPawnMode : uint8
{
	Default,
	PlacingChamber,
	PlacingAlien
};

UCLASS()
class BEN10_API AMuseumPawn : public APawn
{
	GENERATED_BODY()

public:
	AMuseumPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** Next select on a floor/table places an empty chamber. */
	UFUNCTION(BlueprintCallable, Category = "Museum")
	void BeginPlaceChamber();

	/** Next select on a chamber puts this alien inside (or on a surface creates a new chamber with it). */
	UFUNCTION(BlueprintCallable, Category = "Museum")
	void BeginPlaceAlien(UAlienDataAsset* Alien);

	UFUNCTION(BlueprintCallable, Category = "Museum")
	void CancelPlacement();

	UFUNCTION(BlueprintPure, Category = "Museum")
	EMuseumPawnMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Museum")
	UAlienDataAsset* GetPendingAlien() const { return PendingAlien; }

	UFUNCTION(BlueprintPure, Category = "Museum")
	FVector GetHeadLocation() const;

	UFUNCTION(BlueprintPure, Category = "Museum")
	FRotator GetHeadRotation() const;

	// ---------- Input (optional assets; sensible defaults are created at runtime when empty) ----------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> InputMapping;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SelectLeftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SelectRightAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrabLeftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrabRightAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MenuAction;

	/** 2D: X rotates, Y resizes the carried chamber. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AdjustAction;

	/** Desktop testing only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Desktop testing only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	// ---------- Tuning ----------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float DesktopEyeHeight = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float DesktopMoveSpeed = 200.f;

	/** How close (cm) a hand must be to a chamber to grab it without the ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float NearGrabDistance = 8.f;

	/** A select shorter than this (s) that moved less than ClickMaxMove (cm) counts as a click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float ClickMaxTime = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float ClickMaxMove = 3.f;

	/** Degrees per second when rotating a carried chamber with the thumbstick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float RotateSpeed = 90.f;

	/** Relative size change per second when resizing with the thumbstick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float ScaleSpeed = 0.6f;

	/** Seconds of left-hand pinch (pointing at nothing) that toggles the collection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float MenuHoldTime = 1.0f;

	/**
	 * Floating chambers: when the pointer hits no surface, a new chamber appears in mid-air this far
	 * along the ray. Thumbstick forward/back (E/Q on desktop) changes it while placing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum", meta = (Units = "cm"))
	float FloatPlacementDistance = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	FVector2D FloatPlacementRange = FVector2D(50.f, 400.f);

	/** cm per second when changing FloatPlacementDistance with the thumbstick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float FloatDistanceSpeed = 120.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMotionControllerComponent> LeftAim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMotionControllerComponent> RightAim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMotionControllerComponent> LeftGrip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMotionControllerComponent> RightGrip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UStaticMeshComponent> LeftLaser;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UStaticMeshComponent> RightLaser;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UStaticMeshComponent> LeftReticle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UStaticMeshComponent> RightReticle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMuseumHandInteractor> LeftHand;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMuseumHandInteractor> RightHand;

	/** Hologram footprint shown while choosing where to put a chamber. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UStaticMeshComponent> PlacementGhost;

private:
	struct FHandGrab
	{
		TWeakObjectPtr<AAlienChamber> Chamber;
		FVector LocalOffset = FVector::ZeroVector;
		float YawOffset = 0.f;
		float TargetScale = 1.f;
		float StartTime = 0.f;
		FVector StartHandLocation = FVector::ZeroVector;
		bool bMoved = false;
		bool bFromSelect = false;
	};

	void CreateDefaultInput();
	void ApplyInputMapping();
	void ConfigureForDisplayMode();

	// Enhanced Input callbacks
	void OnSelectLeftStarted(const FInputActionValue& Value);
	void OnSelectLeftCompleted(const FInputActionValue& Value);
	void OnSelectRightStarted(const FInputActionValue& Value);
	void OnSelectRightCompleted(const FInputActionValue& Value);
	void OnGrabLeftStarted(const FInputActionValue& Value);
	void OnGrabLeftCompleted(const FInputActionValue& Value);
	void OnGrabRightStarted(const FInputActionValue& Value);
	void OnGrabRightCompleted(const FInputActionValue& Value);
	void OnMenu(const FInputActionValue& Value);
	void OnAdjust(const FInputActionValue& Value);
	void OnAdjustCompleted(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnMove(const FInputActionValue& Value);

	// Interactor events
	void HandleSelect(UMuseumHandInteractor* Hand, bool bPressed);
	void HandleGrab(UMuseumHandInteractor* Hand, bool bPressed);

	bool TryBeginGrab(UMuseumHandInteractor* Hand, bool bFromSelect);
	void EndGrab(UMuseumHandInteractor* Hand);
	void BeginTwoHandGrab(AAlienChamber* Chamber);
	void ResetOneHandGrab(UMuseumHandInteractor* Hand, AAlienChamber* Chamber);
	void UpdateGrabs(float DeltaSeconds);
	void UpdateHover();
	void UpdatePlacement(float DeltaSeconds);
	void UpdateMenuGesture(float DeltaSeconds);
	void ConfirmPlacement();
	void SetMode(EMuseumPawnMode NewMode);
	void SetPlacementTarget(AAlienChamber* Chamber);

	FHandGrab& GetGrab(const UMuseumHandInteractor* Hand);
	UMuseumHandInteractor* GetOtherHand(const UMuseumHandInteractor* Hand) const;
	UMuseumHandInteractor* GetPointingHand() const;
	AAlienChamber* FindNearestChamber(const FVector& Location, float MaxDistance) const;
	AMuseumDirector* GetDirector() const;

	UPROPERTY(Transient)
	TObjectPtr<UAlienDataAsset> PendingAlien;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMID;

	/** Ghost shapes for square cases and round pods. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> GhostCubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> GhostCylinderMesh;

	EMuseumPawnMode Mode = EMuseumPawnMode::Default;
	FHandGrab LeftGrab;
	FHandGrab RightGrab;

	bool bTwoHand = false;
	float TwoHandStartDistance = 1.f;
	float TwoHandStartScale = 1.f;
	float TwoHandStartYaw = 0.f;
	float TwoHandChamberStartYaw = 0.f;
	FVector TwoHandOffset = FVector::ZeroVector;

	FMuseumSurfaceHit PlacementHit;
	FVector PlacementLocation = FVector::ZeroVector; // bottom centre of the new chamber
	bool bPlacementInAir = false;
	bool bPlacementSpotOk = false;                  // a surface or open air (ignores other chambers)
	bool bPlacementValid = false;
	TWeakObjectPtr<AAlienChamber> PlacementTarget;

	TWeakObjectPtr<AActor> HoveredActor[2];
	TWeakObjectPtr<UPrimitiveComponent> HoveredComponent[2];

	FVector2D AdjustInput = FVector2D::ZeroVector;
	float MenuHoldTimer = 0.f;
	bool bMenuHoldConsumed = false;
	bool bDesktopMode = false;
	bool bInputCreated = false;
	float LastPlacementTime = -10.f;
};

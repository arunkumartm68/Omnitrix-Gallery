// Alien Museum - the player in mixed reality.
//
// Controls (Touch controllers / hand tracking / desktop PIE):
//   Point + Trigger / Pinch / Left mouse   select UI, place chambers and aliens
//   Trigger, Grip or fist on a chamber     grab and carry it (works up close or by ray; reaching out /
//                                          pulling in pushes / pulls a far one along the ray)
//   Grip / fist on an alien (up close or   take the alien out and hold it like a pet: turn your hand
//   by ray), or hold trigger / pinch on it to turn it, thumbstick / Z C spins it, Q E zooms, two hands
//                                          zoom; let go over its case = back in, elsewhere = it
//                                          floats home by itself
//   Quick trigger / pinch on a chamber     show or hide its info panel
//   Red X on a chamber (Delete on desktop) remove the chamber (press twice to confirm)
//   Both hands on one chamber              scale and rotate it
//   Thumbstick while carrying / Z C Q E    rotate (X) and resize (Y)
//   X / Y / B / Menu / Tab                 open or close the Alien Collection
//   Left palm to your face + pinch         open or close the Alien Collection (hand tracking; Quest's
//                                          menu gesture) - or a left pinch held 1 s pointing at nothing
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
class AAlienCharacter;
class AMuseumDirector;
class AOmnitrixWatch;
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

	/** The Omnitrix on the left wrist. */
	UFUNCTION(BlueprintPure, Category = "Museum")
	AOmnitrixWatch* GetWatch() const { return Watch; }

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

	/** Desktop testing (Delete): remove the chamber under the pointer, press twice like its red X. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RemoveAction;

	/** 2D: X rotates, Y resizes the carried chamber. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AdjustAction;

	/** Desktop testing only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Desktop testing only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** The Omnitrix's button (left thumbstick click; F on the desktop). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> WatchAction;

	/** The Omnitrix's dial (left thumbstick left / right; the mouse wheel or , and . on the desktop). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> WatchDialAction;

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

	/**
	 * Carrying a chamber grabbed from afar: reaching out or pulling the hand in moves it along the ray by that much
	 * times (its distance / the hand's reach), up to this - a case 3 m away comes 60 cm closer for 10 cm of hand.
	 * A chamber held at the hand moves 1:1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float MaxCarryDepthGain = 8.f;

	/** Relative size change per second when resizing with the thumbstick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float ScaleSpeed = 0.6f;

	/** How close (cm) a hand must be to an alien to pick it up directly (reach into the case). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float NearAlienDistance = 10.f;

	/** Degrees per second when spinning a held alien with the thumbstick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float ExamineSpinSpeed = 150.f;

	/** Relative size change per second when zooming a held alien with the thumbstick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	float ExamineZoomSpeed = 1.2f;

	/** Smallest and largest zoom of a held alien (1 = its size in the case). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum")
	FVector2D ExamineZoomRange = FVector2D(0.5f, 2.5f);

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

	/**
	 * A fingertip (hand tracking) or controller tip going into a case's glass at least this fast is a tap;
	 * slower, it is a hand reaching in, not a knock.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Glass", meta = (Units = "cm/s"))
	float TapMinSpeed = 25.f;

	/** ...and this fast or faster, it is a hard knock (a startled alien). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Glass", meta = (Units = "cm/s"))
	float TapKnockSpeed = 150.f;

	/**
	 * Wear the Omnitrix on the left wrist. Unfinished (the dial works in the editor, not yet tried on the
	 * headset, and nothing comes out when it is slammed), so it is off until it is finished.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Omnitrix")
	bool bWearOmnitrix = false;

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

	/** Over the ghost of a new alien's case: how big it will be ("LIFE SIZE", "78% OF LIFE SIZE"). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<class UTextRenderComponent> PlacementLabel;

private:
	struct FHandGrab
	{
		TWeakObjectPtr<AAlienChamber> Chamber;
		FVector LocalOffset = FVector::ZeroVector;
		float YawOffset = 0.f;
		float TargetScale = 1.f;
		float StartTime = 0.f;
		FVector StartHandLocation = FVector::ZeroVector;
		float StartReach = 0.f;   // how far the hand was held out from the head (horizontally) when it grabbed
		float DepthGain = 1.f;    // reaching out / pulling in moves the case this many times as far (far cases more)
		float MinDistance = 0.f;  // the case's centre is never pulled closer to the head than this
		bool bMoved = false;
		bool bFromSelect = false;
	};

	/** A hand holding an alien taken out of its case. */
	struct FHandAlien
	{
		TWeakObjectPtr<AAlienCharacter> Alien;
		FVector LocalOffset = FVector::ZeroVector;   // alien centre in the hand's frame, at zoom 1
		FQuat LocalRotation = FQuat::Identity;        // alien rotation in the hand's frame
		float Zoom = 1.f;
		float Spin = 0.f;                             // degrees around its own up axis (thumbstick)
		bool bFromSelect = false;
		bool bActive = false;
	};

	/** Trigger / pinch on an alien by ray: a quick click opens its case's info panel, holding picks it up. */
	struct FAlienPress
	{
		TWeakObjectPtr<AAlienCharacter> Alien;
		float StartTime = 0.f;
	};

	/** A hand dragging one of a chamber's resize handles. */
	struct FHandResize
	{
		TWeakObjectPtr<AAlienChamber> Chamber;
		TWeakObjectPtr<UPrimitiveComponent> Handle;
		FVector Direction = FVector::ZeroVector;  // world direction that makes the chamber bigger
		FVector StartPoint = FVector::ZeroVector;
		float RayDistance = 0.f;                  // ray drags: the point stays this far along the ray
		bool bNear = false;                       // hand-tracking pinch right at the handle
		bool bFromSelect = false;
	};

	void CreateDefaultInput();
	void ApplyInputMapping();
	void ConfigureForDisplayMode();

	// Enhanced Input callbacks. Trigger and grip come in as analog values every frame (Triggered) and 0 when let go
	// (Completed); the hand turns them into presses with a threshold.
	void OnSelectLeft(const FInputActionValue& Value);
	void OnSelectLeftReleased(const FInputActionValue& Value);
	void OnSelectRight(const FInputActionValue& Value);
	void OnSelectRightReleased(const FInputActionValue& Value);
	void OnGrabLeft(const FInputActionValue& Value);
	void OnGrabLeftReleased(const FInputActionValue& Value);
	void OnGrabRight(const FInputActionValue& Value);
	void OnGrabRightReleased(const FInputActionValue& Value);
	void SetLeftSelectAxis(float Value);
	void OnMenu(const FInputActionValue& Value);
	void HandleMenuPinch(UMuseumHandInteractor* Hand);
	void OnRemove(const FInputActionValue& Value);
	void OnAdjust(const FInputActionValue& Value);
	void OnAdjustCompleted(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnMove(const FInputActionValue& Value);
	void OnWatch(const FInputActionValue& Value);
	void OnWatchDial(const FInputActionValue& Value);
	void OnWatchDialCompleted(const FInputActionValue& Value);

	// Interactor events
	void HandleSelect(UMuseumHandInteractor* Hand, bool bPressed);
	void HandleGrab(UMuseumHandInteractor* Hand, bool bPressed);

	/** The red X (or Delete): the first press arms it, the second removes the chamber. */
	void PressRemove(AAlienChamber* Chamber);
	AAlienChamber* FindRemoveButton(const UMuseumHandInteractor* Hand) const;

	// Holding aliens
	AAlienCharacter* FindAlienNear(const FVector& Location, float MaxDistance) const;
	AAlienCharacter* FindAlienOnRay(const UMuseumHandInteractor* Hand, FVector* OutHitLocation = nullptr) const;
	AAlienCharacter* FindAlienToGrab(const UMuseumHandInteractor* Hand, bool& bOutNear, FVector* OutHitLocation = nullptr) const;
	bool BeginAlienGrab(UMuseumHandInteractor* Hand, AAlienCharacter* Alien, bool bFromSelect, bool bNear);
	void EndAlienGrab(UMuseumHandInteractor* Hand);
	void UpdateAlienPresses();
	void UpdateAlienGrabs(float DeltaSeconds);
	void UpdateAlienTargets();
	FHandAlien& GetAlienGrab(const UMuseumHandInteractor* Hand);
	FAlienPress& GetAlienPress(const UMuseumHandInteractor* Hand);

	bool TryBeginGrab(UMuseumHandInteractor* Hand, bool bFromSelect);
	void EndGrab(UMuseumHandInteractor* Hand);
	bool TryBeginResize(UMuseumHandInteractor* Hand, bool bFromSelect);
	void EndResize(UMuseumHandInteractor* Hand);
	void UpdateResizes();
	FVector GetResizePoint(const UMuseumHandInteractor* Hand, const FHandResize& Resize) const;
	FHandResize& GetResize(const UMuseumHandInteractor* Hand);
	void BeginTwoHandGrab(AAlienChamber* Chamber);
	/** Starts (or restarts) carrying with one hand; GrabPoint is where it took hold - at the hand, or along its ray. */
	void ResetOneHandGrab(UMuseumHandInteractor* Hand, AAlienChamber* Chamber, const FVector& GrabPoint);
	void UpdateGrabs(float DeltaSeconds);
	void UpdateHover();
	void UpdatePlacement(float DeltaSeconds);
	void UpdateMenuGesture(float DeltaSeconds);
	void ConfirmPlacement();
	void SetMode(EMuseumPawnMode NewMode);
	void SetPlacementTarget(AAlienChamber* Chamber);

	/** A hand's tap point near a case's glass: a tap is it going in through the glass, fast. */
	struct FHandTap
	{
		TWeakObjectPtr<AAlienChamber> Chamber;
		float LastDistance = 0.f;      // out of the glass last frame (cm, < 0 inside)
		double LastTapTime = -10.0;
		bool bHasLast = false;
		bool bArmed = true;            // after going in, the tip has to come back out before it can tap again
	};

	void UpdateGlassTaps(float DeltaSeconds);
	FHandTap& GetTap(const UMuseumHandInteractor* Hand);

	FHandGrab& GetGrab(const UMuseumHandInteractor* Hand);
	UMuseumHandInteractor* GetOtherHand(const UMuseumHandInteractor* Hand) const;
	UMuseumHandInteractor* GetPointingHand() const;
	/** Roughly where this hand's shoulder is (the arm swings around it). */
	FVector GetShoulder(const UMuseumHandInteractor* Hand) const;
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
	FHandResize LeftResize;
	FHandResize RightResize;
	FHandAlien LeftAlien;
	FHandAlien RightAlien;
	FAlienPress LeftPress;
	FAlienPress RightPress;
	FHandTap LeftTap;
	FHandTap RightTap;

	/** The Omnitrix on the left wrist (spawned in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<AOmnitrixWatch> Watch;

	/** The left trigger went to the watch (a slam): its release goes there too, not to the hand. */
	bool bLeftSelectToWatch = false;

	void UpdateWatchHands();

	UFUNCTION()
	void HandleOmnitrixTransform(UAlienDataAsset* Alien);

	UFUNCTION()
	void HandleOmnitrixRevert(bool bTimedOut);
	TWeakObjectPtr<AAlienCharacter> TargetedAlien[2];
	bool bTwoHandAlien = false;
	float TwoHandAlienStartDistance = 1.f;
	float TwoHandAlienStartZoom = 1.f;

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

	/** The hand that pressed select last: it points for placing a chamber (a left-hander's left, not always the right). */
	TWeakObjectPtr<UMuseumHandInteractor> LastSelectHand;

	/** The collection was opened / closed then (a menu button and a menu gesture at once toggle it only once). */
	double LastMenuToggleTime = -10.0;
	float MenuHoldTimer = 0.f;
	bool bMenuHoldConsumed = false;
	bool bDesktopMode = false;
	bool bInputCreated = false;
	float LastPlacementTime = -10.f;
};

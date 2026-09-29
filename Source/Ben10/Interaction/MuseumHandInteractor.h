// Alien Museum - one hand of the player: pose, buttons/pinch and the pointer ray.
//
// Works with three input sources and picks the best one every frame:
//   Hand       - Quest hand tracking (pinch = select, closed fist = grab; the grab point is the
//                pinch point, or the palm while the fist is closed; a pinch with the palm turned
//                to the face is Quest's menu gesture and selects nothing)
//   Controller - Touch controllers (trigger = select, grip = grab, both with a press / release threshold)
//   Desktop    - mouse ray from the camera when no headset is active (PIE testing)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "MuseumHandInteractor.generated.h"

class UMotionControllerComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMuseumHandSource : uint8
{
	None,
	Controller,
	Hand,
	Desktop
};

USTRUCT(BlueprintType)
struct BEN10_API FMuseumPointerHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	bool bHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	FVector Normal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	float Distance = 0.f;

	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UPrimitiveComponent> Component;
};

UCLASS(ClassGroup = (AlienMuseum))
class BEN10_API UMuseumHandInteractor : public UActorComponent
{
	GENERATED_BODY()

public:
	UMuseumHandInteractor();

	/** Wires the components owned by the pawn. */
	void Setup(EControllerHand InHand, UMotionControllerComponent* InAim, UMotionControllerComponent* InGrip,
		UStaticMeshComponent* InLaser, UStaticMeshComponent* InReticle);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Button input routed from Enhanced Input ----
	void SetControllerSelect(bool bPressed);
	void SetControllerGrab(bool bPressed);

	/**
	 * Trigger / grip as analog values (0..1): pressed from ControllerPressThreshold, let go again under
	 * ControllerReleaseThreshold - a finger resting on the grip never grabs, and a trigger that doesn't spring all
	 * the way back still lets go.
	 */
	void SetControllerSelectAxis(float Value);
	void SetControllerGrabAxis(float Value);
	bool IsControllerSelectHeld() const { return bControllerSelect; }

	/** Desktop testing: aim ray supplied by the pawn (camera ray). */
	void SetDesktopRay(const FVector& Origin, const FVector& Direction);

	// ---- Queries ----
	EControllerHand GetHand() const { return Hand; }
	EMuseumHandSource GetSource() const { return Source; }
	bool IsTracked() const { return Source != EMuseumHandSource::None; }
	FVector GetAimOrigin() const { return AimOrigin; }
	FVector GetAimDirection() const { return AimDirection; }
	FVector GetGrabLocation() const { return GrabLocation; }
	/** Orientation of the holding hand (grip controller, palm or camera), for turning held things. */
	FQuat GetGrabRotation() const { return GrabRotation; }
	/** Yaw used to carry objects (direction the hand points, flattened). */
	float GetCarryYaw() const { return AimDirection.Rotation().Yaw; }
	bool IsSelectPressed() const { return bSelectPressed; }
	bool IsGrabPressed() const { return bGrabPressed; }
	const FMuseumPointerHit& GetPointerHit() const { return PointerHit; }

	/**
	 * The point that taps on glass: the index fingertip (hand tracking - not while pinching or making a fist,
	 * those select and grab) or the controller's tip. False on the desktop and while the hand is lost.
	 */
	bool GetTapPoint(FVector& OutPoint) const { OutPoint = TapPoint; return bHasTapPoint; }

	/** A short buzz on this hand's controller (nothing with hand tracking). Amplitude 0..1. */
	void PulseHaptics(float Amplitude, float Duration = 0.04f);

	/**
	 * The wrist, for a watch. Hand tracking: a frame built from the joints - X along the hand to the fingers,
	 * Y across it (towards the thumb on the left hand), Z out of the back of the hand - at the wrist joint.
	 * Controller: the grip pose. False on the desktop and while the hand is lost.
	 */
	bool GetWristPose(FTransform& OutPose) const { OutPose = WristPose; return bHasWristPose; }

	/** The controller's grip pose (a watch rides on it, late-updated with the controller). */
	UMotionControllerComponent* GetGripController() const { return GripController; }

	/** Hand tracking: pinching, and where (between the thumb and index tips) - even while busy. */
	bool IsPinching() const { return bPinching; }
	FVector GetPinchPoint() const { return PinchPoint; }

	/** Hand tracking: the palm is turned to the face and held in view - Quest's menu pose. */
	bool IsPalmFacingHead() const { return bPalmFacingHead; }

	/**
	 * Busy (working the watch up close): its trigger / pinch and grip / fist select and grab nothing and its
	 * laser is off. A button pressed while busy stays ignored until it is let go.
	 */
	void SetBusy(bool bInBusy) { bBusy = bInBusy; }
	bool IsBusy() const { return bBusy; }

	// ---- Visual overrides (valid for the current frame) ----
	/** Makes the laser end at EndPoint with the given colour, e.g. for a placement preview. */
	void SetLaserOverride(const FVector& EndPoint, const FLinearColor& Color);
	void SetLaserEnabled(bool bEnabled) { bLaserEnabled = bEnabled; }

	// ---- Events ----
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnHandButton, UMuseumHandInteractor* /*Hand*/, bool /*bPressed*/);
	/** Trigger (controller), pinch (hand) or left mouse (desktop). */
	FOnHandButton OnSelect;
	/** Grip (controller), closed fist (hand tracking) or right mouse (desktop). */
	FOnHandButton OnGrab;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHandGesture, UMuseumHandInteractor* /*Hand*/);
	/**
	 * Hand tracking: a pinch made with the palm turned to the face (Quest's menu gesture on the left hand). That
	 * pinch selects nothing, whatever the ray is on.
	 */
	FOnHandGesture OnMenuPinch;

	// ---- Settings ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float MaxPointerDistance = 500.f;

	/** Thumb-index tip distance (cm) that starts a pinch (the tip joints stay about 1.5-2 cm apart when the pads touch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float PinchStartDistance = 2.5f;

	/** Thumb-index distance (cm) that ends a pinch (hysteresis against flicker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float PinchEndDistance = 4.0f;

	/** Average palm-to-fingertip distance (middle, ring, little; cm) that closes a fist = grab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float FistStartDistance = 5.5f;

	/** ... and that opens it again (hysteresis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float FistEndDistance = 7.5f;

	/**
	 * A fist also needs the index tip this close to the palm (cm): curling only the middle, ring and little fingers
	 * is how many people pinch, and that must stay a pinch.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float FistIndexStartDistance = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float FistIndexEndDistance = 7.5f;

	/** Controller trigger / grip travel (0..1) that presses it... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float ControllerPressThreshold = 0.55f;

	/** ... and under which it lets go again (hysteresis against flicker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float ControllerReleaseThreshold = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	FLinearColor LaserColor = FLinearColor(0.3f, 0.8f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	FLinearColor LaserHoverColor = FLinearColor(0.4f, 1.0f, 0.5f);

private:
	bool UpdateFromHandTracking();
	bool UpdateFromController();
	void UpdatePointer();
	void UpdateVisuals();
	void SetSelectState(bool bPressed);
	void SetGrabState(bool bPressed);

	UPROPERTY(Transient)
	TObjectPtr<UMotionControllerComponent> AimController;

	UPROPERTY(Transient)
	TObjectPtr<UMotionControllerComponent> GripController;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Laser;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Reticle;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LaserMID;

	EControllerHand Hand = EControllerHand::Right;
	EMuseumHandSource Source = EMuseumHandSource::None;
	FVector AimOrigin = FVector::ZeroVector;
	FVector AimDirection = FVector::ForwardVector;
	FVector GrabLocation = FVector::ZeroVector;
	FQuat GrabRotation = FQuat::Identity;
	FMuseumPointerHit PointerHit;

	bool bSelectPressed = false;
	bool bGrabPressed = false;
	bool bControllerSelect = false;
	bool bControllerGrab = false;
	bool bPinching = false;
	bool bFist = false;
	bool bPalmFacingHead = false;
	bool bMenuPinch = false; // this pinch began in the menu pose: it selects nothing

	FVector TapPoint = FVector::ZeroVector;
	bool bHasTapPoint = false;
	FTransform WristPose;
	bool bHasWristPose = false;
	FVector PinchPoint = FVector::ZeroVector;
	bool bBusy = false;
	bool bSelectLatched = false; // pressed while busy: ignored until let go
	bool bGrabLatched = false;
	FTimerHandle HapticTimer;

	bool bHasDesktopRay = false;
	FVector DesktopOrigin = FVector::ZeroVector;
	FVector DesktopDirection = FVector::ForwardVector;

	bool bLaserEnabled = true;
	bool bHasLaserOverride = false;
	FVector LaserOverrideEnd = FVector::ZeroVector;
	FLinearColor LaserOverrideColor = FLinearColor::White;
};

// Alien Museum - one hand of the player: pose, buttons/pinch and the pointer ray.
//
// Works with three input sources and picks the best one every frame:
//   Hand       - Quest hand tracking (pinch = select, closed fist = grab; the grab point is the
//                pinch point, or the palm while the fist is closed)
//   Controller - Touch controllers (trigger = select, grip = grab)
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

	// ---- Settings ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float MaxPointerDistance = 500.f;

	/** Thumb-index distance (cm) that starts a pinch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float PinchStartDistance = 1.8f;

	/** Thumb-index distance (cm) that ends a pinch (hysteresis against flicker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float PinchEndDistance = 3.5f;

	/** Average palm-to-fingertip distance (middle, ring, little; cm) that closes a fist = grab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float FistStartDistance = 5.5f;

	/** ... and that opens it again (hysteresis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Hand")
	float FistEndDistance = 7.5f;

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

	bool bHasDesktopRay = false;
	FVector DesktopOrigin = FVector::ZeroVector;
	FVector DesktopDirection = FVector::ForwardVector;

	bool bLaserEnabled = true;
	bool bHasLaserOverride = false;
	FVector LaserOverrideEnd = FVector::ZeroVector;
	FLinearColor LaserOverrideColor = FLinearColor::White;
};

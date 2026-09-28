// Alien Museum - one hand of the player: pose, buttons/pinch and the pointer ray.

#include "Interaction/MuseumHandInteractor.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumInteractable.h"
#include "Core/MuseumTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "TimerManager.h"

UMuseumHandInteractor::UMuseumHandInteractor()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMuseumHandInteractor::Setup(EControllerHand InHand, UMotionControllerComponent* InAim, UMotionControllerComponent* InGrip,
	UStaticMeshComponent* InLaser, UStaticMeshComponent* InReticle)
{
	Hand = InHand;
	AimController = InAim;
	GripController = InGrip;
	Laser = InLaser;
	Reticle = InReticle;

	if (UMaterialInterface* Emissive = MuseumAssets::EmissiveMaterial())
	{
		LaserMID = UMaterialInstanceDynamic::Create(Emissive, this);
		LaserMID->SetVectorParameterValue(MuseumAssets::Params::Color, LaserColor);
		LaserMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.5f);
		if (Laser)
		{
			Laser->SetMaterial(0, LaserMID);
		}
		if (Reticle)
		{
			Reticle->SetMaterial(0, LaserMID);
		}
	}
}

void UMuseumHandInteractor::SetControllerSelect(bool bPressed)
{
	bControllerSelect = bPressed;
}

void UMuseumHandInteractor::SetControllerGrab(bool bPressed)
{
	bControllerGrab = bPressed;
}

void UMuseumHandInteractor::SetDesktopRay(const FVector& Origin, const FVector& Direction)
{
	bHasDesktopRay = true;
	DesktopOrigin = Origin;
	DesktopDirection = Direction.GetSafeNormal();
}

void UMuseumHandInteractor::SetLaserOverride(const FVector& EndPoint, const FLinearColor& Color)
{
	bHasLaserOverride = true;
	LaserOverrideEnd = EndPoint;
	LaserOverrideColor = Color;
}

void UMuseumHandInteractor::SetSelectState(bool bPressed)
{
	if (bPressed != bSelectPressed)
	{
		bSelectPressed = bPressed;
		OnSelect.Broadcast(this, bPressed);
	}
}

void UMuseumHandInteractor::SetGrabState(bool bPressed)
{
	if (bPressed != bGrabPressed)
	{
		bGrabPressed = bPressed;
		OnGrab.Broadcast(this, bPressed);
	}
}

bool UMuseumHandInteractor::UpdateFromHandTracking()
{
	FXRHandTrackingState State;
	UHeadMountedDisplayFunctionLibrary::GetHandTrackingState(GetOwner(), EXRSpaceType::UnrealWorldSpace, Hand, State);
	if (!State.bValid || State.TrackingStatus == ETrackingStatus::NotTracked || State.HandKeyLocations.Num() < EHandKeypointCount)
	{
		return false;
	}

	auto Key = [&State](EHandKeypoint Keypoint) { return State.HandKeyLocations[static_cast<int32>(Keypoint)]; };
	const FVector ThumbTip = Key(EHandKeypoint::ThumbTip);
	const FVector IndexTip = Key(EHandKeypoint::IndexTip);
	const FVector IndexKnuckle = Key(EHandKeypoint::IndexProximal);
	const FVector Palm = Key(EHandKeypoint::Palm);

	// Pointer ray from an estimated shoulder through the index knuckle: stable and natural,
	// similar to the system hand ray.
	FVector Shoulder = IndexKnuckle - FVector(40.f, 0.f, -20.f);
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const FRotator FlatView(0.f, Camera->GetCameraRotation().Yaw, 0.f);
		const float Side = Hand == EControllerHand::Left ? -1.f : 1.f;
		Shoulder = Camera->GetCameraLocation() + FVector(0.f, 0.f, -15.f) + FRotationMatrix(FlatView).GetScaledAxis(EAxis::Y) * (15.f * Side);
	}
	AimOrigin = IndexKnuckle;
	AimDirection = (IndexKnuckle - Shoulder).GetSafeNormal();
	if (State.HandKeyRotations.IsValidIndex(static_cast<int32>(EHandKeypoint::Palm)))
	{
		GrabRotation = State.HandKeyRotations[static_cast<int32>(EHandKeypoint::Palm)];
	}

	// Closed fist = grab: the middle, ring and little fingertips curl in to the palm.
	const float Curl = (FVector::Dist(Key(EHandKeypoint::MiddleTip), Palm) + FVector::Dist(Key(EHandKeypoint::RingTip), Palm)
		+ FVector::Dist(Key(EHandKeypoint::LittleTip), Palm)) / 3.f;
	bFist = bFist ? Curl < FistEndDistance : Curl < FistStartDistance;

	// Pinch with hysteresis - not while making a fist (closing fingers brush thumb and index together).
	const float PinchDistance = FVector::Dist(ThumbTip, IndexTip);
	bPinching = !bFist && (bPinching ? PinchDistance < PinchEndDistance : PinchDistance < PinchStartDistance);

	// Grab point: the palm inside a fist, else between thumb and index. Set before the events fire.
	GrabLocation = bFist ? Palm : (ThumbTip + IndexTip) * 0.5f;
	// Taps come from the fingertip; a pinch or a fist is busy selecting or grabbing.
	TapPoint = IndexTip;
	bHasTapPoint = !bFist && !bPinching;
	SetSelectState(bPinching);
	SetGrabState(bFist);
	return true;
}

bool UMuseumHandInteractor::UpdateFromController()
{
	if (!AimController || !AimController->IsTracked())
	{
		return false;
	}
	AimOrigin = AimController->GetComponentLocation();
	AimDirection = AimController->GetForwardVector();
	const bool bGripTracked = GripController && GripController->IsTracked();
	GrabLocation = bGripTracked ? GripController->GetComponentLocation() : AimOrigin;
	GrabRotation = bGripTracked ? GripController->GetComponentQuat() : AimController->GetComponentQuat();
	// The aim pose sits at the front of the controller: its tip knocks on glass.
	TapPoint = AimOrigin + AimDirection * 1.5f;
	bHasTapPoint = true;
	SetSelectState(bControllerSelect);
	SetGrabState(bControllerGrab);
	return true;
}

void UMuseumHandInteractor::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EMuseumHandSource NewSource = EMuseumHandSource::None;
	if (bHasDesktopRay)
	{
		AimOrigin = DesktopOrigin;
		AimDirection = DesktopDirection;
		GrabLocation = DesktopOrigin + DesktopDirection * 40.f;
		GrabRotation = FRotationMatrix::MakeFromX(DesktopDirection).ToQuat();
		SetSelectState(bControllerSelect);
		SetGrabState(bControllerGrab);
		NewSource = EMuseumHandSource::Desktop;
	}
	else if (UpdateFromHandTracking())
	{
		NewSource = EMuseumHandSource::Hand;
	}
	else if (UpdateFromController())
	{
		NewSource = EMuseumHandSource::Controller;
	}

	if (NewSource != EMuseumHandSource::Hand)
	{
		bPinching = false;
		bFist = false;
	}
	if (NewSource == EMuseumHandSource::None || NewSource == EMuseumHandSource::Desktop)
	{
		bHasTapPoint = false;
	}
	Source = NewSource;

	if (Source == EMuseumHandSource::None)
	{
		// Tracking lost: release everything so nothing stays stuck to an invisible hand.
		SetSelectState(false);
		SetGrabState(false);
		PointerHit = FMuseumPointerHit();
	}
	else
	{
		UpdatePointer();
	}
	UpdateVisuals();
	bHasLaserOverride = false;
}

void UMuseumHandInteractor::PulseHaptics(float Amplitude, float Duration)
{
	APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
	UWorld* World = GetWorld();
	if (!Player || !World || Source != EMuseumHandSource::Controller)
	{
		return;
	}
	Player->SetHapticsByValue(1.f, FMath::Clamp(Amplitude, 0.f, 1.f), Hand);
	World->GetTimerManager().SetTimer(HapticTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (APlayerController* Owner = UGameplayStatics::GetPlayerController(this, 0))
		{
			Owner->SetHapticsByValue(0.f, 0.f, Hand);
		}
	}), FMath::Max(0.01f, Duration), false);
}

void UMuseumHandInteractor::UpdatePointer()
{
	PointerHit = FMuseumPointerHit();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MuseumPointer), false, GetOwner());
	const FVector End = AimOrigin + AimDirection * MaxPointerDistance;
	if (World->LineTraceSingleByChannel(Hit, AimOrigin, End, MuseumCollision::PointerChannel, Params))
	{
		PointerHit.bHit = true;
		PointerHit.Location = Hit.ImpactPoint;
		PointerHit.Normal = Hit.ImpactNormal;
		PointerHit.Distance = Hit.Distance;
		PointerHit.Actor = Hit.GetActor();
		PointerHit.Component = Hit.GetComponent();
	}
}

void UMuseumHandInteractor::UpdateVisuals()
{
	if (!Laser || !Reticle)
	{
		return;
	}

	const bool bTracked = Source != EMuseumHandSource::None;
	const bool bShowLaser = bLaserEnabled && bTracked && Source != EMuseumHandSource::Desktop;
	const bool bHasEnd = bHasLaserOverride || PointerHit.bHit;
	Laser->SetVisibility(bShowLaser);
	Reticle->SetVisibility(bLaserEnabled && bTracked && bHasEnd);
	if (!bTracked)
	{
		return;
	}

	const FVector End = bHasLaserOverride ? LaserOverrideEnd
		: (PointerHit.bHit ? PointerHit.Location : AimOrigin + AimDirection * 100.f);

	FLinearColor Color = LaserColor;
	if (bHasLaserOverride)
	{
		Color = LaserOverrideColor;
	}
	else if (PointerHit.Actor.IsValid() && Cast<IMuseumInteractable>(PointerHit.Actor.Get()))
	{
		Color = LaserHoverColor;
	}
	if (LaserMID)
	{
		LaserMID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
	}

	if (bShowLaser)
	{
		const FVector Delta = End - AimOrigin;
		const float Length = FMath::Max(1.f, static_cast<float>(Delta.Size()));
		Laser->SetWorldLocationAndRotation(AimOrigin + Delta * 0.5f, FRotationMatrix::MakeFromZ(Delta / Length).Rotator());
		Laser->SetWorldScale3D(FVector(0.004f, 0.004f, Length / 100.f));
	}
	Reticle->SetWorldLocation(End);
	Reticle->SetWorldScale3D(FVector(Source == EMuseumHandSource::Desktop ? 0.01f : 0.015f));
}

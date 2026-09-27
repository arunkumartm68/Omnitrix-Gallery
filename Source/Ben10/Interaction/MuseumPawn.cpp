// Alien Museum - the player in mixed reality.

#include "Interaction/MuseumPawn.h"
#include "Interaction/MuseumHandInteractor.h"
#include "Aliens/AlienCharacter.h"
#include "Chamber/AlienChamber.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumDirector.h"
#include "Core/MuseumInteractable.h"
#include "MR/MuseumSceneComponent.h"
#include "Data/AlienDataAsset.h"
#include "Ben10.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FLinearColor ValidColor(0.3f, 1.0f, 0.5f);
	const FLinearColor InvalidColor(1.0f, 0.3f, 0.25f);
	const FLinearColor AlienTargetColor(1.0f, 0.8f, 0.25f); // gold: this alien can be picked up
}

AMuseumPawn::AMuseumPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	GhostCubeMesh = CubeFinder.Object;
	GhostCylinderMesh = CylinderFinder.Object;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Tracking space: everything tracked by the headset lives under VROrigin.
	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	VROrigin->SetupAttachment(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true;

	auto MakeController = [this](const TCHAR* Name, const TCHAR* Source)
	{
		UMotionControllerComponent* Controller = CreateDefaultSubobject<UMotionControllerComponent>(Name);
		Controller->SetupAttachment(VROrigin);
		Controller->SetTrackingMotionSource(FName(Source));
		return Controller;
	};
	LeftAim = MakeController(TEXT("LeftAim"), TEXT("LeftAim"));
	RightAim = MakeController(TEXT("RightAim"), TEXT("RightAim"));
	LeftGrip = MakeController(TEXT("LeftGrip"), TEXT("LeftGrip"));
	RightGrip = MakeController(TEXT("RightGrip"), TEXT("RightGrip"));

	auto MakeVisual = [this, Root](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Visual = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Visual->SetupAttachment(Root);
		Visual->SetStaticMesh(Mesh);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetCastShadow(false);
		Visual->SetUsingAbsoluteScale(true);
		Visual->SetVisibility(false);
		Visual->SetCanEverAffectNavigation(false);
		return Visual;
	};
	LeftLaser = MakeVisual(TEXT("LeftLaser"), CylinderFinder.Object);
	RightLaser = MakeVisual(TEXT("RightLaser"), CylinderFinder.Object);
	LeftReticle = MakeVisual(TEXT("LeftReticle"), SphereFinder.Object);
	RightReticle = MakeVisual(TEXT("RightReticle"), SphereFinder.Object);

	PlacementGhost = MakeVisual(TEXT("PlacementGhost"), CubeFinder.Object);
	PlacementGhost->SetUsingAbsoluteRotation(true);
	PlacementGhost->SetWorldScale3D(FVector(0.66f, 0.66f, 0.72f)); // default chamber size; matched to the chamber class while placing
	PlacementGhost->SetTranslucentSortPriority(2);

	LeftHand = CreateDefaultSubobject<UMuseumHandInteractor>(TEXT("LeftHand"));
	RightHand = CreateDefaultSubobject<UMuseumHandInteractor>(TEXT("RightHand"));
}

void AMuseumPawn::BeginPlay()
{
	Super::BeginPlay();

	LeftHand->Setup(EControllerHand::Left, LeftAim, LeftGrip, LeftLaser, LeftReticle);
	RightHand->Setup(EControllerHand::Right, RightAim, RightGrip, RightLaser, RightReticle);
	LeftHand->OnSelect.AddUObject(this, &AMuseumPawn::HandleSelect);
	RightHand->OnSelect.AddUObject(this, &AMuseumPawn::HandleSelect);
	LeftHand->OnGrab.AddUObject(this, &AMuseumPawn::HandleGrab);
	RightHand->OnGrab.AddUObject(this, &AMuseumPawn::HandleGrab);

	// Read the hands after they have updated this frame.
	AddTickPrerequisiteComponent(LeftHand);
	AddTickPrerequisiteComponent(RightHand);

	if (UMaterialInterface* Hologram = MuseumAssets::HologramMaterial())
	{
		GhostMID = UMaterialInstanceDynamic::Create(Hologram, this);
		GhostMID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.35f);
		PlacementGhost->SetMaterial(0, GhostMID);
	}

	ConfigureForDisplayMode();
	CreateDefaultInput();
	ApplyInputMapping();
}

void AMuseumPawn::ConfigureForDisplayMode()
{
	bDesktopMode = !UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
	if (bDesktopMode)
	{
		// PIE without a headset: a normal first-person camera at eye height.
		Camera->bUsePawnControlRotation = true;
		Camera->SetRelativeLocation(FVector(0.f, 0.f, DesktopEyeHeight));
		UE_LOG(LogAlienMuseum, Log, TEXT("Museum pawn in desktop test mode (mouse look, WASD, LMB select, RMB grab, Tab collection)"));
	}
	else
	{
		// Mixed reality: the pawn origin is the real floor, the camera follows the headset.
		Camera->bUsePawnControlRotation = false;
		Camera->SetRelativeLocation(FVector::ZeroVector);
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::LocalFloor);
	}
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void AMuseumPawn::CreateDefaultInput()
{
	if (bInputCreated)
	{
		return;
	}
	bInputCreated = true;

	auto MakeAction = [this](TObjectPtr<UInputAction>& Action, const TCHAR* Name, EInputActionValueType Type)
	{
		if (!Action)
		{
			Action = NewObject<UInputAction>(this, Name);
			Action->ValueType = Type;
		}
	};
	MakeAction(SelectLeftAction, TEXT("IA_Museum_SelectLeft"), EInputActionValueType::Axis1D);
	MakeAction(SelectRightAction, TEXT("IA_Museum_SelectRight"), EInputActionValueType::Axis1D);
	MakeAction(GrabLeftAction, TEXT("IA_Museum_GrabLeft"), EInputActionValueType::Axis1D);
	MakeAction(GrabRightAction, TEXT("IA_Museum_GrabRight"), EInputActionValueType::Axis1D);
	MakeAction(MenuAction, TEXT("IA_Museum_Menu"), EInputActionValueType::Boolean);
	MakeAction(RemoveAction, TEXT("IA_Museum_Remove"), EInputActionValueType::Boolean);
	MakeAction(AdjustAction, TEXT("IA_Museum_Adjust"), EInputActionValueType::Axis2D);
	MakeAction(LookAction, TEXT("IA_Museum_Look"), EInputActionValueType::Axis2D);
	MakeAction(MoveAction, TEXT("IA_Museum_Move"), EInputActionValueType::Axis2D);

	if (InputMapping)
	{
		return; // a designer-made mapping context was assigned
	}

	UInputMappingContext* Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Museum_Default"));
	InputMapping = Mapping;

	auto Map = [Mapping](UInputAction* Action, const TCHAR* KeyName)
	{
		return &Mapping->MapKey(Action, FKey(KeyName));
	};
	auto Swizzle = [Mapping](FEnhancedActionKeyMapping* KeyMapping)
	{
		KeyMapping->Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Mapping)); // X -> Y
	};
	auto Negate = [Mapping](FEnhancedActionKeyMapping* KeyMapping)
	{
		KeyMapping->Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	};

	// Touch controllers (OpenXR key names, the same ones the VR Template uses).
	Map(SelectLeftAction, TEXT("OculusTouch_Left_Trigger_Axis"));
	Map(SelectRightAction, TEXT("OculusTouch_Right_Trigger_Axis"));
	Map(GrabLeftAction, TEXT("OculusTouch_Left_Grip_Axis"));
	Map(GrabRightAction, TEXT("OculusTouch_Right_Grip_Axis"));
	Map(MenuAction, TEXT("OculusTouch_Left_X_Click"));
	Map(MenuAction, TEXT("OculusTouch_Left_Y_Click"));
	Map(MenuAction, TEXT("OculusTouch_Right_B_Click"));
	Map(MenuAction, TEXT("OculusTouch_Left_Menu_Click"));
	const TCHAR* Thumbsticks[] = { TEXT("OculusTouch_Left_Thumbstick_2D"), TEXT("OculusTouch_Right_Thumbstick_2D") };
	for (const TCHAR* Stick : Thumbsticks)
	{
		FEnhancedActionKeyMapping* StickMapping = Map(AdjustAction, Stick);
		StickMapping->Modifiers.Add(NewObject<UInputModifierDeadZone>(Mapping));
	}

	// Desktop testing.
	Map(SelectRightAction, TEXT("LeftMouseButton"));
	Map(GrabRightAction, TEXT("RightMouseButton"));
	Map(MenuAction, TEXT("Tab"));
	Map(RemoveAction, TEXT("Delete"));
	Map(AdjustAction, TEXT("C"));                            // rotate +
	Negate(Map(AdjustAction, TEXT("Z")));                    // rotate -
	Swizzle(Map(AdjustAction, TEXT("E")));                   // bigger
	{
		FEnhancedActionKeyMapping* Smaller = Map(AdjustAction, TEXT("Q"));
		Swizzle(Smaller);
		Negate(Smaller);
	}
	Map(LookAction, TEXT("Mouse2D"));
	Swizzle(Map(MoveAction, TEXT("W")));
	{
		FEnhancedActionKeyMapping* Back = Map(MoveAction, TEXT("S"));
		Swizzle(Back);
		Negate(Back);
	}
	Map(MoveAction, TEXT("D"));
	Negate(Map(MoveAction, TEXT("A")));
}

void AMuseumPawn::ApplyInputMapping()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !InputMapping)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->RemoveMappingContext(InputMapping);
			Subsystem->AddMappingContext(InputMapping, 1);
		}
	}
}

void AMuseumPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	CreateDefaultInput();
	ApplyInputMapping();
}

void AMuseumPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	CreateDefaultInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogAlienMuseum, Error, TEXT("AMuseumPawn needs the Enhanced Input component (Project Settings > Input)"));
		return;
	}

	Input->BindAction(SelectLeftAction, ETriggerEvent::Started, this, &AMuseumPawn::OnSelectLeftStarted);
	Input->BindAction(SelectLeftAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnSelectLeftCompleted);
	Input->BindAction(SelectRightAction, ETriggerEvent::Started, this, &AMuseumPawn::OnSelectRightStarted);
	Input->BindAction(SelectRightAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnSelectRightCompleted);
	Input->BindAction(GrabLeftAction, ETriggerEvent::Started, this, &AMuseumPawn::OnGrabLeftStarted);
	Input->BindAction(GrabLeftAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnGrabLeftCompleted);
	Input->BindAction(GrabRightAction, ETriggerEvent::Started, this, &AMuseumPawn::OnGrabRightStarted);
	Input->BindAction(GrabRightAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnGrabRightCompleted);
	Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AMuseumPawn::OnMenu);
	Input->BindAction(RemoveAction, ETriggerEvent::Started, this, &AMuseumPawn::OnRemove);
	Input->BindAction(AdjustAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnAdjust);
	Input->BindAction(AdjustAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnAdjustCompleted);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnLook);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnMove);
}

void AMuseumPawn::OnSelectLeftStarted(const FInputActionValue&) { LeftHand->SetControllerSelect(true); }
void AMuseumPawn::OnSelectLeftCompleted(const FInputActionValue&) { LeftHand->SetControllerSelect(false); }
void AMuseumPawn::OnSelectRightStarted(const FInputActionValue&) { RightHand->SetControllerSelect(true); }
void AMuseumPawn::OnSelectRightCompleted(const FInputActionValue&) { RightHand->SetControllerSelect(false); }
void AMuseumPawn::OnGrabLeftStarted(const FInputActionValue&) { LeftHand->SetControllerGrab(true); }
void AMuseumPawn::OnGrabLeftCompleted(const FInputActionValue&) { LeftHand->SetControllerGrab(false); }
void AMuseumPawn::OnGrabRightStarted(const FInputActionValue&) { RightHand->SetControllerGrab(true); }
void AMuseumPawn::OnGrabRightCompleted(const FInputActionValue&) { RightHand->SetControllerGrab(false); }

void AMuseumPawn::OnMenu(const FInputActionValue&)
{
	if (Mode != EMuseumPawnMode::Default)
	{
		CancelPlacement();
	}
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->ToggleCollectionPanel();
	}
}

void AMuseumPawn::OnRemove(const FInputActionValue&)
{
	for (UMuseumHandInteractor* Hand : { RightHand.Get(), LeftHand.Get() })
	{
		if (AAlienChamber* Chamber = Cast<AAlienChamber>(Hand->GetPointerHit().Actor.Get()))
		{
			PressRemove(Chamber);
			return;
		}
	}
}

void AMuseumPawn::PressRemove(AAlienChamber* Chamber)
{
	AMuseumDirector* Director = GetDirector();
	if (!Chamber || !Director)
	{
		return;
	}
	if (!Chamber->PressRemoveButton())
	{
		Director->SetStatusText(TEXT("Press the red X again to remove this chamber and its alien."));
		return;
	}
	// Let go of everything that belongs to it first.
	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		if (GetGrab(Hand).Chamber.Get() == Chamber)
		{
			EndGrab(Hand);
		}
		if (GetResize(Hand).Chamber.Get() == Chamber)
		{
			EndResize(Hand);
		}
		const AAlienCharacter* Held = GetAlienGrab(Hand).Alien.Get();
		if (Held && Held->GetHomeChamber() == Chamber)
		{
			EndAlienGrab(Hand);
		}
	}
	Director->RemoveChamber(Chamber);
	Director->SetStatusText(TEXT("Chamber removed."));
}

AAlienChamber* AMuseumPawn::FindRemoveButton(const UMuseumHandInteractor* Hand) const
{
	// Under the ray...
	const FMuseumPointerHit& Hit = Hand->GetPointerHit();
	AAlienChamber* Chamber = Cast<AAlienChamber>(Hit.Actor.Get());
	if (Chamber && Chamber->IsRemoveButton(Hit.Component.Get()))
	{
		return Chamber;
	}
	// ... or pinched right at it (hand tracking).
	if (const AMuseumDirector* Director = GetDirector())
	{
		for (AAlienChamber* Candidate : Director->GetChambers())
		{
			if (Candidate->IsRemoveButtonNear(Hand->GetGrabLocation(), NearGrabDistance * 0.75f))
			{
				return Candidate;
			}
		}
	}
	return nullptr;
}

void AMuseumPawn::OnAdjust(const FInputActionValue& Value)
{
	AdjustInput = Value.Get<FVector2D>();
}

void AMuseumPawn::OnAdjustCompleted(const FInputActionValue&)
{
	AdjustInput = FVector2D::ZeroVector;
}

void AMuseumPawn::OnLook(const FInputActionValue& Value)
{
	if (bDesktopMode)
	{
		const FVector2D Look = Value.Get<FVector2D>();
		AddControllerYawInput(Look.X);
		AddControllerPitchInput(-Look.Y);
	}
}

void AMuseumPawn::OnMove(const FInputActionValue& Value)
{
	if (bDesktopMode)
	{
		const FVector2D Move = Value.Get<FVector2D>();
		const FRotator Flat(0.f, GetControlRotation().Yaw, 0.f);
		const FVector Delta = (Flat.Vector() * Move.Y + FRotationMatrix(Flat).GetScaledAxis(EAxis::Y) * Move.X) * DesktopMoveSpeed * GetWorld()->GetDeltaSeconds();
		AddActorWorldOffset(Delta);
	}
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

AMuseumDirector* AMuseumPawn::GetDirector() const
{
	return AMuseumDirector::Get(this);
}

FVector AMuseumPawn::GetHeadLocation() const
{
	return Camera->GetComponentLocation();
}

FRotator AMuseumPawn::GetHeadRotation() const
{
	return Camera->GetComponentRotation();
}

AMuseumPawn::FHandGrab& AMuseumPawn::GetGrab(const UMuseumHandInteractor* Hand)
{
	return Hand == LeftHand ? LeftGrab : RightGrab;
}

UMuseumHandInteractor* AMuseumPawn::GetOtherHand(const UMuseumHandInteractor* Hand) const
{
	return Hand == LeftHand ? RightHand.Get() : LeftHand.Get();
}

UMuseumHandInteractor* AMuseumPawn::GetPointingHand() const
{
	// Prefer the right hand; fall back to the left one.
	if (RightHand->IsTracked())
	{
		return RightHand;
	}
	return LeftHand->IsTracked() ? LeftHand.Get() : nullptr;
}

AAlienChamber* AMuseumPawn::FindNearestChamber(const FVector& Location, float MaxDistance) const
{
	AAlienChamber* Best = nullptr;
	float BestDistance = MaxDistance;
	for (TActorIterator<AAlienChamber> It(GetWorld()); It; ++It)
	{
		const float Distance = It->DistanceToChamber(Location);
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

// ---------------------------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------------------------

void AMuseumPawn::SetMode(EMuseumPawnMode NewMode)
{
	Mode = NewMode;
	if (Mode == EMuseumPawnMode::Default)
	{
		PendingAlien = nullptr;
		PlacementGhost->SetVisibility(false);
		SetPlacementTarget(nullptr);
	}
}

void AMuseumPawn::BeginPlaceChamber()
{
	SetMode(EMuseumPawnMode::PlacingChamber);
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->SetStatusText(TEXT("Point at the floor or a table and pull the trigger / pinch to place a chamber."));
	}
}

void AMuseumPawn::BeginPlaceAlien(UAlienDataAsset* Alien)
{
	if (!Alien)
	{
		return;
	}
	PendingAlien = Alien;
	SetMode(EMuseumPawnMode::PlacingAlien);
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->SetStatusText(FString::Printf(TEXT("Point at a chamber (or empty floor) to place %s."), *Alien->DisplayName.ToString()));
	}
}

void AMuseumPawn::CancelPlacement()
{
	SetMode(EMuseumPawnMode::Default);
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->SetStatusText(TEXT("Choose an alien, then point at a chamber."));
	}
}

void AMuseumPawn::SetPlacementTarget(AAlienChamber* Chamber)
{
	AAlienChamber* Previous = PlacementTarget.Get();
	if (Previous == Chamber)
	{
		return;
	}
	if (Previous)
	{
		Previous->SetHighlighted(false);
	}
	PlacementTarget = Chamber;
	if (Chamber)
	{
		Chamber->SetHighlighted(true, true);
	}
}

// ---------------------------------------------------------------------------------------------
// Select / grab
// ---------------------------------------------------------------------------------------------

void AMuseumPawn::HandleSelect(UMuseumHandInteractor* Hand, bool bPressed)
{
	if (!bPressed)
	{
		// Let go before the hold time on an alien: a click - its case's info panel.
		FAlienPress& Press = GetAlienPress(Hand);
		AAlienCharacter* Clicked = Press.Alien.Get();
		Press = FAlienPress();
		if (Clicked)
		{
			if (AAlienChamber* Chamber = Clicked->GetHomeChamber())
			{
				Chamber->ToggleInfoPanel();
			}
			return;
		}
		const FHandAlien& Held = GetAlienGrab(Hand);
		if (Held.bActive && Held.bFromSelect)
		{
			EndAlienGrab(Hand);
			return;
		}
		const FHandResize& Resize = GetResize(Hand);
		if (Resize.Chamber.IsValid() && Resize.bFromSelect)
		{
			EndResize(Hand);
			return;
		}
		const FHandGrab& Grab = GetGrab(Hand);
		if (Grab.Chamber.IsValid() && Grab.bFromSelect)
		{
			EndGrab(Hand);
		}
		return;
	}

	// Holding an alien with the grip: the trigger does nothing.
	if (GetAlienGrab(Hand).bActive)
	{
		return;
	}

	// A chamber's resize handle (at the hand or under the ray): drag it.
	if (Mode == EMuseumPawnMode::Default && TryBeginResize(Hand, true))
	{
		return;
	}

	// A chamber's red X: remove the chamber (asks for a second press).
	if (AAlienChamber* ToRemove = FindRemoveButton(Hand))
	{
		if (Mode != EMuseumPawnMode::Default)
		{
			SetMode(EMuseumPawnMode::Default);
		}
		PressRemove(ToRemove);
		return;
	}

	// An alien: right at the hand it is picked up at once; by ray a quick click shows its case's
	// info panel and holding picks it up (UpdateAlienPresses).
	if (Mode == EMuseumPawnMode::Default)
	{
		bool bNear = false;
		if (AAlienCharacter* Alien = FindAlienToGrab(Hand, bNear))
		{
			if (bNear)
			{
				if (BeginAlienGrab(Hand, Alien, true, true))
				{
					return;
				}
			}
			else
			{
				FAlienPress& Press = GetAlienPress(Hand);
				Press.Alien = Alien;
				Press.StartTime = GetWorld()->GetTimeSeconds();
				return;
			}
		}
	}

	// Chamber right at the hand: always a grab.
	if (Mode == EMuseumPawnMode::Default && FindNearestChamber(Hand->GetGrabLocation(), NearGrabDistance))
	{
		TryBeginGrab(Hand, true);
		return;
	}

	// UI (panel buttons, cards) works in every mode.
	const FMuseumPointerHit& Hit = Hand->GetPointerHit();
	AActor* HitActor = Hit.Actor.Get();
	IMuseumInteractable* Interactable = Cast<IMuseumInteractable>(HitActor);
	if (Interactable && !Cast<AAlienChamber>(HitActor))
	{
		Interactable->OnPointerSelect(Hit.Component.Get(), this);
		return;
	}

	if (Mode != EMuseumPawnMode::Default)
	{
		ConfirmPlacement();
		return;
	}

	// Chamber at the end of the ray: grab (a quick click toggles its info panel instead).
	TryBeginGrab(Hand, true);
}

void AMuseumPawn::HandleGrab(UMuseumHandInteractor* Hand, bool bPressed)
{
	if (bPressed)
	{
		if (GetAlienGrab(Hand).bActive)
		{
			return; // already holding an alien with the trigger / pinch
		}
		if (Mode == EMuseumPawnMode::Default && TryBeginResize(Hand, false))
		{
			return;
		}
		// An alien at the hand or under the ray: take it out of its case.
		if (Mode == EMuseumPawnMode::Default)
		{
			bool bNear = false;
			AAlienCharacter* Alien = FindAlienToGrab(Hand, bNear);
			if (Alien && BeginAlienGrab(Hand, Alien, false, bNear))
			{
				return;
			}
		}
		TryBeginGrab(Hand, false);
	}
	else
	{
		const FHandAlien& Held = GetAlienGrab(Hand);
		if (Held.bActive && !Held.bFromSelect)
		{
			EndAlienGrab(Hand);
			return;
		}
		const FHandResize& Resize = GetResize(Hand);
		if (Resize.Chamber.IsValid() && !Resize.bFromSelect)
		{
			EndResize(Hand);
			return;
		}
		const FHandGrab& Grab = GetGrab(Hand);
		if (Grab.Chamber.IsValid() && !Grab.bFromSelect)
		{
			EndGrab(Hand);
		}
	}
}

AMuseumPawn::FHandResize& AMuseumPawn::GetResize(const UMuseumHandInteractor* Hand)
{
	return Hand == LeftHand ? LeftResize : RightResize;
}

bool AMuseumPawn::TryBeginResize(UMuseumHandInteractor* Hand, bool bFromSelect)
{
	FHandResize& Resize = GetResize(Hand);
	if (Resize.Chamber.IsValid() || GetGrab(Hand).Chamber.IsValid())
	{
		return false;
	}

	AAlienChamber* Chamber = nullptr;
	UPrimitiveComponent* Handle = nullptr;
	bool bNear = false;

	// Hand tracking: pinching right at a handle knob.
	if (AMuseumDirector* Director = GetDirector())
	{
		for (AAlienChamber* Candidate : Director->GetChambers())
		{
			if (UPrimitiveComponent* Near = Candidate->FindResizeHandleNear(Hand->GetGrabLocation(), NearGrabDistance * 0.75f))
			{
				Chamber = Candidate;
				Handle = Near;
				bNear = true;
				break;
			}
		}
	}
	// Otherwise the handle under the ray.
	if (!Handle)
	{
		const FMuseumPointerHit& Hit = Hand->GetPointerHit();
		Chamber = Cast<AAlienChamber>(Hit.Actor.Get());
		Handle = Hit.Component.Get();
		if (!Chamber || Chamber->GetResizeAxis(Handle) == EChamberResizeAxis::None)
		{
			return false;
		}
		Resize.RayDistance = Hit.Distance;
	}
	if (Chamber->IsBeingGrabbed() || Chamber->IsBeingResized())
	{
		return false;
	}

	if (Mode != EMuseumPawnMode::Default)
	{
		SetMode(EMuseumPawnMode::Default);
	}
	Resize.Chamber = Chamber;
	Resize.Handle = Handle;
	Resize.bNear = bNear;
	Resize.bFromSelect = bFromSelect;
	Resize.Direction = Chamber->GetResizeDirection(Handle);
	Resize.StartPoint = GetResizePoint(Hand, Resize);
	Chamber->BeginResize(Handle);
	return true;
}

FVector AMuseumPawn::GetResizePoint(const UMuseumHandInteractor* Hand, const FHandResize& Resize) const
{
	// Ray drags keep the grabbed point at the same distance along the ray, like carrying a chamber.
	return Resize.bNear ? Hand->GetGrabLocation() : Hand->GetAimOrigin() + Hand->GetAimDirection() * Resize.RayDistance;
}

void AMuseumPawn::UpdateResizes()
{
	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		FHandResize& Resize = GetResize(Hand);
		AAlienChamber* Chamber = Resize.Chamber.Get();
		if (!Chamber)
		{
			continue;
		}
		if (!Hand->IsTracked())
		{
			EndResize(Hand);
			continue;
		}
		const FVector Point = GetResizePoint(Hand, Resize);
		Chamber->UpdateResize(FVector::DotProduct(Point - Resize.StartPoint, Resize.Direction));
		if (!Resize.bNear)
		{
			Hand->SetLaserOverride(Point, ValidColor);
		}
	}
}

void AMuseumPawn::EndResize(UMuseumHandInteractor* Hand)
{
	FHandResize& Resize = GetResize(Hand);
	if (AAlienChamber* Chamber = Resize.Chamber.Get())
	{
		Chamber->EndResize();
	}
	Resize = FHandResize();
}

// ---------------------------------------------------------------------------------------------
// Holding an alien
// ---------------------------------------------------------------------------------------------

AMuseumPawn::FHandAlien& AMuseumPawn::GetAlienGrab(const UMuseumHandInteractor* Hand)
{
	return Hand == LeftHand ? LeftAlien : RightAlien;
}

AMuseumPawn::FAlienPress& AMuseumPawn::GetAlienPress(const UMuseumHandInteractor* Hand)
{
	return Hand == LeftHand ? LeftPress : RightPress;
}

AAlienCharacter* AMuseumPawn::FindAlienNear(const FVector& Location, float MaxDistance) const
{
	AAlienCharacter* Best = nullptr;
	float BestDistance = MaxDistance;
	for (TActorIterator<AAlienCharacter> It(GetWorld()); It; ++It)
	{
		AAlienCharacter* Alien = *It;
		const AAlienChamber* Chamber = Alien->GetHomeChamber();
		if ((Alien->IsHeld() && !Alien->IsExamined()) || (Chamber && (Chamber->IsBeingGrabbed() || Chamber->IsBeingResized())))
		{
			continue; // riding in a carried case, flying home, or its case is being resized
		}
		const UCapsuleComponent* Capsule = Alien->GetCapsuleComponent();
		const FVector Center = Capsule->GetComponentLocation();
		const FVector Axis = Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere();
		const FVector Closest = FMath::ClosestPointOnSegment(Location, Center - Axis, Center + Axis);
		const float Distance = FMath::Max(0.f, static_cast<float>(FVector::Dist(Location, Closest)) - Capsule->GetScaledCapsuleRadius());
		if (Distance <= BestDistance)
		{
			Best = Alien;
			BestDistance = Distance;
		}
	}
	return Best;
}

AAlienCharacter* AMuseumPawn::FindAlienOnRay(const UMuseumHandInteractor* Hand, FVector* OutHitLocation) const
{
	// The ray stops on the case's pointer box; look through it for the alien inside.
	const FMuseumPointerHit& Hit = Hand->GetPointerHit();
	const AAlienChamber* Chamber = Cast<AAlienChamber>(Hit.Actor.Get());
	if (!Chamber || Chamber->GetResizeAxis(Hit.Component.Get()) != EChamberResizeAxis::None || Chamber->IsRemoveButton(Hit.Component.Get())
		|| Chamber->IsBeingGrabbed() || Chamber->IsBeingResized())
	{
		return nullptr;
	}
	AAlienCharacter* Alien = Chamber->GetOccupant();
	if (!Alien || Alien->IsHeld())
	{
		return nullptr;
	}
	// Closest approach of the ray to the capsule's axis, a little wider than the body so a small
	// alien is easy to point at.
	const UCapsuleComponent* Capsule = Alien->GetCapsuleComponent();
	const FVector Center = Capsule->GetComponentLocation();
	const FVector Axis = Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere();
	const FVector Start = Hand->GetAimOrigin();
	const FVector End = Start + Hand->GetAimDirection() * Hand->MaxPointerDistance;
	FVector OnRay, OnAxis;
	FMath::SegmentDistToSegmentSafe(Start, End, Center - Axis, Center + Axis, OnRay, OnAxis);
	if (FVector::Dist(OnRay, OnAxis) > Capsule->GetScaledCapsuleRadius() + 3.f)
	{
		return nullptr;
	}
	if (OutHitLocation)
	{
		*OutHitLocation = OnRay;
	}
	return Alien;
}

AAlienCharacter* AMuseumPawn::FindAlienToGrab(const UMuseumHandInteractor* Hand, bool& bOutNear, FVector* OutHitLocation) const
{
	// Right at the hand first; a case at the hand wins over an alien further along the ray.
	bOutNear = true;
	if (AAlienCharacter* Alien = FindAlienNear(Hand->GetGrabLocation(), NearAlienDistance))
	{
		if (OutHitLocation)
		{
			*OutHitLocation = Alien->GetActorLocation();
		}
		return Alien;
	}
	bOutNear = false;
	if (FindNearestChamber(Hand->GetGrabLocation(), NearGrabDistance))
	{
		return nullptr;
	}
	return FindAlienOnRay(Hand, OutHitLocation);
}

bool AMuseumPawn::BeginAlienGrab(UMuseumHandInteractor* Hand, AAlienCharacter* Alien, bool bFromSelect, bool bNear)
{
	FHandAlien& Grab = GetAlienGrab(Hand);
	if (!Alien || Grab.bActive || GetGrab(Hand).Chamber.IsValid() || GetResize(Hand).Chamber.IsValid()
		|| (Alien->IsHeld() && !Alien->IsExamined()))
	{
		return false;
	}
	if (const AAlienChamber* Chamber = Alien->GetHomeChamber())
	{
		if (Chamber->IsBeingGrabbed() || Chamber->IsBeingResized())
		{
			return false;
		}
	}
	if (Mode != EMuseumPawnMode::Default)
	{
		SetMode(EMuseumPawnMode::Default);
	}

	const FQuat HandRotation = Hand->GetGrabRotation();
	const FHandAlien& Other = GetAlienGrab(GetOtherHand(Hand));
	if (Other.bActive && Other.Alien.Get() == Alien)
	{
		// The second hand on the same alien: spread the hands to zoom it.
		Grab = Other;
		bTwoHandAlien = true;
		TwoHandAlienStartDistance = FMath::Max(5.f, static_cast<float>(FVector::Dist(LeftHand->GetGrabLocation(), RightHand->GetGrabLocation())));
		TwoHandAlienStartZoom = Other.Zoom;
	}
	else
	{
		Alien->BeginExamine();
		Grab = FHandAlien();
		FVector Hold = Alien->GetActorLocation();
		FQuat Facing = Alien->GetActorQuat();
		if (!bNear)
		{
			// From afar it comes to the hand and turns its face to you.
			Hold = Hand->GetGrabLocation() + Hand->GetAimDirection() * (Alien->GetCapsuleComponent()->GetScaledCapsuleRadius() + 8.f);
			Facing = FRotator(0.f, (GetHeadLocation() - Hold).Rotation().Yaw, 0.f).Quaternion();
		}
		Grab.LocalOffset = HandRotation.Inverse().RotateVector(Hold - Hand->GetGrabLocation());
		Grab.LocalRotation = HandRotation.Inverse() * Facing;
	}
	Grab.Alien = Alien;
	Grab.bFromSelect = bFromSelect;
	Grab.bActive = true;
	Hand->SetLaserEnabled(false);
	Alien->SetTargeted(false);
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->SetStatusText(TEXT("Turn your hand to look around it. Thumbstick: spin and zoom. Let go over its case to put it back."));
	}
	return true;
}

void AMuseumPawn::EndAlienGrab(UMuseumHandInteractor* Hand)
{
	FHandAlien& Grab = GetAlienGrab(Hand);
	AAlienCharacter* Alien = Grab.Alien.Get();
	Grab = FHandAlien();
	Hand->SetLaserEnabled(true);
	bTwoHandAlien = false;
	if (!Alien)
	{
		return;
	}

	// The other hand still holds it: carry on from there.
	UMuseumHandInteractor* OtherHand = GetOtherHand(Hand);
	FHandAlien& Other = GetAlienGrab(OtherHand);
	if (Other.bActive && Other.Alien.Get() == Alien)
	{
		const FQuat OtherRotation = OtherHand->GetGrabRotation();
		Other.LocalOffset = OtherRotation.Inverse().RotateVector(Alien->GetActorLocation() - OtherHand->GetGrabLocation()) / FMath::Max(Other.Zoom, 0.01f);
		Other.LocalRotation = OtherRotation.Inverse() * Alien->GetActorQuat();
		Other.Spin = 0.f;
		return;
	}
	Alien->EndExamine();
}

void AMuseumPawn::UpdateAlienPresses()
{
	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		FAlienPress& Press = GetAlienPress(Hand);
		AAlienCharacter* Alien = Press.Alien.Get();
		if (!Alien || !Hand->IsSelectPressed())
		{
			Press = FAlienPress();
		}
		else if (GetWorld()->GetTimeSeconds() - Press.StartTime >= ClickMaxTime)
		{
			Press = FAlienPress();
			BeginAlienGrab(Hand, Alien, true, false); // held long enough: pick it up
		}
	}
}

void AMuseumPawn::UpdateAlienGrabs(float DeltaSeconds)
{
	// Two hands on one alien: it sits between them, spreading them zooms.
	if (bTwoHandAlien)
	{
		AAlienCharacter* Alien = LeftAlien.Alien.Get();
		if (Alien && Alien->IsExamined() && RightAlien.Alien.Get() == Alien && LeftHand->IsTracked() && RightHand->IsTracked())
		{
			const FVector L = LeftHand->GetGrabLocation();
			const FVector R = RightHand->GetGrabLocation();
			const float Spread = FMath::Max(5.f, static_cast<float>(FVector::Dist(L, R)));
			const float Zoom = FMath::Clamp(TwoHandAlienStartZoom * Spread / TwoHandAlienStartDistance, ExamineZoomRange.X, ExamineZoomRange.Y);
			LeftAlien.Zoom = RightAlien.Zoom = Zoom;
			Alien->SetExamineTarget((L + R) * 0.5f, Alien->GetActorQuat(), Zoom);
			return;
		}
		bTwoHandAlien = false;
	}

	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		FHandAlien& Grab = GetAlienGrab(Hand);
		if (!Grab.bActive)
		{
			continue;
		}
		AAlienCharacter* Alien = Grab.Alien.Get();
		if (!Alien || !Alien->IsExamined() || !Hand->IsTracked())
		{
			EndAlienGrab(Hand); // its case was removed, or the hand was lost
			continue;
		}
		// Thumbstick / Z C Q E: X spins it, Y zooms.
		if (FMath::Abs(AdjustInput.X) > 0.2f)
		{
			Grab.Spin += AdjustInput.X * ExamineSpinSpeed * DeltaSeconds;
		}
		if (FMath::Abs(AdjustInput.Y) > 0.2f)
		{
			Grab.Zoom = FMath::Clamp(Grab.Zoom * (1.f + AdjustInput.Y * ExamineZoomSpeed * DeltaSeconds), ExamineZoomRange.X, ExamineZoomRange.Y);
		}
		// Rigidly in the hand; zooming scales it around the hand.
		const FQuat HandRotation = Hand->GetGrabRotation();
		const FVector Target = Hand->GetGrabLocation() + HandRotation.RotateVector(Grab.LocalOffset * Grab.Zoom);
		const FQuat Rotation = HandRotation * Grab.LocalRotation * FQuat(FVector::UpVector, FMath::DegreesToRadians(Grab.Spin));
		Alien->SetExamineTarget(Target, Rotation, Grab.Zoom);
	}
}

void AMuseumPawn::UpdateAlienTargets()
{
	// The alien each free hand would pick up: a gold ring at its feet and a gold laser to it.
	UMuseumHandInteractor* Hands[2] = { LeftHand.Get(), RightHand.Get() };
	AAlienCharacter* NewTargets[2] = { nullptr, nullptr };
	for (int32 i = 0; i < 2; ++i)
	{
		UMuseumHandInteractor* Hand = Hands[i];
		if (Mode != EMuseumPawnMode::Default || !Hand->IsTracked() || GetGrab(Hand).Chamber.IsValid()
			|| GetResize(Hand).Chamber.IsValid() || GetAlienGrab(Hand).bActive)
		{
			continue;
		}
		bool bNear = false;
		FVector Point = FVector::ZeroVector;
		AAlienCharacter* Alien = FindAlienToGrab(Hand, bNear, &Point);
		if (!Alien || Alien->IsExamined())
		{
			continue;
		}
		NewTargets[i] = Alien;
		if (!bNear)
		{
			Hand->SetLaserOverride(Point, AlienTargetColor);
		}
	}
	for (int32 i = 0; i < 2; ++i)
	{
		AAlienCharacter* Old = TargetedAlien[i].Get();
		if (Old && Old != NewTargets[0] && Old != NewTargets[1])
		{
			Old->SetTargeted(false);
		}
	}
	for (int32 i = 0; i < 2; ++i)
	{
		if (NewTargets[i])
		{
			NewTargets[i]->SetTargeted(true);
		}
		TargetedAlien[i] = NewTargets[i];
	}
}

bool AMuseumPawn::TryBeginGrab(UMuseumHandInteractor* Hand, bool bFromSelect)
{
	FHandGrab& Grab = GetGrab(Hand);
	if (Grab.Chamber.IsValid() || GetResize(Hand).Chamber.IsValid())
	{
		return false;
	}

	AAlienChamber* Target = FindNearestChamber(Hand->GetGrabLocation(), NearGrabDistance);
	if (!Target)
	{
		Target = Cast<AAlienChamber>(Hand->GetPointerHit().Actor.Get());
	}
	if (!Target || Target->IsBeingResized())
	{
		return false;
	}

	if (Mode != EMuseumPawnMode::Default)
	{
		SetMode(EMuseumPawnMode::Default);
	}

	Grab.bFromSelect = bFromSelect;
	Grab.StartTime = GetWorld()->GetTimeSeconds();
	Grab.StartHandLocation = Hand->GetGrabLocation();
	Grab.bMoved = false;
	ResetOneHandGrab(Hand, Target);
	Hand->SetLaserEnabled(false);

	const FHandGrab& Other = GetGrab(GetOtherHand(Hand));
	if (Other.Chamber.Get() == Target)
	{
		BeginTwoHandGrab(Target);
	}
	else
	{
		Target->BeginGrab();
	}
	return true;
}

void AMuseumPawn::ResetOneHandGrab(UMuseumHandInteractor* Hand, AAlienChamber* Chamber)
{
	FHandGrab& Grab = GetGrab(Hand);
	const float Yaw = Hand->GetCarryYaw();
	Grab.Chamber = Chamber;
	Grab.LocalOffset = FRotator(0.f, -Yaw, 0.f).RotateVector(Chamber->GetActorLocation() - Hand->GetGrabLocation());
	Grab.YawOffset = Chamber->GetActorRotation().Yaw - Yaw;
	Grab.TargetScale = Chamber->GetChamberScale();
}

void AMuseumPawn::BeginTwoHandGrab(AAlienChamber* Chamber)
{
	const FVector L = LeftHand->GetGrabLocation();
	const FVector R = RightHand->GetGrabLocation();
	const FVector Mid = (L + R) * 0.5f;
	const FVector Between = R - L;

	bTwoHand = true;
	TwoHandStartDistance = FMath::Max(5.f, static_cast<float>(Between.Size()));
	TwoHandStartScale = Chamber->GetChamberScale();
	TwoHandStartYaw = Between.Rotation().Yaw;
	TwoHandChamberStartYaw = Chamber->GetActorRotation().Yaw;
	TwoHandOffset = FRotator(0.f, -TwoHandStartYaw, 0.f).RotateVector(Chamber->GetActorLocation() - Mid);
	LeftGrab.bMoved = RightGrab.bMoved = true; // never a click
}

void AMuseumPawn::EndGrab(UMuseumHandInteractor* Hand)
{
	FHandGrab& Grab = GetGrab(Hand);
	AAlienChamber* Chamber = Grab.Chamber.Get();
	const bool bWasClick = Grab.bFromSelect && !Grab.bMoved && (GetWorld()->GetTimeSeconds() - Grab.StartTime) <= ClickMaxTime;
	Grab = FHandGrab();
	Hand->SetLaserEnabled(true);
	if (!Chamber)
	{
		return;
	}

	// Other hand still holds it: continue one-handed.
	UMuseumHandInteractor* Other = GetOtherHand(Hand);
	if (GetGrab(Other).Chamber.Get() == Chamber)
	{
		bTwoHand = false;
		ResetOneHandGrab(Other, Chamber);
		return;
	}

	bTwoHand = false;
	Chamber->EndGrab();
	if (bWasClick)
	{
		Chamber->ToggleInfoPanel();
	}
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->SettleChamber(Chamber);
	}
}

void AMuseumPawn::UpdateGrabs(float DeltaSeconds)
{
	// Two hands on the same chamber: move + rotate + scale around the midpoint.
	if (bTwoHand)
	{
		AAlienChamber* Chamber = LeftGrab.Chamber.Get();
		if (Chamber && RightGrab.Chamber.Get() == Chamber)
		{
			const FVector L = LeftHand->GetGrabLocation();
			const FVector R = RightHand->GetGrabLocation();
			const FVector Between = R - L;
			const float Distance = FMath::Max(5.f, static_cast<float>(Between.Size()));
			const float Scale = FMath::Clamp(TwoHandStartScale * Distance / TwoHandStartDistance, Chamber->ScaleRange.X, Chamber->ScaleRange.Y);
			const float Yaw = Between.Rotation().Yaw;
			const FVector Target = (L + R) * 0.5f + FRotator(0.f, Yaw, 0.f).RotateVector(TwoHandOffset * (Scale / TwoHandStartScale));
			Chamber->UpdateGrab(Target, TwoHandChamberStartYaw + (Yaw - TwoHandStartYaw), Scale);
			return;
		}
		bTwoHand = false;
	}

	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		FHandGrab& Grab = GetGrab(Hand);
		AAlienChamber* Chamber = Grab.Chamber.Get();
		if (!Chamber)
		{
			continue;
		}
		if (!Hand->IsTracked())
		{
			EndGrab(Hand);
			continue;
		}

		// Thumbstick / keys: rotate (X) and resize (Y).
		if (FMath::Abs(AdjustInput.X) > 0.2f)
		{
			Grab.YawOffset += AdjustInput.X * RotateSpeed * DeltaSeconds;
			Grab.bMoved = true;
		}
		if (FMath::Abs(AdjustInput.Y) > 0.2f)
		{
			Grab.TargetScale = FMath::Clamp(Grab.TargetScale * (1.f + AdjustInput.Y * ScaleSpeed * DeltaSeconds), Chamber->ScaleRange.X, Chamber->ScaleRange.Y);
			Grab.bMoved = true;
		}

		const float Yaw = Hand->GetCarryYaw();
		const FVector Target = Hand->GetGrabLocation() + FRotator(0.f, Yaw, 0.f).RotateVector(Grab.LocalOffset);
		Chamber->UpdateGrab(Target, Yaw + Grab.YawOffset, Grab.TargetScale);

		if (!Grab.bMoved && FVector::Dist(Hand->GetGrabLocation(), Grab.StartHandLocation) > ClickMaxMove)
		{
			Grab.bMoved = true;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Per-frame: hover, placement preview, menu gesture
// ---------------------------------------------------------------------------------------------

void AMuseumPawn::UpdateHover()
{
	UMuseumHandInteractor* Hands[2] = { LeftHand.Get(), RightHand.Get() };
	for (int32 i = 0; i < 2; ++i)
	{
		UMuseumHandInteractor* Hand = Hands[i];
		AActor* NewActor = nullptr;
		UPrimitiveComponent* NewComponent = nullptr;
		if (Hand->IsTracked() && !GetGrab(Hand).Chamber.IsValid() && !GetResize(Hand).Chamber.IsValid() && !GetAlienGrab(Hand).bActive)
		{
			NewActor = Hand->GetPointerHit().Actor.Get();
			NewComponent = Hand->GetPointerHit().Component.Get();
		}
		if (!Cast<IMuseumInteractable>(NewActor))
		{
			NewActor = nullptr;
			NewComponent = nullptr;
		}

		if (NewActor != HoveredActor[i].Get() || NewComponent != HoveredComponent[i].Get())
		{
			if (IMuseumInteractable* Old = Cast<IMuseumInteractable>(HoveredActor[i].Get()))
			{
				Old->OnPointerHover(HoveredComponent[i].Get(), false);
			}
			if (IMuseumInteractable* New = Cast<IMuseumInteractable>(NewActor))
			{
				New->OnPointerHover(NewComponent, true);
			}
			HoveredActor[i] = NewActor;
			HoveredComponent[i] = NewComponent;
		}
	}
}

void AMuseumPawn::UpdatePlacement(float DeltaSeconds)
{
	if (Mode == EMuseumPawnMode::Default)
	{
		return;
	}

	AMuseumDirector* Director = GetDirector();
	UMuseumHandInteractor* Hand = GetPointingHand();
	if (!Director || !Hand)
	{
		PlacementGhost->SetVisibility(false);
		return;
	}

	// Pointing at the UI: let the normal laser show.
	AActor* HitActor = Hand->GetPointerHit().Actor.Get();
	if (Cast<IMuseumInteractable>(HitActor) && !Cast<AAlienChamber>(HitActor))
	{
		PlacementGhost->SetVisibility(false);
		SetPlacementTarget(nullptr);
		bPlacementValid = false;
		return;
	}

	// Placing an alien: a chamber under the ray is the target.
	if (Mode == EMuseumPawnMode::PlacingAlien)
	{
		if (AAlienChamber* Chamber = Cast<AAlienChamber>(HitActor))
		{
			SetPlacementTarget(Chamber);
			PlacementGhost->SetVisibility(false);
			Hand->SetLaserOverride(Hand->GetPointerHit().Location, ValidColor);
			return;
		}
	}
	SetPlacementTarget(nullptr);

	// Otherwise: where a new chamber would go. A real surface under the ray wins; with floating
	// chambers, pointing into open space puts it in mid-air FloatPlacementDistance along the ray.
	const AAlienChamber* Template = Director->GetChamberTemplate();
	const FVector CaseSize = Template ? Template->GetOuterSize() : FVector(70.f, 86.f, 121.f); // depth, width, height
	const float Height = static_cast<float>(CaseSize.Z);
	const FVector Origin = Hand->GetAimOrigin();
	const FVector Direction = Hand->GetAimDirection();

	if (FMath::Abs(AdjustInput.Y) > 0.2f)
	{
		FloatPlacementDistance = FMath::Clamp(FloatPlacementDistance + AdjustInput.Y * FloatDistanceSpeed * DeltaSeconds,
			FloatPlacementRange.X, FloatPlacementRange.Y);
	}

	PlacementHit = Director->GetScene()->RaycastSurface(Origin, Direction, 600.f);
	bPlacementInAir = false;
	bool bShowGhost = false;
	FVector LaserEnd = Origin + Direction * 300.f;
	if (PlacementHit.IsPlaceable() || (PlacementHit.bHit && !Director->bChambersFloat))
	{
		PlacementLocation = PlacementHit.Location;
		LaserEnd = PlacementHit.Location;
		bShowGhost = true;
	}
	else if (Director->bChambersFloat)
	{
		// The ray points at the middle of the floating chamber; it never goes below the floor.
		LaserEnd = Origin + Direction * FloatPlacementDistance;
		PlacementLocation = LaserEnd - FVector(0.f, 0.f, Height * 0.5f);
		PlacementLocation.Z = FMath::Max(PlacementLocation.Z, Director->GetScene()->GetFloorZ());
		bPlacementInAir = true;
		bShowGhost = true;
	}

	// The new chamber will face the viewer.
	const float Yaw = (GetHeadLocation() - PlacementLocation).Rotation().Yaw;
	bPlacementSpotOk = bPlacementInAir || PlacementHit.IsPlaceable();
	bPlacementValid = bPlacementSpotOk
		&& Director->CanAddChamber()
		&& Director->IsFootprintFree(PlacementLocation, Yaw, CaseSize + FVector(4.f, 4.f, 0.f), nullptr);

	const FLinearColor Color = bPlacementValid ? ValidColor : InvalidColor;
	Hand->SetLaserOverride(LaserEnd, bShowGhost ? Color : InvalidColor);
	if (!bShowGhost)
	{
		PlacementGhost->SetVisibility(false);
		return;
	}

	// Same shape, size and facing as the chamber that will be spawned.
	UStaticMesh* GhostMesh = (!Template || Template->Shape == EChamberShape::Square) ? GhostCubeMesh.Get() : GhostCylinderMesh.Get();
	if (GhostMesh && PlacementGhost->GetStaticMesh() != GhostMesh)
	{
		PlacementGhost->SetStaticMesh(GhostMesh);
	}
	PlacementGhost->SetVisibility(true);
	PlacementGhost->SetWorldScale3D(CaseSize / 100.f);
	PlacementGhost->SetWorldLocationAndRotation(PlacementLocation + FVector(0.f, 0.f, Height * 0.5f), FRotator(0.f, Yaw, 0.f));
	if (GhostMID)
	{
		GhostMID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
	}
}

void AMuseumPawn::ConfirmPlacement()
{
	AMuseumDirector* Director = GetDirector();
	if (!Director)
	{
		return;
	}

	const FVector Head = GetHeadLocation();
	if (Mode == EMuseumPawnMode::PlacingAlien)
	{
		if (AAlienChamber* Chamber = PlacementTarget.Get())
		{
			Director->PlaceAlienInChamber(Chamber, PendingAlien);
			SetMode(EMuseumPawnMode::Default);
			return;
		}
	}

	if (!bPlacementValid)
	{
		Director->SetStatusText(!Director->CanAddChamber() ? TEXT("The museum is full. Remove a chamber first.")
			: bPlacementSpotOk ? TEXT("Too close to another chamber.")
			: TEXT("Can't place there. Aim at free floor or a table top."));
		return;
	}

	const float Yaw = (Head - PlacementLocation).Rotation().Yaw;
	Director->SpawnChamberAt(PlacementLocation, Yaw, Mode == EMuseumPawnMode::PlacingAlien ? PendingAlien.Get() : nullptr);
	SetMode(EMuseumPawnMode::Default);
	Director->SetStatusText(bPlacementInAir
		? TEXT("Chamber floating in the air. Grab it to move, use two hands to resize.")
		: TEXT("Chamber placed. Grab it to move, use two hands to resize."));
}

void AMuseumPawn::UpdateMenuGesture(float DeltaSeconds)
{
	const bool bHolding = LeftHand->GetSource() == EMuseumHandSource::Hand
		&& LeftHand->IsSelectPressed()
		&& !LeftGrab.Chamber.IsValid()
		&& !LeftResize.Chamber.IsValid()
		&& !LeftAlien.bActive
		&& !LeftPress.Alien.IsValid()
		&& !Cast<IMuseumInteractable>(LeftHand->GetPointerHit().Actor.Get());

	if (!bHolding)
	{
		MenuHoldTimer = 0.f;
		bMenuHoldConsumed = false;
		return;
	}
	if (bMenuHoldConsumed)
	{
		return;
	}
	MenuHoldTimer += DeltaSeconds;
	if (MenuHoldTimer >= MenuHoldTime)
	{
		bMenuHoldConsumed = true;
		OnMenu(FInputActionValue());
	}
}

void AMuseumPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDesktopMode)
	{
		// Desktop testing: the right "hand" is the camera ray.
		RightHand->SetDesktopRay(Camera->GetComponentLocation(), Camera->GetForwardVector());
	}

	UpdateResizes();
	UpdateGrabs(DeltaSeconds);
	UpdateAlienPresses();
	UpdateAlienGrabs(DeltaSeconds);
	UpdateHover();
	UpdateAlienTargets();
	UpdatePlacement(DeltaSeconds);
	UpdateMenuGesture(DeltaSeconds);
}

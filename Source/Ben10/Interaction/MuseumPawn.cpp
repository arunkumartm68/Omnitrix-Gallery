// Alien Museum - the player in mixed reality.

#include "Interaction/MuseumPawn.h"
#include "Core/MuseumAudio.h"
#include "Interaction/MuseumHandInteractor.h"
#include "Aliens/AlienCharacter.h"
#include "Chamber/AlienChamber.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumDirector.h"
#include "Core/MuseumInteractable.h"
#include "MR/MuseumSceneComponent.h"
#include "Omnitrix/OmnitrixWatch.h"
#include "Data/AlienDataAsset.h"
#include "UI/AlienCollectionPanel.h"
#include "Ben10.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
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

	PlacementLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PlacementLabel"));
	PlacementLabel->SetupAttachment(RootComponent);
	PlacementLabel->SetUsingAbsoluteLocation(true);
	PlacementLabel->SetUsingAbsoluteRotation(true);
	PlacementLabel->SetUsingAbsoluteScale(true);
	PlacementLabel->SetHorizontalAlignment(EHTA_Center);
	PlacementLabel->SetVerticalAlignment(EVRTA_TextBottom);
	PlacementLabel->SetWorldSize(7.f);
	PlacementLabel->SetTextRenderColor(FColor(170, 255, 190));
	PlacementLabel->SetCastShadow(false);
	PlacementLabel->SetVisibility(false);

	LeftHand = CreateDefaultSubobject<UMuseumHandInteractor>(TEXT("LeftHand"));
	RightHand = CreateDefaultSubobject<UMuseumHandInteractor>(TEXT("RightHand"));

	// Input as assets (Scripts/create_input_assets.py): on Quest the OpenXR runtime only feeds the
	// Touch controllers' triggers, grips, sticks and buttons to actions of a mapping context that is
	// listed in DefaultInput.ini's Default Mapping Contexts and already loaded when the XR session
	// starts - loading them here (class default object, at start-up) guarantees both.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MappingAsset(TEXT("/Game/AlienMuseum/Input/IMC_Museum.IMC_Museum"));
	static ConstructorHelpers::FObjectFinder<UInputAction> SelectLeftAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_SelectLeft.IA_Museum_SelectLeft"));
	static ConstructorHelpers::FObjectFinder<UInputAction> SelectRightAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_SelectRight.IA_Museum_SelectRight"));
	static ConstructorHelpers::FObjectFinder<UInputAction> GrabLeftAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_GrabLeft.IA_Museum_GrabLeft"));
	static ConstructorHelpers::FObjectFinder<UInputAction> GrabRightAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_GrabRight.IA_Museum_GrabRight"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MenuAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_Menu.IA_Museum_Menu"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RemoveAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_Remove.IA_Museum_Remove"));
	static ConstructorHelpers::FObjectFinder<UInputAction> AdjustAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_Adjust.IA_Museum_Adjust"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_Look.IA_Museum_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_Move.IA_Museum_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> WatchAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_Watch.IA_Museum_Watch"));
	static ConstructorHelpers::FObjectFinder<UInputAction> WatchDialAsset(TEXT("/Game/AlienMuseum/Input/IA_Museum_WatchDial.IA_Museum_WatchDial"));
	InputMapping = MappingAsset.Object;
	SelectLeftAction = SelectLeftAsset.Object;
	SelectRightAction = SelectRightAsset.Object;
	GrabLeftAction = GrabLeftAsset.Object;
	GrabRightAction = GrabRightAsset.Object;
	MenuAction = MenuAsset.Object;
	RemoveAction = RemoveAsset.Object;
	AdjustAction = AdjustAsset.Object;
	LookAction = LookAsset.Object;
	MoveAction = MoveAsset.Object;
	WatchAction = WatchAsset.Object;
	WatchDialAction = WatchDialAsset.Object;
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
	LeftHand->OnMenuPinch.AddUObject(this, &AMuseumPawn::HandleMenuPinch);
	RightHand->OnMenuPinch.AddUObject(this, &AMuseumPawn::HandleMenuPinch);

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

	// The Omnitrix on the left wrist (unfinished: off unless bWearOmnitrix).
	if (!bWearOmnitrix)
	{
		return;
	}
	FActorSpawnParameters WatchSpawn;
	WatchSpawn.Owner = this;
	Watch = GetWorld()->SpawnActor<AOmnitrixWatch>(AOmnitrixWatch::StaticClass(), GetActorTransform(), WatchSpawn);
	if (Watch)
	{
		Watch->Setup(LeftHand, RightHand, Camera);
		Watch->OnTransform.AddDynamic(this, &AMuseumPawn::HandleOmnitrixTransform);
		Watch->OnRevert.AddDynamic(this, &AMuseumPawn::HandleOmnitrixRevert);
	}
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
	MakeAction(WatchAction, TEXT("IA_Museum_Watch"), EInputActionValueType::Boolean);
	MakeAction(WatchDialAction, TEXT("IA_Museum_WatchDial"), EInputActionValueType::Axis1D);

	if (InputMapping)
	{
		return; // the input assets (or a designer-made mapping context) are assigned
	}

	// Fallback when the assets are missing: fine for desktop testing, but on Quest the controllers
	// never reach a mapping context made at run time (only hand tracking would work).
	UE_LOG(LogAlienMuseum, Warning, TEXT("IMC_Museum not found - building the input mapping at run time; Touch controller input will not work in VR (run Scripts/create_input_assets.py)"));
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
	Map(WatchAction, TEXT("OculusTouch_Left_Thumbstick_Click"));
	Map(WatchAction, TEXT("F"));
	Map(WatchDialAction, TEXT("OculusTouch_Left_Thumbstick_X"))->Modifiers.Add(NewObject<UInputModifierDeadZone>(Mapping));
	Map(WatchDialAction, TEXT("MouseWheelAxis"));
	Map(WatchDialAction, TEXT("Period"));
	Negate(Map(WatchDialAction, TEXT("Comma")));
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

	// Trigger and grip: their analog value every frame it is above zero, and a final Completed at zero. (With no
	// trigger on the mapping, Started / Completed alone would fire at the lightest touch and wait for exactly 0.)
	Input->BindAction(SelectLeftAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnSelectLeft);
	Input->BindAction(SelectLeftAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnSelectLeftReleased);
	Input->BindAction(SelectRightAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnSelectRight);
	Input->BindAction(SelectRightAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnSelectRightReleased);
	Input->BindAction(GrabLeftAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnGrabLeft);
	Input->BindAction(GrabLeftAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnGrabLeftReleased);
	Input->BindAction(GrabRightAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnGrabRight);
	Input->BindAction(GrabRightAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnGrabRightReleased);
	Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AMuseumPawn::OnMenu);
	Input->BindAction(RemoveAction, ETriggerEvent::Started, this, &AMuseumPawn::OnRemove);
	Input->BindAction(AdjustAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnAdjust);
	Input->BindAction(AdjustAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnAdjustCompleted);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnLook);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnMove);
	Input->BindAction(WatchAction, ETriggerEvent::Started, this, &AMuseumPawn::OnWatch);
	Input->BindAction(WatchDialAction, ETriggerEvent::Triggered, this, &AMuseumPawn::OnWatchDial);
	Input->BindAction(WatchDialAction, ETriggerEvent::Completed, this, &AMuseumPawn::OnWatchDialCompleted);
}

void AMuseumPawn::OnSelectLeft(const FInputActionValue& Value) { SetLeftSelectAxis(Value.Get<float>()); }
void AMuseumPawn::OnSelectLeftReleased(const FInputActionValue&) { SetLeftSelectAxis(0.f); }

void AMuseumPawn::SetLeftSelectAxis(float Value)
{
	// A press that went to the watch (a slam) stays with it until the trigger is let go.
	if (bLeftSelectToWatch)
	{
		bLeftSelectToWatch = Value > LeftHand->ControllerReleaseThreshold;
		return;
	}
	// The dial is open: the left trigger slams it (the watch hand's pointer clicks nothing meanwhile).
	if (Watch && Watch->IsDialOpen() && !LeftHand->IsControllerSelectHeld() && Value >= LeftHand->ControllerPressThreshold)
	{
		bLeftSelectToWatch = true;
		Watch->Slam();
		return;
	}
	LeftHand->SetControllerSelectAxis(Value);
}

void AMuseumPawn::OnWatch(const FInputActionValue&)
{
	if (Watch)
	{
		Watch->Press();
	}
}

void AMuseumPawn::OnWatchDial(const FInputActionValue& Value)
{
	if (Watch)
	{
		Watch->DialStick(Value.Get<float>());
	}
}

void AMuseumPawn::OnWatchDialCompleted(const FInputActionValue&)
{
	if (Watch)
	{
		Watch->DialStick(0.f);
	}
}

void AMuseumPawn::UpdateWatchHands()
{
	// A hand working the watch up close is busy there: it clicks and grabs nothing else meanwhile. It only
	// becomes busy with nothing in it (reaching for the watch never drops what it carries).
	if (!Watch)
	{
		return;
	}
	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		const bool bWanted = Watch->WantsHand(Hand);
		if (!bWanted)
		{
			Hand->SetBusy(false);
		}
		else if (!Hand->IsBusy() && !Hand->IsSelectPressed() && !Hand->IsGrabPressed() && !GetGrab(Hand).Chamber.IsValid()
			&& !GetAlienGrab(Hand).Alien.IsValid())
		{
			Hand->SetBusy(true);
		}
	}
}

void AMuseumPawn::HandleOmnitrixTransform(UAlienDataAsset* Alien)
{
	// Step 5 brings the alien out life-size; for now the watch keeps the time.
	UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: transformed into %s"), *GetNameSafe(Alien));
}

void AMuseumPawn::HandleOmnitrixRevert(bool bTimedOut)
{
	UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: back to normal (%s)"), bTimedOut ? TEXT("timed out") : TEXT("turned back"));
}
void AMuseumPawn::OnSelectRight(const FInputActionValue& Value) { RightHand->SetControllerSelectAxis(Value.Get<float>()); }
void AMuseumPawn::OnSelectRightReleased(const FInputActionValue&) { RightHand->SetControllerSelectAxis(0.f); }
void AMuseumPawn::OnGrabLeft(const FInputActionValue& Value) { LeftHand->SetControllerGrabAxis(Value.Get<float>()); }
void AMuseumPawn::OnGrabLeftReleased(const FInputActionValue&) { LeftHand->SetControllerGrabAxis(0.f); }
void AMuseumPawn::OnGrabRight(const FInputActionValue& Value) { RightHand->SetControllerGrabAxis(Value.Get<float>()); }
void AMuseumPawn::OnGrabRightReleased(const FInputActionValue&) { RightHand->SetControllerGrabAxis(0.f); }

void AMuseumPawn::OnMenu(const FInputActionValue&)
{
	// A button and the hand gesture at the same moment (or a double press) toggle it once.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastMenuToggleTime < 0.4)
	{
		return;
	}
	LastMenuToggleTime = Now;
	if (Mode != EMuseumPawnMode::Default)
	{
		CancelPlacement();
	}
	if (AMuseumDirector* Director = GetDirector())
	{
		Director->ToggleCollectionPanel();
		UE_LOG(LogAlienMuseum, Log, TEXT("Menu button: collection %s"),
			Director->GetPanel() && Director->GetPanel()->IsPanelVisible() ? TEXT("shown") : TEXT("hidden"));
	}
}

void AMuseumPawn::HandleMenuPinch(UMuseumHandInteractor* Hand)
{
	// Quest's menu gesture: look at your left palm and pinch. (The right palm's pinch is the system's own button.)
	if (Hand == LeftHand)
	{
		OnMenu(FInputActionValue());
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
	UMuseumAudio* Audio = UMuseumAudio::Get(this);
	if (!Chamber->PressRemoveButton())
	{
		if (Audio)
		{
			Audio->PlayAt(TEXT("Case.Arm"), Chamber->GetActorLocation() + FVector(0.f, 0.f, 20.f));
		}
		Director->SetStatusText(TEXT("Press the red X again to remove this chamber and its alien."));
		return;
	}
	if (Audio)
	{
		Audio->PlayAt(TEXT("Case.Remove"), Chamber->GetActorLocation() + FVector(0.f, 0.f, 40.f * Chamber->GetChamberScale()));
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
	// The left stick turns the watch's dial while it is open: it doesn't turn or scale anything else then.
	AdjustInput = Watch && Watch->IsDialOpen() ? FVector2D::ZeroVector : Value.Get<FVector2D>();
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
	// The hand that pressed select last (it chose the alien); else the right hand, else the left one.
	UMuseumHandInteractor* Last = LastSelectHand.Get();
	if (Last && Last->IsTracked())
	{
		return Last;
	}
	if (RightHand->IsTracked())
	{
		return RightHand;
	}
	return LeftHand->IsTracked() ? LeftHand.Get() : nullptr;
}

FVector AMuseumPawn::GetShoulder(const UMuseumHandInteractor* Hand) const
{
	const FRotator FlatView(0.f, GetHeadRotation().Yaw, 0.f);
	const float Side = Hand && Hand->GetHand() == EControllerHand::Left ? -1.f : 1.f;
	return GetHeadLocation() + FVector(0.f, 0.f, -15.f) + FRotationMatrix(FlatView).GetScaledAxis(EAxis::Y) * (15.f * Side);
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

	// This hand points from now on (placing a chamber follows it).
	LastSelectHand = Hand;

	// Holding an alien with the grip: the trigger does nothing.
	if (GetAlienGrab(Hand).bActive)
	{
		return;
	}

	// A button or card under the ray: it wins over a case the hand happens to be near (life-size cases are big,
	// and the hand is often within reach of one while pointing at the collection).
	const FMuseumPointerHit& Hit = Hand->GetPointerHit();
	AActor* HitActor = Hit.Actor.Get();
	IMuseumInteractable* Interactable = Cast<IMuseumInteractable>(HitActor);
	const bool bRayOnUI = Interactable && !Cast<AAlienChamber>(HitActor);

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

	// Chamber right at the hand: a grab (unless the ray is on a button).
	if (Mode == EMuseumPawnMode::Default && !bRayOnUI && FindNearestChamber(Hand->GetGrabLocation(), NearGrabDistance))
	{
		TryBeginGrab(Hand, true);
		return;
	}

	// UI (panel buttons, cards) works in every mode.
	if (bRayOnUI)
	{
		Interactable->OnPointerSelect(Hit.Component.Get(), this);
		return;
	}

	if (Mode != EMuseumPawnMode::Default)
	{
		UpdatePlacement(0.f); // where this hand points (the preview may have followed the other one)
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
	if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
	{
		Audio->PlayAt(TEXT("Case.Resize"), Handle->GetComponentLocation());
	}
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
		if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
		{
			Audio->PlayAt(TEXT("Case.Resize"), Hand->GetGrabLocation(), 0.7f);
		}
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
	FVector GrabPoint = Hand->GetGrabLocation(); // held at the hand
	if (!Target)
	{
		Target = Cast<AAlienChamber>(Hand->GetPointerHit().Actor.Get());
		GrabPoint = Hand->GetPointerHit().Location; // taken hold of along the ray, maybe metres away
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
	ResetOneHandGrab(Hand, Target, GrabPoint);
	Hand->SetLaserEnabled(false);

	const FHandGrab& Other = GetGrab(GetOtherHand(Hand));
	if (Other.Chamber.Get() == Target)
	{
		BeginTwoHandGrab(Target);
	}
	else
	{
		Target->BeginGrab();
		if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
		{
			Audio->PlayAt(TEXT("Case.Grab"), Target->GetActorLocation(), 0.8f);
		}
	}
	return true;
}

void AMuseumPawn::ResetOneHandGrab(UMuseumHandInteractor* Hand, AAlienChamber* Chamber, const FVector& GrabPoint)
{
	FHandGrab& Grab = GetGrab(Hand);
	const float Yaw = Hand->GetCarryYaw();
	Grab.Chamber = Chamber;
	Grab.LocalOffset = FRotator(0.f, -Yaw, 0.f).RotateVector(Chamber->GetActorLocation() - Hand->GetGrabLocation());
	Grab.YawOffset = Chamber->GetActorRotation().Yaw - Yaw;
	Grab.TargetScale = Chamber->GetChamberScale();

	// Taken hold of far along the ray: the arm alone only reaches half a metre either way, so reaching out or pulling
	// in carries it forward or back in proportion to how far off it is. Held at the hand it moves 1:1. The reach is
	// measured from the shoulder, so swinging or raising the arm doesn't push or pull it. (On the desktop the "hand"
	// is the camera ray: WASD walks it about instead.)
	const FVector Shoulder = GetShoulder(Hand);
	Grab.StartReach = static_cast<float>(FVector::Dist(Hand->GetGrabLocation(), Shoulder));
	const float HoldDistance = static_cast<float>(FVector::Dist(GrabPoint, Shoulder));
	Grab.DepthGain = bDesktopMode ? 1.f
		: FMath::Clamp(HoldDistance / FMath::Max(20.f, Grab.StartReach), 1.f, FMath::Max(1.f, MaxCarryDepthGain));
	// Never pulled onto the viewer: its centre stays a little over half its diagonal away (or as close as it was).
	const FVector Outer = Chamber->GetOuterSize();
	const float CenterDistance = static_cast<float>(FVector::Dist2D(Chamber->GetActorLocation(), GetHeadLocation()));
	Grab.MinDistance = FMath::Min(CenterDistance, 0.5f * static_cast<float>(FVector2D(Outer.X, Outer.Y).Size()) + 30.f);
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
		const FMuseumPointerHit& OtherHit = Other->GetPointerHit();
		ResetOneHandGrab(Other, Chamber, OtherHit.Actor.Get() == Chamber ? OtherHit.Location : Other->GetGrabLocation());
		return;
	}

	bTwoHand = false;
	Chamber->EndGrab();
	if (bWasClick)
	{
		Chamber->ToggleInfoPanel();
	}
	else if (Chamber->WasMovedByLastGrab())
	{
		if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
		{
			Audio->PlayAt(TEXT("Case.Release"), Chamber->GetActorLocation());
		}
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
			const FVector2D Limits = Chamber->GetScaleLimits(); // the usual range, stretched up to its alien's life size
			const float Scale = FMath::Clamp(TwoHandStartScale * Distance / TwoHandStartDistance, Limits.X, Limits.Y);
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
			const FVector2D Limits = Chamber->GetScaleLimits();
			Grab.TargetScale = FMath::Clamp(Grab.TargetScale * (1.f + AdjustInput.Y * ScaleSpeed * DeltaSeconds), Limits.X, Limits.Y);
			Grab.bMoved = true;
		}

		// Reaching out / pulling in: a case held far along the ray goes forward / back DepthGain times as far as the
		// hand (the hand's own movement plus the extra), along the ray's direction.
		const float Yaw = Hand->GetCarryYaw();
		const FVector Head = GetHeadLocation();
		const float Reach = static_cast<float>(FVector::Dist(Hand->GetGrabLocation(), GetShoulder(Hand)));
		// (A hand's tremor of a centimetre or so isn't a push: a click must leave the case exactly where it stands.)
		const float ReachChange = Reach - Grab.StartReach;
		FVector Offset = Grab.LocalOffset;
		Offset.X += FMath::Sign(ReachChange) * FMath::Max(0.f, FMath::Abs(ReachChange) - 1.5f) * (Grab.DepthGain - 1.f);
		FVector Target = Hand->GetGrabLocation() + FRotator(0.f, Yaw, 0.f).RotateVector(Offset);
		// ... never onto the viewer.
		const FVector2D FromHead(Target.X - Head.X, Target.Y - Head.Y);
		const float Distance = static_cast<float>(FromHead.Size());
		if (Distance < Grab.MinDistance && Distance > 1.f)
		{
			const FVector2D Out = FromHead * (Grab.MinDistance / Distance);
			Target.X = Head.X + Out.X;
			Target.Y = Head.Y + Out.Y;
		}
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

	UMuseumSceneComponent* Scene = Director->GetScene();
	PlacementHit = Scene->RaycastSurface(Origin, Direction, 600.f);
	bPlacementInAir = false;
	bool bShowGhost = false;
	FVector LaserEnd = Origin + Direction * 300.f;
	float Yaw = 0.f;
	FVector GhostSize = CaseSize;
	bool bFits = true;
	FString LabelText;
	if (Mode == EMuseumPawnMode::PlacingAlien && PendingAlien && Template)
	{
		// A new case for an alien is its own case, as big as the alien really is if the room allows it - so it stands
		// where you point, on the floor (or on a table top you point at), never in mid-air: under a ceiling a floating
		// case can't be life size. Pointing at a wall (or a piece of furniture's side) it stands at its foot; into open
		// space, on the floor under the end of the ray. Walls, furniture or other cases in the way, it slides into
		// the free space and faces you (FindLifeSizeSpot).
		const FMuseumSurfaceHit RoomHit = Scene->RaycastRoom(Origin, Direction, 600.f);
		const float FloorZ = Scene->GetFloorZ();
		LaserEnd = RoomHit.bHit ? RoomHit.Location : Origin + Direction * FloatPlacementDistance;
		const FVector Aim = RoomHit.IsPlaceable() ? RoomHit.Location : FVector(LaserEnd.X, LaserEnd.Y, FloorZ);
		// The search tries up to 18 spots: while the hand holds still (and for a quarter second) the last answer stands.
		const double Now = GetWorld()->GetTimeSeconds();
		if (SpotCache.Alien.Get() != PendingAlien || FVector::DistSquared(SpotCache.Aim, Aim) > 4.0 || Now - SpotCache.Time > 0.25
			|| DeltaSeconds <= 0.f) // asked right before placing: always fresh
		{
			SpotCache.bFits = Director->FindLifeSizeSpot(PendingAlien, Aim, Aim.Z > FloorZ + 10.f, RoomHit.bHit && !RoomHit.IsPlaceable(),
				SpotCache.Location, SpotCache.Yaw, SpotCache.Inner, SpotCache.Scale);
			SpotCache.Alien = PendingAlien;
			SpotCache.Aim = Aim;
			SpotCache.Time = Now;
		}
		bFits = SpotCache.bFits;
		PlacementLocation = SpotCache.Location;
		Yaw = SpotCache.Yaw;
		const FVector Inner = SpotCache.Inner;
		const float Scale = SpotCache.Scale;
		if (bFits)
		{
			GhostSize = Template->GetOuterSizeAt(Inner, Scale);
		}
		else
		{
			PlacementLocation = Aim;
			Yaw = (GetHeadLocation() - Aim).Rotation().Yaw;
		}
		bShowGhost = true;
		bPlacementSpotOk = true;
		if (PendingAlien->LifeHeight > 0.f)
		{
			const float Percent = 100.f * Scale / PendingAlien->GetLifeScale();
			LabelText = PendingAlien->DisplayName.ToString().ToUpper() + TEXT("\n")
				+ (!bFits ? FString(TEXT("NO ROOM HERE"))
					: Percent >= 99.5f ? FString::Printf(TEXT("LIFE SIZE  %.2f m"), PendingAlien->LifeHeight / 100.f)
					: FString::Printf(TEXT("%.0f%% OF LIFE SIZE"), Percent));
		}
	}
	else
	{
		// An empty case: on the surface under the ray, or with floating chambers in mid-air FloatPlacementDistance
		// along it - but never past the wall it points at.
		if (PlacementHit.IsPlaceable() || (PlacementHit.bHit && !Director->bChambersFloat))
		{
			PlacementLocation = PlacementHit.Location;
			LaserEnd = PlacementHit.Location;
			bShowGhost = true;
		}
		else if (Director->bChambersFloat)
		{
			float Reach = FloatPlacementDistance;
			const FMuseumSurfaceHit Wall = Scene->RaycastRoom(Origin, Direction, FloatPlacementDistance + static_cast<float>(CaseSize.X));
			if (Wall.bHit)
			{
				Reach = FMath::Min(Reach, FMath::Max(20.f, static_cast<float>(FVector::Dist(Origin, Wall.Location) - 0.5 * CaseSize.X) - 5.f));
			}
			// The ray points at the middle of the floating chamber; it never goes below the floor.
			LaserEnd = Origin + Direction * Reach;
			PlacementLocation = LaserEnd - FVector(0.f, 0.f, Height * 0.5f);
			PlacementLocation.Z = FMath::Max(PlacementLocation.Z, Scene->GetFloorZ());
			bPlacementInAir = true;
			bShowGhost = true;
		}
		// The new chamber will face the viewer.
		Yaw = (GetHeadLocation() - PlacementLocation).Rotation().Yaw;
		bPlacementSpotOk = bPlacementInAir || PlacementHit.IsPlaceable();
	}
	bPlacementValid = bPlacementSpotOk
		&& bFits
		&& Director->CanAddChamber()
		&& Director->IsFootprintFree(PlacementLocation, Yaw, GhostSize + FVector(4.f, 4.f, 0.f), nullptr);

	const FLinearColor Color = bPlacementValid ? ValidColor : InvalidColor;
	Hand->SetLaserOverride(LaserEnd, bShowGhost ? Color : InvalidColor);
	if (!bShowGhost)
	{
		PlacementGhost->SetVisibility(false);
		PlacementLabel->SetVisibility(false);
		return;
	}

	// Same shape, size and facing as the chamber that will be spawned.
	UStaticMesh* GhostMesh = (!Template || Template->Shape == EChamberShape::Square) ? GhostCubeMesh.Get() : GhostCylinderMesh.Get();
	if (GhostMesh && PlacementGhost->GetStaticMesh() != GhostMesh)
	{
		PlacementGhost->SetStaticMesh(GhostMesh);
	}
	PlacementGhost->SetVisibility(true);
	PlacementGhost->SetWorldScale3D(GhostSize / 100.f);
	PlacementGhost->SetWorldLocationAndRotation(PlacementLocation + FVector(0.f, 0.f, GhostSize.Z * 0.5f), FRotator(0.f, Yaw, 0.f));
	PlacementLabel->SetVisibility(!LabelText.IsEmpty());
	if (!LabelText.IsEmpty())
	{
		// Over the ghost, turned to the viewer (text faces its +X).
		const FVector Top = PlacementLocation + FVector(0.f, 0.f, GhostSize.Z + 8.f);
		PlacementLabel->SetText(FText::FromString(LabelText));
		PlacementLabel->SetTextRenderColor(bPlacementValid ? FColor(170, 255, 190) : FColor(255, 140, 120));
		PlacementLabel->SetWorldLocationAndRotation(Top, FRotator(0.f, (GetHeadLocation() - Top).Rotation().Yaw, 0.f));
	}
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
			if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
			{
				Audio->PlayAt(TEXT("Alien.Materialize"), Chamber->GetActorLocation() + FVector(0.f, 0.f, 40.f * Chamber->GetChamberScale()));
			}
			SetMode(EMuseumPawnMode::Default);
			return;
		}
	}

	if (!bPlacementValid)
	{
		if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
		{
			Audio->PlayAt(TEXT("UI.Error"), Head + (PlacementLocation - Head).GetSafeNormal() * 40.f); // towards where you point
		}
		Director->SetStatusText(!Director->CanAddChamber() ? TEXT("The museum is full. Remove a chamber first.")
			: Mode == EMuseumPawnMode::PlacingAlien ? TEXT("No room for its case here. Point at open floor.")
			: bPlacementSpotOk ? TEXT("Too close to another chamber.")
			: TEXT("Can't place there. Aim at free floor or a table top."));
		return;
	}

	const float Yaw = (Head - PlacementLocation).Rotation().Yaw;
	Director->SpawnChamberAt(PlacementLocation, Yaw, Mode == EMuseumPawnMode::PlacingAlien ? PendingAlien.Get() : nullptr);
	if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
	{
		Audio->PlayAt(TEXT("Case.Place"), PlacementLocation + FVector(0.f, 0.f, 20.f));
		if (Mode == EMuseumPawnMode::PlacingAlien)
		{
			Audio->PlayAt(TEXT("Alien.Materialize"), PlacementLocation + FVector(0.f, 0.f, 50.f));
		}
	}
	const bool bPlacedAlien = Mode == EMuseumPawnMode::PlacingAlien;
	SetMode(EMuseumPawnMode::Default);
	if (!bPlacedAlien) // for an alien the director has said how big it came out
	{
		Director->SetStatusText(bPlacementInAir
			? TEXT("Chamber floating in the air. Grab it to move, use two hands to resize.")
			: TEXT("Chamber placed. Grab it to move, use two hands to resize."));
	}
}

AMuseumPawn::FHandTap& AMuseumPawn::GetTap(const UMuseumHandInteractor* Hand)
{
	return Hand == LeftHand ? LeftTap : RightTap;
}

void AMuseumPawn::UpdateGlassTaps(float DeltaSeconds)
{
	AMuseumDirector* Director = GetDirector();
	const UWorld* World = GetWorld();
	if (!Director || !World || DeltaSeconds <= 0.f)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (UMuseumHandInteractor* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		FHandTap& Tap = GetTap(Hand);
		FVector Tip;
		if (!Hand->GetTapPoint(Tip))
		{
			Tap = FHandTap();
			continue;
		}
		// The glass nearest the tip, if it is within a few cm of it.
		AAlienChamber* Nearest = nullptr;
		float NearestDistance = 0.f;
		FVector NearestOnGlass = FVector::ZeroVector;
		for (AAlienChamber* Chamber : Director->GetChambers())
		{
			float Distance = 0.f;
			FVector Normal;
			FVector OnGlass;
			if (Chamber && Chamber->GetGlassWallDistance(Tip, Distance, Normal, OnGlass) && FMath::Abs(Distance) < 12.f
				&& (!Nearest || FMath::Abs(Distance) < FMath::Abs(NearestDistance)))
			{
				Nearest = Chamber;
				NearestDistance = Distance;
				NearestOnGlass = OnGlass;
			}
		}
		if (!Nearest)
		{
			Tap = FHandTap();
			continue;
		}
		// Busy hands don't knock: holding or reaching for something, resizing, placing a case.
		const bool bBusy = Mode != EMuseumPawnMode::Default || Hand->IsGrabPressed() || Hand->IsSelectPressed()
			|| GetGrab(Hand).Chamber.IsValid() || GetResize(Hand).Chamber.IsValid()
			|| GetAlienGrab(Hand).Alien.IsValid() || GetAlienPress(Hand).Alien.IsValid() || Nearest->IsBeingGrabbed();
		if (Tap.Chamber.Get() == Nearest && Tap.bHasLast && Tap.LastDistance > 0.f && NearestDistance <= 0.f)
		{
			// In through the glass since the last frame: fast enough to be a knock?
			const float Speed = (Tap.LastDistance - NearestDistance) / DeltaSeconds;
			if (Tap.bArmed && !bBusy && Speed >= TapMinSpeed && Now - Tap.LastTapTime > 0.15)
			{
				const float Strength = FMath::Clamp((Speed - TapMinSpeed) / FMath::Max(1.f, TapKnockSpeed - TapMinSpeed), 0.f, 1.f);
				Nearest->TapGlass(NearestOnGlass, Strength);
				Hand->PulseHaptics(0.25f + 0.6f * Strength, 0.03f + 0.04f * Strength);
				Tap.LastTapTime = Now;
			}
			Tap.bArmed = false; // a slow push through the glass is a hand reaching in: no knock until it comes back out
		}
		if (NearestDistance > 1.5f)
		{
			Tap.bArmed = true;
		}
		Tap.Chamber = Nearest;
		Tap.LastDistance = NearestDistance;
		Tap.bHasLast = true;
	}
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
	UpdateGlassTaps(DeltaSeconds);
	UpdateWatchHands();
}

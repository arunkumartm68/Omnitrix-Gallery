// Alien Museum - the player in mixed reality.

#include "Interaction/MuseumPawn.h"
#include "Interaction/MuseumHandInteractor.h"
#include "Chamber/AlienChamber.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumDirector.h"
#include "Core/MuseumInteractable.h"
#include "MR/MuseumSceneComponent.h"
#include "Data/AlienDataAsset.h"
#include "Ben10.h"
#include "Camera/CameraComponent.h"
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
		const FHandGrab& Grab = GetGrab(Hand);
		if (Grab.Chamber.IsValid() && Grab.bFromSelect)
		{
			EndGrab(Hand);
		}
		return;
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
		TryBeginGrab(Hand, false);
	}
	else
	{
		const FHandGrab& Grab = GetGrab(Hand);
		if (Grab.Chamber.IsValid() && !Grab.bFromSelect)
		{
			EndGrab(Hand);
		}
	}
}

bool AMuseumPawn::TryBeginGrab(UMuseumHandInteractor* Hand, bool bFromSelect)
{
	FHandGrab& Grab = GetGrab(Hand);
	if (Grab.Chamber.IsValid())
	{
		return false;
	}

	AAlienChamber* Target = FindNearestChamber(Hand->GetGrabLocation(), NearGrabDistance);
	if (!Target)
	{
		Target = Cast<AAlienChamber>(Hand->GetPointerHit().Actor.Get());
	}
	if (!Target)
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
		if (Hand->IsTracked() && !GetGrab(Hand).Chamber.IsValid())
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
	const float HalfWidth = Template ? Template->GetHalfWidth() : 33.f;
	const float Height = Template ? Template->GetTotalHeight() : 72.f;
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

	bPlacementSpotOk = bPlacementInAir || PlacementHit.IsPlaceable();
	bPlacementValid = bPlacementSpotOk
		&& Director->CanAddChamber()
		&& Director->IsPlacementFree(PlacementLocation, HalfWidth + 3.f, nullptr);

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
	const float Yaw = (GetHeadLocation() - PlacementLocation).Rotation().Yaw;
	PlacementGhost->SetVisibility(true);
	PlacementGhost->SetWorldScale3D(FVector(2.f * HalfWidth, 2.f * HalfWidth, Height) / 100.f);
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

	UpdateGrabs(DeltaSeconds);
	UpdateHover();
	UpdatePlacement(DeltaSeconds);
	UpdateMenuGesture(DeltaSeconds);
}

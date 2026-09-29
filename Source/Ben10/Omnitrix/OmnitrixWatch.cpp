// Alien Museum - the classic Omnitrix on the player's left wrist.

#include "Omnitrix/OmnitrixWatch.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumAudio.h"
#include "Core/MuseumDirector.h"
#include "Data/AlienCollectionAsset.h"
#include "Data/AlienDataAsset.h"
#include "Interaction/MuseumHandInteractor.h"
#include "Ben10.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The model's geometry (cm at scale 1, the watch's frame): Scripts/blender_convert_omnitrix.py measured it
	// into SourceArt/Converted/Omnitrix.json.
	const FVector FaceCentre(0.f, 0.f, 1.654f);
	const FVector ButtonCentre(1.989f, 0.f, 0.824f);
	constexpr float CoreRadius = 1.3f;   // the faceplate ring
	constexpr float CoreTop = 1.914f;
	constexpr float PopHeight = 0.55f;   // how far the core rises for the dial
	constexpr float ButtonRadius = 0.9f; // a fingertip this close presses it

	const FName FaceSlot(TEXT("OmnitrixFace"));
	const FName ParamColor(TEXT("Color"));
	const FName ParamBrightness(TEXT("Brightness"));
	const FName ParamMode(TEXT("Mode"));
	const FName ParamSilhouette(TEXT("Silhouette"));
	const FName ParamFlash(TEXT("Flash"));
	const FName ParamFlashColor(TEXT("FlashColor"));
	const FName ParamRing(TEXT("Ring"));
	const FName ParamRingColor(TEXT("RingColor"));

	float EaseOutBack(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		const float C1 = 1.4f;
		return 1.f + (C1 + 1.f) * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f);
	}

	/** Rotation with its Z along Z and its X as near as possible to X. */
	FRotator Frame(const FVector& Z, const FVector& X)
	{
		return FRotationMatrix::MakeFromZX(Z.GetSafeNormal(), X.GetSafeNormal()).Rotator();
	}
}

AOmnitrixWatch::AOmnitrixWatch()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // after the hands have updated
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto MakeMesh = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCastShadow(false);
		Mesh->SetCanEverAffectNavigation(false);
		return Mesh;
	};
	Band = MakeMesh(TEXT("Band"), Root);
	CorePivot = CreateDefaultSubobject<USceneComponent>(TEXT("CorePivot"));
	CorePivot->SetupAttachment(Root);
	Core = MakeMesh(TEXT("Core"), CorePivot);
	Face = MakeMesh(TEXT("Face"), CorePivot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BandMesh(TEXT("/Game/AlienMuseum/Models/Omnitrix/Band/Omnitrix_Band.Omnitrix_Band"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CoreMesh(TEXT("/Game/AlienMuseum/Models/Omnitrix/Core/Omnitrix_Core.Omnitrix_Core"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FaceMesh(TEXT("/Game/AlienMuseum/Models/Omnitrix/Face/Omnitrix_Face.Omnitrix_Face"));
	Band->SetStaticMesh(BandMesh.Object);
	Core->SetStaticMesh(CoreMesh.Object);
	Face->SetStaticMesh(FaceMesh.Object);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetAbsolute(false, false, true); // world-size text whatever the watch's scale
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(1.3f);
	Label->SetTextRenderColor(FColor(120, 255, 140));
	Label->SetCastShadow(false);
	Label->SetVisibility(false);

	FlashCard = MakeMesh(TEXT("FlashCard"), Root);
	FlashCard->SetVisibility(false);

	// Default placements (see the header). Controller: behind the grip at the wrist, the face on the back of
	// the wrist (left of the controller, tilted up), the button towards the thumb. Hand: on the wrist frame
	// UMuseumHandInteractor builds (X along the hand, Y towards the thumb, Z out of its back), 3.5 cm
	// back towards the elbow. Desktop: bottom left of the view, turned to the eyes.
	ControllerOffset = FTransform(Frame(FVector(0.f, -0.87f, 0.5f), FVector(0.f, 0.5f, 0.87f)), FVector(-8.5f, 0.f, -1.5f));
	HandOffset = FTransform(Frame(FVector(0.f, 0.f, 1.f), FVector(0.f, 1.f, 0.f)), FVector(-3.5f, 0.f, 0.f));
	DesktopOffset = FTransform(Frame(FVector(-0.89f, 0.3f, 0.36f), FVector(0.f, 0.f, 1.f)), FVector(42.f, -14.f, -17.f));
}

void AOmnitrixWatch::BeginPlay()
{
	Super::BeginPlay();
	Root->SetRelativeScale3D(FVector(WatchScale));

	// The face: the watch's own material, drawn procedurally (M_OmnitrixFace, Scripts/create_museum_materials.py).
	FaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AlienMuseum/Materials/M_OmnitrixFace.M_OmnitrixFace"));
	if (FaceMaterial)
	{
		FaceMID = UMaterialInstanceDynamic::Create(FaceMaterial, this);
		const int32 Slot = Face->GetMaterialIndex(FaceSlot);
		Face->SetMaterial(Slot != INDEX_NONE ? Slot : 0, FaceMID);
	}
	else
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("Omnitrix: M_OmnitrixFace is missing (run Scripts/create_museum_materials.py)"));
	}

	// The flash in front of the eyes: a glowing card, only while it flashes.
	FlashCard->SetStaticMesh(MuseumAssets::PlaneMesh());
	if (UMaterialInterface* Glow = MuseumAssets::FXGlowMaterial())
	{
		FlashMID = UMaterialInstanceDynamic::Create(Glow, this);
		FlashMID->SetVectorParameterValue(MuseumAssets::Params::Color, ReadyColor);
		FlashMID->SetScalarParameterValue(MuseumAssets::Params::Softness, 0.6f);
		FlashCard->SetMaterial(0, FlashMID);
	}

	for (TActorIterator<AMuseumDirector> It(GetWorld()); It; ++It)
	{
		Director = *It;
		break;
	}
	SetState(EOmnitrixState::Asleep);
}

void AOmnitrixWatch::Setup(UMuseumHandInteractor* InWristHand, UMuseumHandInteractor* InOtherHand, UCameraComponent* InCamera)
{
	WristHand = InWristHand;
	OtherHand = InOtherHand;
	Camera = InCamera;
	if (Camera)
	{
		FlashCard->AttachToComponent(Camera, FAttachmentTransformRules::KeepRelativeTransform);
		FlashCard->SetUsingAbsoluteScale(true);
		// 12 cm in front of the eyes, its face (the plane's +Z) turned to them, wide enough to fill the view.
		FlashCard->SetRelativeLocationAndRotation(FVector(12.f, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		FlashCard->SetWorldScale3D(FVector(0.6f));
	}
}

// ---------------------------------------------------------------------------------------------
// Aliens
// ---------------------------------------------------------------------------------------------

TArray<UAlienDataAsset*> AOmnitrixWatch::GetAliens() const
{
	TArray<UAlienDataAsset*> Aliens;
	if (const AMuseumDirector* Museum = Director.Get())
	{
		if (Museum->Collection)
		{
			for (UAlienDataAsset* Alien : Museum->Collection->Aliens)
			{
				if (Alien)
				{
					Aliens.Add(Alien);
				}
			}
		}
	}
	return Aliens;
}

UAlienDataAsset* AOmnitrixWatch::GetSelectedAlien() const
{
	const TArray<UAlienDataAsset*> Aliens = GetAliens();
	return Aliens.Num() > 0 ? Aliens[((Selected % Aliens.Num()) + Aliens.Num()) % Aliens.Num()] : nullptr;
}

float AOmnitrixWatch::GetTimeLeft() const
{
	return State == EOmnitrixState::Transformed || State == EOmnitrixState::Recharging ? FMath::Max(0.f, TimeLeft) : 0.f;
}

// ---------------------------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------------------------

void AOmnitrixWatch::SetState(EOmnitrixState NewState)
{
	State = NewState;
	StateTime = 0.f;
	Label->SetVisibility(State == EOmnitrixState::Dial && bShown);
}

void AOmnitrixWatch::Press()
{
	switch (State)
	{
	case EOmnitrixState::Asleep:
	case EOmnitrixState::Awake:
		OpenDial();
		break;
	case EOmnitrixState::Dial:
		CloseDial();
		break;
	case EOmnitrixState::Transformed:
		TurnBack(false);
		break;
	case EOmnitrixState::Recharging:
		Refuse();
		break;
	}
}

void AOmnitrixWatch::OpenDial()
{
	const TArray<UAlienDataAsset*> Aliens = GetAliens();
	if (Aliens.Num() == 0)
	{
		Refuse();
		return;
	}
	SetState(EOmnitrixState::Dial);
	LastInputTime = 0.f;
	bSettled = true;
	ShowSelection();
	Sound(TEXT("Omnitrix.Activate"));
	Buzz(0.35f, 0.05f);
	UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: dial open (%s)"), *GetNameSafe(GetSelectedAlien()));
}

void AOmnitrixWatch::CloseDial()
{
	if (State != EOmnitrixState::Dial)
	{
		return;
	}
	SetState(bLookedAt ? EOmnitrixState::Awake : EOmnitrixState::Asleep);
	bTwisting = false;
	Sound(TEXT("Omnitrix.Close"));
	UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: dial closed"));
}

void AOmnitrixWatch::Dial(int32 Steps)
{
	const TArray<UAlienDataAsset*> Aliens = GetAliens();
	if (State != EOmnitrixState::Dial || Steps == 0 || Aliens.Num() == 0)
	{
		return;
	}
	Selected = (((Selected + Steps) % Aliens.Num()) + Aliens.Num()) % Aliens.Num();
	// The core clicks round a notch per alien (never less than a clear quarter-turn's worth over the set).
	CoreTurnTarget += Steps * FMath::Max(360.f / Aliens.Num(), 18.f);
	LastInputTime = 0.f;
	bSettled = false;
	ShowSelection();
	Sound(TEXT("Omnitrix.Dial"));
	Buzz(0.15f, 0.015f);
}

void AOmnitrixWatch::DialStick(float Value)
{
	if (FMath::Abs(Value) < 0.35f)
	{
		StickDirection = 0; // back in the middle: the next push steps at once
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 Direction = Value > 0.f ? 1 : -1;
	if (FMath::Abs(Value) >= 0.6f && (Direction != StickDirection || Now >= NextStickStep))
	{
		NextStickStep = Now + (Direction != StickDirection ? 0.45 : 0.22); // held: a pause, then it keeps turning
		StickDirection = Direction;
		Dial(Direction);
	}
}

void AOmnitrixWatch::Slam()
{
	UAlienDataAsset* Alien = GetSelectedAlien();
	if (State != EOmnitrixState::Dial || !Alien)
	{
		return;
	}
	ActiveAlien = Alien;
	TimeLeft = TransformTime;
	BeepTimer = 0.f;
	SetState(EOmnitrixState::Transformed);
	bTwisting = false;
	Flash = 1.f;
	FlashColor = FLinearColor::White;
	ScreenFlash = 1.f;
	Sound(TEXT("Omnitrix.Slam"));
	Buzz(0.9f, 0.12f);
	UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: slammed - %s for %.0f s"), *Alien->GetName(), TransformTime);
	OnTransform.Broadcast(Alien);
}

void AOmnitrixWatch::TurnBack(bool bTimedOut)
{
	if (State != EOmnitrixState::Transformed)
	{
		return;
	}
	UAlienDataAsset* Was = ActiveAlien;
	ActiveAlien = nullptr;
	RechargeLength = FMath::Max(0.5f, bTimedOut ? RechargeTime : TurnBackRechargeTime);
	TimeLeft = RechargeLength;
	SetState(EOmnitrixState::Recharging);
	Flash = 1.f;
	FlashColor = WarningColor;
	ScreenFlash = bTimedOut ? 0.7f : 0.4f;
	Sound(TEXT("Omnitrix.Timeout"));
	Buzz(0.6f, 0.2f);
	UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: %s - back from %s, recharging %.0f s"), bTimedOut ? TEXT("timed out") : TEXT("turned back"),
		*GetNameSafe(Was), RechargeLength);
	OnRevert.Broadcast(bTimedOut);
}

void AOmnitrixWatch::Refuse()
{
	RefuseBlink = 1.f;
	Sound(TEXT("Omnitrix.Denied"));
	Buzz(0.4f, 0.06f);
}

bool AOmnitrixWatch::WantsHand(const UMuseumHandInteractor* Hand) const
{
	FVector Tip;
	if (!bShown || !Hand || Hand != OtherHand || !Hand->GetTapPoint(Tip))
	{
		return false;
	}
	// Up close to a watch that is being looked at (or whose dial is open).
	const bool bInUse = bLookedAt || State == EOmnitrixState::Dial;
	return bInUse && FVector::Dist(Tip, FaceCentreWorld()) < 6.f * WatchScale;
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void AOmnitrixWatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateTime += DeltaSeconds;
	UpdatePlacement();
	UpdateLook(DeltaSeconds);
	UpdateTimer(DeltaSeconds);
	UpdateGestures(DeltaSeconds);
	UpdateVisuals(DeltaSeconds);
}

FVector AOmnitrixWatch::FaceCentreWorld() const
{
	return CorePivot->GetComponentTransform().TransformPosition(FaceCentre);
}

void AOmnitrixWatch::UpdatePlacement()
{
	// On the controller (late-updated with it), on the tracked wrist, or on the desktop's camera.
	// (On the desktop only the other hand has a mouse ray; the watch then sits in view.)
	const EMuseumHandSource Source = WristHand ? WristHand->GetSource() : EMuseumHandSource::None;
	const bool bDesktop = OtherHand && OtherHand->GetSource() == EMuseumHandSource::Desktop;
	USceneComponent* Parent = nullptr;
	FTransform Relative;
	bool bShow = true;
	if (bDesktop && Camera)
	{
		Parent = Camera;
		Relative = DesktopOffset;
	}
	else if (Source == EMuseumHandSource::Controller && WristHand->GetGripController())
	{
		Parent = WristHand->GetGripController();
		Relative = ControllerOffset;
	}
	else if (Source != EMuseumHandSource::Hand)
	{
		bShow = false;
	}

	if (Parent)
	{
		if (AttachedTo.Get() != Parent)
		{
			AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
			AttachedTo = Parent;
		}
		Root->SetRelativeTransform(FTransform(Relative.GetRotation(), Relative.GetTranslation(), FVector(WatchScale)));
	}
	else
	{
		if (AttachedTo.IsValid())
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			AttachedTo = nullptr;
		}
		FTransform Wrist;
		if (Source == EMuseumHandSource::Hand && WristHand->GetWristPose(Wrist))
		{
			const FTransform Placed = HandOffset * FTransform(Wrist.GetRotation(), Wrist.GetTranslation());
			SetActorTransform(FTransform(Placed.GetRotation(), Placed.GetTranslation(), FVector(WatchScale)));
		}
		else
		{
			bShow = false;
		}
	}

	if (bShow != bShown)
	{
		bShown = bShow;
		SetActorHiddenInGame(!bShown);
		Label->SetVisibility(State == EOmnitrixState::Dial && bShown);
		if (!bShown)
		{
			bTwisting = false;
		}
	}
}

void AOmnitrixWatch::UpdateLook(float DeltaSeconds)
{
	// Looked at: the face turned to the eyes, the watch in view and near.
	bool bNow = false;
	if (bShown && Camera)
	{
		const FVector Eye = Camera->GetComponentLocation();
		const FVector ToEye = Eye - FaceCentreWorld();
		const float Distance = static_cast<float>(ToEye.Size());
		if (Distance > 1.f && Distance <= WakeDistance)
		{
			const FVector Dir = ToEye / Distance;
			const bool bFacing = FVector::DotProduct(GetActorUpVector(), Dir) >= FMath::Cos(FMath::DegreesToRadians(WakeAngle));
			const bool bInView = FVector::DotProduct(Camera->GetForwardVector(), -Dir) >= FMath::Cos(FMath::DegreesToRadians(35.f));
			bNow = bFacing && bInView;
		}
	}
	LookTime = bNow ? LookTime + DeltaSeconds : 0.f;
	AwayTime = bNow ? 0.f : AwayTime + DeltaSeconds;
	bLookedAt = bNow ? LookTime >= 0.12f : (bLookedAt && AwayTime < 0.6f);

	const double Now = GetWorld()->GetTimeSeconds();
	switch (State)
	{
	case EOmnitrixState::Asleep:
		if (bLookedAt)
		{
			SetState(EOmnitrixState::Awake);
			if (Now - LastWakeSound > 4.0)
			{
				LastWakeSound = Now;
				Sound(TEXT("Omnitrix.Wake"), 0.8f);
			}
		}
		break;
	case EOmnitrixState::Awake:
		if (!bLookedAt && AwayTime > 1.f)
		{
			SetState(EOmnitrixState::Asleep);
		}
		break;
	case EOmnitrixState::Dial:
		LastInputTime += DeltaSeconds;
		// Once it rests on an alien, a soft chirp: that one is ready to go.
		if (!bSettled && LastInputTime >= 0.35f)
		{
			bSettled = true;
			Sound(TEXT("Omnitrix.Select"), 0.6f);
		}
		// Nobody using it: it closes (turning the dial keeps it open).
		if (LastInputTime >= DialTimeout || (!bLookedAt && AwayTime > 2.5f && !bTwisting))
		{
			CloseDial();
		}
		break;
	default:
		break;
	}
}

void AOmnitrixWatch::UpdateTimer(float DeltaSeconds)
{
	if (State == EOmnitrixState::Transformed)
	{
		TimeLeft -= DeltaSeconds;
		if (TimeLeft <= 0.f)
		{
			TurnBack(true);
			return;
		}
		if (TimeLeft <= WarningTime)
		{
			// Beeping faster and faster, flashing red with every beep.
			BeepTimer -= DeltaSeconds;
			if (BeepTimer <= 0.f)
			{
				const float Urgency = 1.f - TimeLeft / FMath::Max(1.f, WarningTime);
				BeepTimer = FMath::Lerp(1.f, 0.18f, Urgency * Urgency);
				BeepFlash = 1.f;
				Sound(TEXT("Omnitrix.Beep"), 0.7f + 0.3f * Urgency);
				Buzz(0.2f + 0.3f * Urgency, 0.03f);
			}
		}
	}
	else if (State == EOmnitrixState::Recharging)
	{
		TimeLeft -= DeltaSeconds;
		if (TimeLeft <= 0.f)
		{
			TimeLeft = 0.f;
			SetState(bLookedAt ? EOmnitrixState::Awake : EOmnitrixState::Asleep);
			Flash = 1.f;
			FlashColor = ReadyColor;
			Sound(TEXT("Omnitrix.Ready"));
			Buzz(0.3f, 0.05f);
			UE_LOG(LogAlienMuseum, Log, TEXT("Omnitrix: ready"));
		}
	}
}

void AOmnitrixWatch::UpdateGestures(float DeltaSeconds)
{
	// The other hand's fingertip (or its controller's tip) works the watch like a real one.
	FVector Tip;
	if (!bShown || !OtherHand || !OtherHand->GetTapPoint(Tip) || DeltaSeconds <= 0.f)
	{
		bSlamArmed = false;
		LastAbove = 100.f;
		bButtonArmed = true;
		bTwisting = false;
		bWasPinching = false;
		return;
	}
	const FTransform Watch = GetActorTransform();

	// The side button: poke it.
	const float ToButton = static_cast<float>(FVector::Dist(Tip, Watch.TransformPosition(ButtonCentre)));
	if (bButtonArmed && ToButton <= ButtonRadius * WatchScale)
	{
		bButtonArmed = false;
		Press();
	}
	else if (ToButton > 2.5f * ButtonRadius * WatchScale)
	{
		bButtonArmed = true;
	}

	if (State != EOmnitrixState::Dial)
	{
		bTwisting = false;
		bSlamArmed = false;
		LastAbove = 100.f;
		bWasPinching = OtherHand->IsPinching();
		return;
	}

	// Pinch the core and twist it (hand tracking): the dial follows the pinch around the face.
	const bool bPinching = OtherHand->GetSource() == EMuseumHandSource::Hand && OtherHand->IsPinching();
	auto AngleAround = [&Watch](const FVector& Point)
	{
		const FVector Local = Watch.InverseTransformPosition(Point);
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Local.Y), static_cast<float>(Local.X)));
	};
	if (bPinching && !bWasPinching && !bTwisting)
	{
		const FVector Local = Watch.InverseTransformPosition(OtherHand->GetPinchPoint());
		if (FVector2D(Local.X, Local.Y).Size() <= CoreRadius * 2.2f && FMath::Abs(Local.Z - CoreTop) <= 2.5f)
		{
			bTwisting = true;
			TwistAngle = AngleAround(OtherHand->GetPinchPoint());
			TwistTurned = 0.f;
			LastInputTime = 0.f;
		}
	}
	bWasPinching = bPinching;
	if (bTwisting)
	{
		if (!bPinching)
		{
			bTwisting = false;
		}
		else
		{
			const float Angle = AngleAround(OtherHand->GetPinchPoint());
			TwistTurned += FMath::FindDeltaAngleDegrees(TwistAngle, Angle);
			TwistAngle = Angle;
			LastInputTime = 0.f;
			while (TwistTurned >= DialStepAngle)
			{
				TwistTurned -= DialStepAngle;
				Dial(1);
			}
			while (TwistTurned <= -DialStepAngle)
			{
				TwistTurned += DialStepAngle;
				Dial(-1);
			}
		}
		bSlamArmed = false;
		return;
	}

	// Slam: a fingertip coming down on the popped-up core, fast.
	const FVector Local = Watch.InverseTransformPosition(Tip);
	const float Radial = FVector2D(Local.X, Local.Y).Size();
	const float Above = static_cast<float>(Local.Z) - (CoreTop + PopHeight); // watch units above the core's top
	if (Radial > CoreRadius * 1.6f)
	{
		bSlamArmed = false;
		LastAbove = 100.f;
		return;
	}
	if (Above > 1.f)
	{
		bSlamArmed = true;
	}
	if (bSlamArmed && LastAbove > 0.2f && Above <= 0.2f)
	{
		const float Speed = (LastAbove - Above) * WatchScale / DeltaSeconds; // cm/s
		bSlamArmed = false;
		if (Speed >= SlamSpeed)
		{
			Slam();
		}
	}
	LastAbove = Above;
}

void AOmnitrixWatch::ShowSelection()
{
	UAlienDataAsset* Alien = GetSelectedAlien();
	Label->SetText(Alien ? FText::FromString(Alien->DisplayName.ToString().ToUpper()) : FText::GetEmpty());
	Label->SetVisibility(State == EOmnitrixState::Dial && bShown);
	if (FaceMID)
	{
		UTexture2D* Silhouette = Alien ? Alien->OmnitrixSilhouette.LoadSynchronous() : nullptr;
		FaceMID->SetTextureParameterValue(ParamSilhouette, Silhouette);
		FaceMID->SetScalarParameterValue(ParamMode, Silhouette ? 1.f : 0.f);
	}
}

void AOmnitrixWatch::UpdateVisuals(float DeltaSeconds)
{
	// The core: up for the dial (with a little overshoot), slammed down fast, turning notch by notch.
	const float PopTarget = State == EOmnitrixState::Dial ? 1.f : 0.f;
	Pop = FMath::FInterpConstantTo(Pop, PopTarget, DeltaSeconds, PopTarget > Pop ? 5.f : 14.f);
	CoreTurn = FMath::FInterpTo(CoreTurn, CoreTurnTarget, DeltaSeconds, 20.f);
	CorePivot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, PopHeight * (PopTarget > 0.f ? EaseOutBack(Pop) : Pop)), FRotator(0.f, CoreTurn, 0.f));

	// The face.
	const float Time = GetWorld()->GetTimeSeconds();
	FLinearColor Color = ReadyColor;
	FLinearColor RingColor = ReadyColor;
	float Target = 0.2f;
	float Ring = 0.f;
	switch (State)
	{
	case EOmnitrixState::Asleep:
		Target = 0.22f;
		break;
	case EOmnitrixState::Awake:
		Target = 1.1f + 0.12f * FMath::Sin(Time * 2.f * PI * 0.7f); // a slow glow
		break;
	case EOmnitrixState::Dial:
		Target = 1.35f;
		break;
	case EOmnitrixState::Transformed:
		Target = bLookedAt ? 1.f : 0.6f;
		Ring = TimeLeft / FMath::Max(1.f, TransformTime);
		if (TimeLeft <= WarningTime)
		{
			Color = FMath::Lerp(ReadyColor, WarningColor, FMath::Max(0.3f, BeepFlash));
			RingColor = WarningColor;
			Target = 0.8f + 0.6f * BeepFlash;
		}
		break;
	case EOmnitrixState::Recharging:
		Color = WarningColor;
		RingColor = WarningColor;
		Target = 0.5f;
		Ring = 1.f - TimeLeft / FMath::Max(0.5f, RechargeLength); // filling up again
		break;
	}
	if (RefuseBlink > 0.f)
	{
		Color = FMath::Lerp(Color, State == EOmnitrixState::Recharging ? WarningColor : RefuseColor, RefuseBlink);
		Target += 0.8f * RefuseBlink;
	}
	Brightness = FMath::FInterpTo(Brightness, Target, DeltaSeconds, 10.f);
	Flash = FMath::Max(0.f, Flash - DeltaSeconds * 2.2f);
	BeepFlash = FMath::Max(0.f, BeepFlash - DeltaSeconds * 5.f);
	RefuseBlink = FMath::Max(0.f, RefuseBlink - DeltaSeconds * 3.f);
	if (FaceMID)
	{
		FaceMID->SetVectorParameterValue(ParamColor, Color);
		FaceMID->SetScalarParameterValue(ParamBrightness, Brightness);
		FaceMID->SetScalarParameterValue(ParamFlash, Flash);
		FaceMID->SetVectorParameterValue(ParamFlashColor, FlashColor);
		FaceMID->SetScalarParameterValue(ParamRing, Ring);
		FaceMID->SetVectorParameterValue(ParamRingColor, RingColor);
		if (State != EOmnitrixState::Dial)
		{
			FaceMID->SetScalarParameterValue(ParamMode, 0.f); // the hourglass
		}
	}

	// The name above the watch, turned to the eyes.
	if (Label->IsVisible() && Camera)
	{
		const FVector Above = Root->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, CoreTop + PopHeight)) + FVector(0.f, 0.f, 2.f);
		const FVector ToEye = (Camera->GetComponentLocation() - Above).GetSafeNormal2D();
		Label->SetWorldLocationAndRotation(Above, ToEye.Rotation());
	}

	// The transformation's flash.
	ScreenFlash = FMath::Max(0.f, ScreenFlash - DeltaSeconds * 2.5f);
	const bool bFlashing = ScreenFlash > 0.f && FlashMID;
	FlashCard->SetVisibility(bFlashing);
	if (bFlashing)
	{
		FlashMID->SetVectorParameterValue(MuseumAssets::Params::Color, State == EOmnitrixState::Transformed ? ReadyColor : WarningColor);
		FlashMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 2.f * ScreenFlash);
		FlashMID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.45f * ScreenFlash);
	}
}

// ---------------------------------------------------------------------------------------------
// Feedback
// ---------------------------------------------------------------------------------------------

void AOmnitrixWatch::Sound(FName Name, float Volume)
{
	if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
	{
		UMuseumAudio::FPlay How;
		How.Location = CorePivot->GetComponentLocation();
		How.AttachTo = CorePivot;
		How.Volume = Volume;
		How.Range = EMuseumSoundRange::Interface;
		Audio->Play(Name, How);
	}
}

void AOmnitrixWatch::Buzz(float Amplitude, float Duration)
{
	if (WristHand)
	{
		WristHand->PulseHaptics(Amplitude, Duration);
	}
}

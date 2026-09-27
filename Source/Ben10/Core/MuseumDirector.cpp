// Alien Museum - one per level. Runs the start-up flow and owns the museum state.

#include "Core/MuseumDirector.h"
#include "Chamber/AlienChamber.h"
#include "Data/AlienCollectionAsset.h"
#include "Data/AlienDataAsset.h"
#include "MR/MuseumPersistenceComponent.h"
#include "MR/MuseumSceneComponent.h"
#include "UI/AlienCollectionPanel.h"
#include "Ben10.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	/** A chamber's outer footprint on the floor plane plus its height range. */
	struct FFootprint
	{
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D AxisX = FVector2D(1.0, 0.0);
		FVector2D AxisY = FVector2D(0.0, 1.0);
		FVector2D Half = FVector2D::ZeroVector;
		double Bottom = 0.0;
		double Top = 0.0;
	};

	FFootprint MakeFootprint(const FVector& FloorLocation, float Yaw, const FVector2D& Half, float Height)
	{
		FFootprint F;
		const double Rad = FMath::DegreesToRadians(static_cast<double>(Yaw));
		F.Center = FVector2D(FloorLocation.X, FloorLocation.Y);
		F.AxisX = FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
		F.AxisY = FVector2D(-FMath::Sin(Rad), FMath::Cos(Rad));
		F.Half = Half;
		F.Bottom = FloorLocation.Z;
		F.Top = FloorLocation.Z + Height;
		return F;
	}

	FFootprint MakeFootprint(const AAlienChamber* Chamber)
	{
		return MakeFootprint(Chamber->GetActorLocation(), Chamber->GetActorRotation().Yaw,
			Chamber->GetFootprintHalfSize(), Chamber->GetTotalHeight());
	}

	/**
	 * Separating-axis test of two rotated rectangles (and their height ranges). When they overlap,
	 * OutPush is the shortest move that takes B out of A (plus Gap).
	 */
	bool FootprintsOverlap(const FFootprint& A, const FFootprint& B, FVector2D* OutPush = nullptr, double Gap = 0.0)
	{
		if (A.Top <= B.Bottom || B.Top <= A.Bottom)
		{
			return false; // one floats above the other
		}
		const FVector2D Delta = B.Center - A.Center;
		double BestDepth = TNumericLimits<double>::Max();
		FVector2D BestAxis = FVector2D::ZeroVector;
		for (const FVector2D& Axis : { A.AxisX, A.AxisY, B.AxisX, B.AxisY })
		{
			const double RadiusA = A.Half.X * FMath::Abs(A.AxisX | Axis) + A.Half.Y * FMath::Abs(A.AxisY | Axis);
			const double RadiusB = B.Half.X * FMath::Abs(B.AxisX | Axis) + B.Half.Y * FMath::Abs(B.AxisY | Axis);
			const double Distance = Delta | Axis;
			const double Depth = RadiusA + RadiusB + Gap - FMath::Abs(Distance);
			if (Depth <= 0.0)
			{
				return false;
			}
			if (Depth < BestDepth)
			{
				BestDepth = Depth;
				BestAxis = Distance >= 0.0 ? Axis : -Axis;
			}
		}
		if (OutPush)
		{
			*OutPush = BestAxis * BestDepth;
		}
		return true;
	}
}

AMuseumDirector::AMuseumDirector()
{
	PrimaryActorTick.bCanEverTick = true; // only for the key light that follows the viewer

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Scene = CreateDefaultSubobject<UMuseumSceneComponent>(TEXT("Scene"));
	Persistence = CreateDefaultSubobject<UMuseumPersistenceComponent>(TEXT("Persistence"));

	ChamberClass = AAlienChamber::StaticClass();
	PanelClass = AAlienCollectionPanel::StaticClass();
}

AMuseumDirector* AMuseumDirector::Get(const UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AMuseumDirector> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AMuseumDirector::BeginPlay()
{
	Super::BeginPlay();

	// Editor-only test furniture would hide the real room behind it on a headset.
	if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled() && !EditorRoomTag.IsNone())
	{
		TArray<AActor*> EditorRoom;
		UGameplayStatics::GetAllActorsWithTag(this, EditorRoomTag, EditorRoom);
		for (AActor* Actor : EditorRoom)
		{
			Actor->Destroy();
		}
	}

	if (!ChamberClass)
	{
		ChamberClass = AAlienChamber::StaticClass();
	}
	if (!PanelClass)
	{
		PanelClass = AAlienCollectionPanel::StaticClass();
	}
	if (!Collection)
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("MuseumDirector has no Alien Collection assigned"));
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Panel = GetWorld()->SpawnActor<AAlienCollectionPanel>(PanelClass, FTransform::Identity, Params);
	if (Panel)
	{
		Panel->SetCollection(Collection);
	}
	PreloadAlienAssets();

	// The key light that follows the viewer: the first movable directional light in the level.
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ULightComponent* LightComponent = It->GetLightComponent();
		if (LightComponent && LightComponent->Mobility == EComponentMobility::Movable)
		{
			KeyLight = *It;
			break;
		}
	}
	if (bKeyLightFollowsViewer && !KeyLight.IsValid())
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("No movable directional light found: the key light cannot follow the viewer"));
	}

	Scene->OnSceneReady.AddDynamic(this, &AMuseumDirector::HandleSceneReady);
	Scene->StartScene();
}

void AMuseumDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateKeyLight(DeltaSeconds);
}

void AMuseumDirector::UpdateKeyLight(float DeltaSeconds)
{
	ADirectionalLight* Light = KeyLight.Get();
	FVector Head;
	FRotator View;
	if (!bKeyLightFollowsViewer || !Light || !GetViewer(Head, View))
	{
		return;
	}
	// Shining the way the viewer looks = coming from behind them. Eased, so turning the head does
	// not make the lighting jump.
	const FRotator Target(-KeyLightElevation, View.Yaw + KeyLightSideAngle, 0.f);
	Light->SetActorRotation(FMath::RInterpTo(Light->GetActorRotation(), Target, DeltaSeconds, 2.f));
}

void AMuseumDirector::HandleSceneReady(bool bDeviceScene)
{
	UE_LOG(LogAlienMuseum, Log, TEXT("Scene ready (%s)"), bDeviceScene ? TEXT("real room") : TEXT("fallback floor"));
	// Give the headset a moment to report a real head pose before placing things in front of it.
	GetWorldTimerManager().SetTimer(StartupTimer, this, &AMuseumDirector::RestoreMuseum, FMath::Max(0.01f, StartupDelay), false);
}

void AMuseumDirector::PreloadAlienAssets()
{
	if (!Collection)
	{
		return;
	}
	TArray<FSoftObjectPath> Paths;
	for (const UAlienDataAsset* Alien : Collection->Aliens)
	{
		if (!Alien)
		{
			continue;
		}
		for (const TSoftObjectPtr<UStaticMesh>* Mesh : { &Alien->ModelMesh, &Alien->PoseMesh, &Alien->BallMesh })
		{
			if (!Mesh->IsNull())
			{
				Paths.AddUnique(Mesh->ToSoftObjectPath());
			}
		}
	}
	if (Paths.Num() == 0)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	const int32 Count = Paths.Num();
	PreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths),
		FStreamableDelegate::CreateWeakLambda(this, [Start, Count]()
		{
			UE_LOG(LogAlienMuseum, Log, TEXT("Preloaded %d alien meshes in %.1f s"), Count, FPlatformTime::Seconds() - Start);
		}));
}

void AMuseumDirector::RestoreMuseum()
{
	Persistence->RestoreMuseum(ChamberClass,
		[this](AAlienChamber* Chamber, FName AlienId)
		{
			RegisterChamber(Chamber);
			Chamber->NotifyPlaced();
			UpdateFloatingState(Chamber);

			// A freshly located anchor can still shift a little; an alien that is already walking
			// would be left outside the glass, so it appears once the chamber has settled.
			UAlienDataAsset* Alien = Collection ? Collection->FindById(AlienId) : nullptr;
			if (!Alien)
			{
				return;
			}
			TWeakObjectPtr<AAlienChamber> WeakChamber(Chamber);
			FTimerHandle SpawnTimer;
			GetWorldTimerManager().SetTimer(SpawnTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakChamber, Alien]()
			{
				AAlienChamber* Restored = WeakChamber.Get();
				if (Restored && !Restored->GetOccupantData())
				{
					Restored->SpawnAlien(Alien);
					UpdateFloatingState(Restored);
				}
			}), FMath::Max(0.01f, RestoredAlienDelay), false);
		},
		[this](int32 NumRestored)
		{
			if (NumRestored == 0 && bSpawnStarterChamber)
			{
				SpawnStarterChamber();
			}
			bMuseumReady = true;
			ShowCollectionPanel();
			SetStatusText(NumRestored > 0
				? FString::Printf(TEXT("Welcome back! %d chamber(s) restored."), NumRestored)
				: FString(TEXT("Choose an alien, then point at a chamber.")));
		});
}

bool AMuseumDirector::GetViewer(FVector& OutHead, FRotator& OutRotation) const
{
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		OutHead = Camera->GetCameraLocation();
		OutRotation = Camera->GetCameraRotation();
		return true;
	}
	return false;
}

void AMuseumDirector::RegisterChamber(AAlienChamber* Chamber)
{
	if (!Chamber || Chambers.Contains(Chamber))
	{
		return;
	}
	Chambers.Add(Chamber);
	Chamber->OnGrabbed.AddUniqueDynamic(this, &AMuseumDirector::HandleChamberGrabbed);
	Chamber->OnResized.AddUniqueDynamic(this, &AMuseumDirector::HandleChamberResized);
	Chamber->OnOccupantChanged.AddUniqueDynamic(this, &AMuseumDirector::HandleOccupantChanged);
}

void AMuseumDirector::SpawnStarterChamber()
{
	FVector Head;
	FRotator View;
	if (!GetViewer(Head, View))
	{
		return;
	}
	const FRotator Flat(0.f, View.Yaw, 0.f);
	const FVector Ahead = Head + Flat.Vector() * StarterChamberDistance;
	const FMuseumSurfaceHit Hit = Scene->FindSurfaceBelow(Ahead, 400.f);
	const FVector FloorLocation = Hit.IsPlaceable() ? Hit.Location : FVector(Ahead.X, Ahead.Y, Scene->GetFloorZ());

	UAlienDataAsset* FirstAlien = (Collection && Collection->Aliens.Num() > 0) ? Collection->Aliens[0].Get() : nullptr;
	SpawnChamberAt(FloorLocation, (Head - FloorLocation).Rotation().Yaw, FirstAlien);
}

AAlienChamber* AMuseumDirector::SpawnChamberAt(const FVector& FloorLocation, float Yaw, UAlienDataAsset* Alien)
{
	UWorld* World = GetWorld();
	if (!World || !CanAddChamber())
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAlienChamber* Chamber = World->SpawnActor<AAlienChamber>(ChamberClass, FloorLocation, FRotator(0.f, Yaw, 0.f), Params);
	if (!Chamber)
	{
		return nullptr;
	}

	if (Alien)
	{
		Chamber->SpawnAlien(Alien);
	}
	RegisterChamber(Chamber);
	Chamber->NotifyPlaced();
	UpdateFloatingState(Chamber);
	Persistence->CommitChamber(Chamber);
	UE_LOG(LogAlienMuseum, Log, TEXT("Chamber placed at %s with %s"), *FloorLocation.ToCompactString(), Alien ? *Alien->AlienId.ToString() : TEXT("no alien"));
	return Chamber;
}

void AMuseumDirector::PlaceAlienInChamber(AAlienChamber* Chamber, UAlienDataAsset* Alien)
{
	if (!Chamber || !Alien)
	{
		return;
	}
	Chamber->SpawnAlien(Alien); // OnOccupantChanged saves the museum
	if (Panel)
	{
		Panel->SetSelectedAlien(nullptr);
	}
	SetStatusText(FString::Printf(TEXT("%s moved into its chamber."), *Alien->DisplayName.ToString()));
}

void AMuseumDirector::RemoveChamber(AAlienChamber* Chamber)
{
	if (!Chamber)
	{
		return;
	}
	Persistence->ForgetChamber(Chamber);
	Chambers.Remove(Chamber);
	Chamber->Destroy();
}

void AMuseumDirector::ClearMuseum()
{
	const TArray<TObjectPtr<AAlienChamber>> Copy = Chambers;
	for (AAlienChamber* Chamber : Copy)
	{
		RemoveChamber(Chamber);
	}
	Chambers.Reset();
	Persistence->ClearAll();
}

void AMuseumDirector::SettleChamber(AAlienChamber* Chamber)
{
	if (!Chamber)
	{
		return;
	}

	// Only clicked (info panel): it never left its spot, so it keeps its anchor and save entry.
	if (!Chamber->WasMovedByLastGrab())
	{
		Chamber->NotifyPlaced();
		return;
	}

	FVector Location = Chamber->GetActorLocation();
	if (bChambersFloat)
	{
		// No gravity: stay where released. Only a chamber released just above a real surface, or
		// partly sunk into one, is set onto it, and none can end up below the floor.
		float SurfaceZ;
		if (FindSurfaceUnderChamber(Chamber, SurfaceSnapDistance, SurfaceZ))
		{
			Location.Z = SurfaceZ;
		}
		Location.Z = FMath::Max(Location.Z, Scene->GetFloorZ());
	}
	else
	{
		const FMuseumSurfaceHit Hit = Scene->FindSurfaceBelow(Location, 400.f);
		Location.Z = Hit.IsPlaceable() ? Hit.Location.Z : Scene->GetFloorZ();
	}
	Chamber->SetActorLocation(Location);
	PushOutOfOtherChambers(Chamber);
	UpdateFloatingState(Chamber);

	Chamber->NotifyPlaced();
	Persistence->CommitChamber(Chamber);
}

bool AMuseumDirector::FindSurfaceUnderChamber(const AAlienChamber* Chamber, float MaxGap, float& OutZ) const
{
	// Start inside the chamber so a surface the base was pushed into is found too.
	const FVector Bottom = Chamber->GetActorLocation();
	const float Lift = FMath::Min(30.f, Chamber->GetTotalHeight() * 0.5f);
	const FMuseumSurfaceHit Hit = Scene->RaycastSurface(Bottom + FVector(0.f, 0.f, Lift), FVector::DownVector, Lift + MaxGap);
	if (Hit.IsPlaceable())
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AMuseumDirector::UpdateFloatingState(AAlienChamber* Chamber) const
{
	if (!Chamber)
	{
		return;
	}
	float SurfaceZ;
	const bool bResting = FindSurfaceUnderChamber(Chamber, 2.f, SurfaceZ)
		|| Chamber->GetActorLocation().Z <= Scene->GetFloorZ() + 2.f;
	Chamber->SetFloating(!bResting);
}

void AMuseumDirector::PushOutOfOtherChambers(AAlienChamber* Chamber) const
{
	// Rectangles, not circles, so cases can stand side by side in a row. A few passes in case being
	// pushed out of one chamber moves it into another. Floating chambers may be stacked.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		bool bMoved = false;
		for (const TObjectPtr<AAlienChamber>& Other : Chambers)
		{
			if (!Other || Other == Chamber)
			{
				continue;
			}
			FVector2D Push;
			if (FootprintsOverlap(MakeFootprint(Other), MakeFootprint(Chamber), &Push, 2.0))
			{
				Chamber->AddActorWorldOffset(FVector(Push.X, Push.Y, 0.0));
				bMoved = true;
			}
		}
		if (!bMoved)
		{
			return;
		}
	}
}

bool AMuseumDirector::CanAddChamber() const
{
	int32 Count = 0;
	for (const TObjectPtr<AAlienChamber>& Chamber : Chambers)
	{
		Count += IsValid(Chamber) ? 1 : 0;
	}
	return Count < MaxChambers;
}

bool AMuseumDirector::IsPlacementFree(const FVector& FloorLocation, float Radius, const AAlienChamber* Ignore) const
{
	const AAlienChamber* Template = GetChamberTemplate();
	return IsFootprintFree(FloorLocation, 0.f, FVector(2.f * Radius, 2.f * Radius, Template ? Template->GetTotalHeight() : 120.f), Ignore);
}

bool AMuseumDirector::IsFootprintFree(const FVector& FloorLocation, float Yaw, const FVector& OuterSize, const AAlienChamber* Ignore) const
{
	const FFootprint New = MakeFootprint(FloorLocation, Yaw, FVector2D(OuterSize.X, OuterSize.Y) * 0.5, OuterSize.Z);
	for (const TObjectPtr<AAlienChamber>& Chamber : Chambers)
	{
		if (IsValid(Chamber) && Chamber != Ignore && FootprintsOverlap(MakeFootprint(Chamber), New))
		{
			return false;
		}
	}
	return true;
}

const AAlienChamber* AMuseumDirector::GetChamberTemplate() const
{
	return ChamberClass ? ChamberClass->GetDefaultObject<AAlienChamber>() : GetDefault<AAlienChamber>();
}

TArray<AAlienChamber*> AMuseumDirector::GetChambers() const
{
	TArray<AAlienChamber*> Result;
	for (const TObjectPtr<AAlienChamber>& Chamber : Chambers)
	{
		if (IsValid(Chamber))
		{
			Result.Add(Chamber);
		}
	}
	return Result;
}

void AMuseumDirector::HandleChamberGrabbed(AAlienChamber* Chamber)
{
	Persistence->ReleaseAnchor(Chamber);
}

void AMuseumDirector::HandleChamberResized(AAlienChamber* Chamber)
{
	// The base did not move, so the spatial anchor stays valid: only the saved size changes.
	if (Chamber && !Chamber->IsBeingGrabbed())
	{
		Persistence->CommitChamber(Chamber);
	}
}

void AMuseumDirector::HandleOccupantChanged(AAlienChamber* Chamber)
{
	if (Chamber && !Chamber->IsBeingGrabbed())
	{
		Persistence->CommitChamber(Chamber);
	}
}

void AMuseumDirector::ShowCollectionPanel()
{
	FVector Head;
	FRotator View;
	if (Panel && GetViewer(Head, View))
	{
		Panel->Summon(Head, View);
	}
}

void AMuseumDirector::ToggleCollectionPanel()
{
	if (!Panel)
	{
		return;
	}
	if (Panel->IsPanelVisible())
	{
		Panel->SetPanelVisible(false);
	}
	else
	{
		ShowCollectionPanel();
	}
}

void AMuseumDirector::SetStatusText(const FString& Text)
{
	if (Panel)
	{
		Panel->SetStatusText(Text);
	}
}

void AMuseumDirector::SetShowRoomDebug(bool bShow)
{
	Scene->SetShowRoomDebug(bShow);
}

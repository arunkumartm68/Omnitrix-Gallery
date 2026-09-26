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
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AMuseumDirector::AMuseumDirector()
{
	PrimaryActorTick.bCanEverTick = false;

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

	Scene->OnSceneReady.AddDynamic(this, &AMuseumDirector::HandleSceneReady);
	Scene->StartScene();
}

void AMuseumDirector::HandleSceneReady(bool bDeviceScene)
{
	UE_LOG(LogAlienMuseum, Log, TEXT("Scene ready (%s)"), bDeviceScene ? TEXT("real room") : TEXT("fallback floor"));
	// Give the headset a moment to report a real head pose before placing things in front of it.
	GetWorldTimerManager().SetTimer(StartupTimer, this, &AMuseumDirector::RestoreMuseum, FMath::Max(0.01f, StartupDelay), false);
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
	for (const TObjectPtr<AAlienChamber>& Other : Chambers)
	{
		if (!Other || Other == Chamber)
		{
			continue;
		}
		const FVector Delta = Chamber->GetActorLocation() - Other->GetActorLocation();
		const float MinDistance = Chamber->GetOuterRadius() + Other->GetOuterRadius() + 2.f;
		// Floating chambers may be stacked: they only collide if their heights overlap.
		const bool bOverlapZ = Delta.Z < Other->GetTotalHeight() && -Delta.Z < Chamber->GetTotalHeight();
		if (bOverlapZ && Delta.Size2D() < MinDistance)
		{
			const FVector Direction = Delta.Size2D() > 1.f ? FVector(Delta.X, Delta.Y, 0.f).GetSafeNormal() : FVector::ForwardVector;
			Chamber->SetActorLocation(Other->GetActorLocation() + Direction * MinDistance + FVector(0.f, 0.f, Delta.Z));
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
	const float NewHeight = Template ? Template->GetTotalHeight() : 80.f;
	for (const TObjectPtr<AAlienChamber>& Chamber : Chambers)
	{
		if (!IsValid(Chamber) || Chamber == Ignore)
		{
			continue;
		}
		const FVector Delta = FloorLocation - Chamber->GetActorLocation();
		const bool bOverlapZ = Delta.Z < Chamber->GetTotalHeight() && -Delta.Z < NewHeight;
		if (bOverlapZ && Delta.Size2D() < Radius + Chamber->GetOuterRadius())
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

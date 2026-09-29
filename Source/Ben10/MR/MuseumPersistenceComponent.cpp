// Alien Museum - remembers chambers between sessions using Meta spatial anchors.

#include "MR/MuseumPersistenceComponent.h"
#include "Chamber/AlienChamber.h"
#include "Data/AlienDataAsset.h"
#include "Ben10.h"
#include "Engine/World.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "OculusXRAnchors.h"
#include "OculusXRAnchorTypes.h"
#include "OculusXRAnchorComponent.h"
#include "OculusXRAnchorBPFunctionLibrary.h"
#include "OculusXRHMDRuntimeSettings.h"

using OculusXRAnchors::FOculusXRAnchors;

UMuseumPersistenceComponent::UMuseumPersistenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UMuseumPersistenceComponent::AreAnchorsAvailable() const
{
	return bUseSpatialAnchors
		&& UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()
		&& GetDefault<UOculusXRHMDRuntimeSettings>()->bAnchorSupportEnabled;
}

// ---------------------------------------------------------------------------------------------
// Save file
// ---------------------------------------------------------------------------------------------

UMuseumSaveGame* UMuseumPersistenceComponent::GetSave()
{
	if (!SaveGame)
	{
		if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
		{
			SaveGame = Cast<UMuseumSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
		}
		LoadedVersion = SaveGame ? SaveGame->Version : UMuseumSaveGame::LifeSizeVersion;
		if (!SaveGame)
		{
			SaveGame = Cast<UMuseumSaveGame>(UGameplayStatics::CreateSaveGameObject(UMuseumSaveGame::StaticClass()));
		}
	}
	return SaveGame;
}

void UMuseumPersistenceComponent::WriteSave()
{
	if (UMuseumSaveGame* Save = GetSave())
	{
		Save->Version = UMuseumSaveGame::LifeSizeVersion;
		UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0);
	}
}

bool UMuseumPersistenceComponent::IsSaveFromBeforeLifeSize()
{
	GetSave();
	return LoadedVersion < UMuseumSaveGame::LifeSizeVersion;
}

FMuseumChamberRecord* UMuseumPersistenceComponent::FindRecord(const FGuid& ChamberId)
{
	UMuseumSaveGame* Save = GetSave();
	return Save ? Save->Chambers.FindByPredicate([&ChamberId](const FMuseumChamberRecord& Record) { return Record.ChamberId == ChamberId; }) : nullptr;
}

FMuseumChamberRecord& UMuseumPersistenceComponent::FindOrAddRecord(const FGuid& ChamberId)
{
	if (FMuseumChamberRecord* Existing = FindRecord(ChamberId))
	{
		return *Existing;
	}
	FMuseumChamberRecord& Record = GetSave()->Chambers.AddDefaulted_GetRef();
	Record.ChamberId = ChamberId;
	return Record;
}

// ---------------------------------------------------------------------------------------------
// Saving chambers and anchors
// ---------------------------------------------------------------------------------------------

void UMuseumPersistenceComponent::CommitChamber(AAlienChamber* Chamber)
{
	if (!Chamber)
	{
		return;
	}

	FMuseumChamberRecord& Record = FindOrAddRecord(Chamber->GetChamberId());
	const UAlienDataAsset* Data = Chamber->GetOccupantData();
	Record.AlienId = Data ? Data->AlienId : NAME_None;
	Record.Scale = Chamber->GetChamberScale();
	Record.InnerSize = Chamber->GetInnerSize();
	Record.LastTransform = Chamber->GetActorTransform();
	WriteSave();

	if (!AreAnchorsAvailable())
	{
		return;
	}

	if (UOculusXRAnchorComponent* Existing = Chamber->FindComponentByClass<UOculusXRAnchorComponent>())
	{
		if (Existing->HasValidHandle())
		{
			if (Existing->IsComponentTickEnabled())
			{
				return; // already anchored where it stands (e.g. only the alien changed)
			}
			// The previous anchor is still being erased: try again shortly.
			TWeakObjectPtr<AAlienChamber> WeakChamber(Chamber);
			FTimerHandle RetryHandle;
			GetWorld()->GetTimerManager().SetTimer(RetryHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakChamber]()
			{
				if (WeakChamber.IsValid())
				{
					CommitChamber(WeakChamber.Get());
				}
			}), 0.3f, false);
			return;
		}
	}

	CreateAndSaveAnchor(Chamber);
}

void UMuseumPersistenceComponent::CreateAndSaveAnchor(AAlienChamber* Chamber)
{
	TWeakObjectPtr<UMuseumPersistenceComponent> WeakThis(this);
	const FGuid ChamberId = Chamber->GetChamberId();

	// Chambers always stand upright, so the anchor only keeps position and yaw.
	const FTransform AnchorPose(FRotator(0.f, Chamber->GetActorRotation().Yaw, 0.f), Chamber->GetActorLocation());

	EOculusXRAnchorResult::Type StartResult;
	FOculusXRAnchors::CreateSpatialAnchor(AnchorPose, Chamber,
		FOculusXRSpatialAnchorCreateDelegate::CreateLambda([WeakThis, ChamberId](EOculusXRAnchorResult::Type Result, UOculusXRAnchorComponent* Anchor)
		{
			if (!WeakThis.IsValid() || !Anchor || !UOculusXRAnchorBPFunctionLibrary::IsAnchorResultSuccess(Result))
			{
				UE_LOG(LogAlienMuseum, Warning, TEXT("Creating a spatial anchor failed (result %d)"), static_cast<int32>(Result));
				return;
			}

			EOculusXRAnchorResult::Type SaveStartResult;
			FOculusXRAnchors::SaveAnchors(TArray<UOculusXRAnchorComponent*>{ Anchor },
				FOculusXRSaveAnchorsDelegate::CreateLambda([WeakThis, ChamberId](EOculusXRAnchorResult::Type SaveResult, const TArray<UOculusXRAnchorComponent*>& Saved)
				{
					if (!WeakThis.IsValid())
					{
						return;
					}
					if (!UOculusXRAnchorBPFunctionLibrary::IsAnchorResultSuccess(SaveResult) || Saved.Num() == 0 || !Saved[0])
					{
						UE_LOG(LogAlienMuseum, Warning, TEXT("Saving a spatial anchor failed (result %d)"), static_cast<int32>(SaveResult));
						return;
					}
					if (FMuseumChamberRecord* Record = WeakThis->FindRecord(ChamberId))
					{
						Record->AnchorUuid = Saved[0]->GetUUID().ToString();
						WeakThis->WriteSave();
						UE_LOG(LogAlienMuseum, Log, TEXT("Chamber anchored and saved (anchor %s)"), *Record->AnchorUuid);
					}
				}),
				SaveStartResult);
		}),
		StartResult);
}

void UMuseumPersistenceComponent::ReleaseAnchor(AAlienChamber* Chamber)
{
	if (!Chamber)
	{
		return;
	}

	TArray<UOculusXRAnchorComponent*> Anchors;
	Chamber->GetComponents<UOculusXRAnchorComponent>(Anchors);
	for (UOculusXRAnchorComponent* Anchor : Anchors)
	{
		// Stop the anchor from pinning the chamber immediately; destroy it once erased
		// (destroying the component also destroys the runtime anchor).
		Anchor->SetComponentTickEnabled(false);

		if (!Anchor->HasValidHandle() || !AreAnchorsAvailable())
		{
			Anchor->DestroyComponent();
			continue;
		}

		TWeakObjectPtr<UOculusXRAnchorComponent> WeakAnchor(Anchor);
		EOculusXRAnchorResult::Type StartResult;
		const bool bStarted = FOculusXRAnchors::EraseAnchors(TArray<UOculusXRAnchorComponent*>{ Anchor },
			FOculusXREraseAnchorsDelegate::CreateLambda([WeakAnchor](EOculusXRAnchorResult::Type Result, const TArray<UOculusXRAnchorComponent*>&, const TArray<FOculusXRUInt64>&, const TArray<FOculusXRUUID>&)
			{
				if (!UOculusXRAnchorBPFunctionLibrary::IsAnchorResultSuccess(Result))
				{
					UE_LOG(LogAlienMuseum, Warning, TEXT("Erasing a spatial anchor failed (result %d)"), static_cast<int32>(Result));
				}
				if (WeakAnchor.IsValid())
				{
					WeakAnchor->DestroyComponent();
				}
			}),
			StartResult);
		if (!bStarted)
		{
			Anchor->DestroyComponent();
		}
	}

	if (FMuseumChamberRecord* Record = FindRecord(Chamber->GetChamberId()))
	{
		Record->AnchorUuid.Reset();
		WriteSave();
	}
}

void UMuseumPersistenceComponent::ForgetChamber(AAlienChamber* Chamber)
{
	if (!Chamber)
	{
		return;
	}
	ReleaseAnchor(Chamber);
	if (UMuseumSaveGame* Save = GetSave())
	{
		const FGuid Id = Chamber->GetChamberId();
		Save->Chambers.RemoveAll([&Id](const FMuseumChamberRecord& Record) { return Record.ChamberId == Id; });
		WriteSave();
	}
}

void UMuseumPersistenceComponent::ClearAll()
{
	if (UMuseumSaveGame* Save = GetSave())
	{
		Save->Chambers.Reset();
		WriteSave();
	}
}

// ---------------------------------------------------------------------------------------------
// Restoring
// ---------------------------------------------------------------------------------------------

void UMuseumPersistenceComponent::RestoreMuseum(TSubclassOf<AAlienChamber> ChamberClass, FOnChamberRestored OnChamberRestored, FOnRestoreComplete OnComplete)
{
	if (bRestoring)
	{
		return;
	}
	bRestoring = true;
	bDiscoveryComplete = false;
	NumRestored = 0;
	RestoreChamberClass = ChamberClass;
	RestoredCallback = MoveTemp(OnChamberRestored);
	CompleteCallback = MoveTemp(OnComplete);
	PendingRestore.Reset();
	PendingLocalization.Reset();

	UMuseumSaveGame* Save = GetSave();
	UWorld* World = GetWorld();
	if (!ChamberClass || !Save || !World || Save->Chambers.Num() == 0)
	{
		FinishRestore();
		return;
	}

	// Editor / no headset: trust the saved transforms.
	if (!AreAnchorsAvailable())
	{
		for (const FMuseumChamberRecord& Record : Save->Chambers)
		{
			FTransform Transform = Record.LastTransform;
			Transform.SetScale3D(FVector::OneVector);
			FActorSpawnParameters Params;
			Params.Owner = GetOwner();
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AAlienChamber* Chamber = World->SpawnActor<AAlienChamber>(ChamberClass, Transform, Params))
			{
				Chamber->SetChamberId(Record.ChamberId);
				Chamber->SetChamberScale(Record.Scale);
				if (!Record.InnerSize.IsNearlyZero())
				{
					Chamber->SetInnerSize(Record.InnerSize);
				}
				++NumRestored;
				if (RestoredCallback)
				{
					RestoredCallback(Chamber, Record.AlienId);
				}
			}
		}
		FinishRestore();
		return;
	}

	// Headset: find the saved anchors in this room.
	TArray<FOculusXRUUID> Uuids;
	for (const FMuseumChamberRecord& Record : Save->Chambers)
	{
		if (Record.AnchorUuid.IsEmpty())
		{
			continue;
		}
		const FOculusXRUUID Uuid = UOculusXRAnchorBPFunctionLibrary::StringToAnchorUUID(Record.AnchorUuid);
		if (Uuid.IsValidUUID())
		{
			Uuids.Add(Uuid);
			PendingRestore.Add(Uuid.ToString(), Record);
		}
	}
	if (Uuids.Num() == 0)
	{
		FinishRestore();
		return;
	}

	DiscoveryFilter = NewObject<UOculusXRSpaceDiscoveryIdsFilter>(this);
	DiscoveryFilter->Uuids = Uuids;
	FOculusXRSpaceDiscoveryInfo DiscoveryInfo;
	DiscoveryInfo.Filters.Add(DiscoveryFilter.Get());

	EOculusXRAnchorResult::Type StartResult;
	const bool bStarted = FOculusXRAnchors::DiscoverAnchors(DiscoveryInfo,
		FOculusXRDiscoverAnchorsResultsDelegate::CreateWeakLambda(this, [this](const TArray<FOculusXRAnchorsDiscoverResult>& Results)
		{
			HandleDiscoverResults(Results);
		}),
		FOculusXRDiscoverAnchorsCompleteDelegate::CreateWeakLambda(this, [this](EOculusXRAnchorResult::Type Result)
		{
			HandleDiscoverComplete(static_cast<int32>(Result));
		}),
		StartResult);

	if (!bStarted)
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("Anchor discovery could not start (result %d)"), static_cast<int32>(StartResult));
		FinishRestore();
		return;
	}

	UE_LOG(LogAlienMuseum, Log, TEXT("Looking for %d saved chamber anchor(s)"), Uuids.Num());
	World->GetTimerManager().SetTimer(PollTimer, this, &UMuseumPersistenceComponent::PollPendingLocalization, 0.2f, true);
	World->GetTimerManager().SetTimer(TimeoutTimer, this, &UMuseumPersistenceComponent::FinishRestore, RestoreTimeout, false);
}

void UMuseumPersistenceComponent::HandleDiscoverResults(const TArray<FOculusXRAnchorsDiscoverResult>& Results)
{
	if (!bRestoring)
	{
		return;
	}
	for (const FOculusXRAnchorsDiscoverResult& Result : Results)
	{
		FMuseumChamberRecord Record;
		if (!PendingRestore.RemoveAndCopyValue(Result.UUID.ToString(), Record))
		{
			continue;
		}

		// Ask the runtime to keep this anchor located.
		EOculusXRAnchorResult::Type StatusResult;
		FOculusXRAnchors::SetComponentStatus(Result.Space.GetValue(), EOculusXRSpaceComponentType::Locatable, true, 0.f,
			FOculusXRAnchorSetComponentStatusDelegate(), StatusResult);

		AActor* Spawned = UOculusXRAnchorBPFunctionLibrary::SpawnActorWithAnchorHandle(this, Result.Space, Result.UUID,
			EOculusXRSpaceStorageLocation::Local, RestoreChamberClass, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		AAlienChamber* Chamber = Cast<AAlienChamber>(Spawned);
		if (!Chamber)
		{
			continue;
		}
		Chamber->SetChamberId(Record.ChamberId);
		Chamber->SetChamberScale(Record.Scale);
		if (!Record.InnerSize.IsNearlyZero())
		{
			Chamber->SetInnerSize(Record.InnerSize);
		}
		Chamber->SetActorHiddenInGame(true); // shown once the anchor reports a valid pose

		FPendingLocalization Pending;
		Pending.Chamber = Chamber;
		Pending.Handle = Result.Space.GetValue();
		Pending.AlienId = Record.AlienId;
		PendingLocalization.Add(Pending);
	}
}

void UMuseumPersistenceComponent::HandleDiscoverComplete(int32 ResultCode)
{
	bDiscoveryComplete = true;
	UE_LOG(LogAlienMuseum, Log, TEXT("Anchor discovery finished (result %d, %d anchor(s) not found in this room)"), ResultCode, PendingRestore.Num());
	if (PendingLocalization.Num() == 0)
	{
		FinishRestore();
	}
}

void UMuseumPersistenceComponent::PollPendingLocalization()
{
	for (int32 i = PendingLocalization.Num() - 1; i >= 0; --i)
	{
		FPendingLocalization& Pending = PendingLocalization[i];
		Pending.Age += 0.2f;
		AAlienChamber* Chamber = Pending.Chamber.Get();
		if (!Chamber)
		{
			PendingLocalization.RemoveAt(i);
			continue;
		}

		FTransform AnchorTransform;
		FOculusXRAnchorLocationFlags Flags;
		const bool bLocated = UOculusXRAnchorBPFunctionLibrary::TryGetAnchorTransformByHandle(FOculusXRUInt64(Pending.Handle), AnchorTransform, Flags) && Flags.IsValid();
		if (bLocated || Pending.Age >= RestoreTimeout)
		{
			if (bLocated)
			{
				Chamber->SetActorLocationAndRotation(AnchorTransform.GetLocation(), FRotator(0.f, AnchorTransform.Rotator().Yaw, 0.f));
			}
			Chamber->SetActorHiddenInGame(false);
			++NumRestored;
			if (RestoredCallback)
			{
				RestoredCallback(Chamber, Pending.AlienId);
			}
			PendingLocalization.RemoveAt(i);
		}
	}

	if (bDiscoveryComplete && PendingLocalization.Num() == 0)
	{
		FinishRestore();
	}
}

void UMuseumPersistenceComponent::FinishRestore()
{
	if (!bRestoring)
	{
		return;
	}

	// Deliver anything still waiting (timeout).
	for (const FPendingLocalization& Pending : PendingLocalization)
	{
		if (AAlienChamber* Chamber = Pending.Chamber.Get())
		{
			Chamber->SetActorHiddenInGame(false);
			++NumRestored;
			if (RestoredCallback)
			{
				RestoredCallback(Chamber, Pending.AlienId);
			}
		}
	}
	PendingLocalization.Reset();

	bRestoring = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
		World->GetTimerManager().ClearTimer(TimeoutTimer);
	}
	DiscoveryFilter = nullptr;

	UE_LOG(LogAlienMuseum, Log, TEXT("Museum restore finished: %d chamber(s)"), NumRestored);
	FOnRestoreComplete Complete = MoveTemp(CompleteCallback);
	RestoredCallback = nullptr;
	CompleteCallback = nullptr;
	if (Complete)
	{
		Complete(NumRestored);
	}
}

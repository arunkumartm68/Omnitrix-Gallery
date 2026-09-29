// Alien Museum - remembers chambers between sessions using Meta spatial anchors.
//
// Flow on a Quest:
//   chamber placed/released -> CreateSpatialAnchor -> SaveAnchors -> UUID written to the save game
//   chamber grabbed         -> anchor erased (otherwise the anchor would pin the chamber in place)
//   next launch             -> DiscoverAnchors(saved UUIDs) -> chamber spawned on each anchor
//
// Without a headset (editor testing) the last world transform is saved and restored instead.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MR/MuseumSaveGame.h"
#include "MuseumPersistenceComponent.generated.h"

class AAlienChamber;
class UMuseumSaveGame;
class UOculusXRSpaceDiscoveryIdsFilter;
struct FOculusXRAnchorsDiscoverResult;

UCLASS(ClassGroup = (AlienMuseum), meta = (BlueprintSpawnableComponent))
class BEN10_API UMuseumPersistenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMuseumPersistenceComponent();

	using FOnChamberRestored = TFunction<void(AAlienChamber* /*Chamber*/, FName /*AlienId*/)>;
	using FOnRestoreComplete = TFunction<void(int32 /*NumRestored*/)>;

	/** Loads the save file and recreates the saved chambers. */
	void RestoreMuseum(TSubclassOf<AAlienChamber> ChamberClass, FOnChamberRestored OnChamberRestored, FOnRestoreComplete OnComplete);

	/** Saves the chamber (alien, scale) and anchors it in the real room. Call after spawn/release/alien change. */
	void CommitChamber(AAlienChamber* Chamber);

	/** Removes the chamber's anchor so it can be moved. The record is kept until CommitChamber. */
	void ReleaseAnchor(AAlienChamber* Chamber);

	/** Chamber deleted: erase its anchor and record. */
	void ForgetChamber(AAlienChamber* Chamber);

	/** Deletes every saved chamber and anchor. */
	UFUNCTION(BlueprintCallable, Category = "Museum|Persistence")
	void ClearAll();

	UFUNCTION(BlueprintPure, Category = "Museum|Persistence")
	bool AreAnchorsAvailable() const;

	/** The save file was written before cases had life size (its cases are made life size once, when restored). */
	bool IsSaveFromBeforeLifeSize();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Persistence")
	FString SaveSlotName = TEXT("AlienMuseum");

	/** Use Meta spatial anchors when a headset is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Persistence")
	bool bUseSpatialAnchors = true;

	/** Give up waiting for anchors after this many seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Persistence")
	float RestoreTimeout = 10.f;

private:
	UMuseumSaveGame* GetSave();
	void WriteSave();
	int32 LoadedVersion = -1; // the save file's version as it was loaded (-1: not loaded yet)
	FMuseumChamberRecord* FindRecord(const FGuid& ChamberId);
	FMuseumChamberRecord& FindOrAddRecord(const FGuid& ChamberId);

	void CreateAndSaveAnchor(AAlienChamber* Chamber);

	void HandleDiscoverResults(const TArray<FOculusXRAnchorsDiscoverResult>& Results);
	void HandleDiscoverComplete(int32 ResultCode);
	void PollPendingLocalization();
	void FinishRestore();

	UPROPERTY(Transient)
	TObjectPtr<UMuseumSaveGame> SaveGame;

	UPROPERTY(Transient)
	TObjectPtr<UOculusXRSpaceDiscoveryIdsFilter> DiscoveryFilter;

	/** Chambers spawned on an anchor that is not localised yet (alien spawned once it is). */
	struct FPendingLocalization
	{
		TWeakObjectPtr<AAlienChamber> Chamber;
		uint64 Handle = 0;
		FName AlienId;
		float Age = 0.f;
	};
	TArray<FPendingLocalization> PendingLocalization;

	TMap<FString, FMuseumChamberRecord> PendingRestore;
	TSubclassOf<AAlienChamber> RestoreChamberClass;
	FOnChamberRestored RestoredCallback;
	FOnRestoreComplete CompleteCallback;
	FTimerHandle PollTimer;
	FTimerHandle TimeoutTimer;
	int32 NumRestored = 0;
	bool bRestoring = false;
	bool bDiscoveryComplete = false;
};

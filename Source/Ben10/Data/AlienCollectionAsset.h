// Alien Museum - the list of aliens shown in the Alien Collection panel.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AlienCollectionAsset.generated.h"

class UAlienDataAsset;

UCLASS(BlueprintType)
class BEN10_API UAlienCollectionAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Aliens in display order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collection")
	TArray<TObjectPtr<UAlienDataAsset>> Aliens;

	UFUNCTION(BlueprintPure, Category = "Collection")
	UAlienDataAsset* FindById(FName AlienId) const;
};

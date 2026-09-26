// Alien Museum - what is saved between sessions.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MuseumSaveGame.generated.h"

USTRUCT(BlueprintType)
struct BEN10_API FMuseumChamberRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	FGuid ChamberId;

	/** UUID of the Meta spatial anchor holding the chamber in the real room (empty = none). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	FString AnchorUuid;

	/** Alien living in the chamber (NAME_None = empty chamber). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	FName AlienId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	float Scale = 1.f;

	/** Inside size of the glass (depth, width, height in cm at scale 1). Zero = the chamber's default. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	FVector InnerSize = FVector::ZeroVector;

	/** Last known world transform. Only trusted without a headset (editor testing). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	FTransform LastTransform;
};

UCLASS()
class BEN10_API UMuseumSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	int32 Version = 1;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Museum")
	TArray<FMuseumChamberRecord> Chambers;
};

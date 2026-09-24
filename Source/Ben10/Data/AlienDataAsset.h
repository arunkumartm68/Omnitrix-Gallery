// Alien Museum - data describing one alien species (identity, look, behaviour).
//
// Create one per alien: Content Browser > Add > Miscellaneous > Data Asset > AlienDataAsset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AlienDataAsset.generated.h"

class AAlienCharacter;

/** Silhouette of the built-in placeholder alien. */
UENUM(BlueprintType)
enum class EAlienBodyShape : uint8
{
	Blob,   // round body, big head
	Tall,   // thin cylindrical body
	Squat   // wide and short
};

UCLASS(BlueprintType)
class BEN10_API UAlienDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------- Identity ----------

	/** Unique id, used for saving which alien lives in which chamber. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName AlienId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Species;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText HomePlanet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	// ---------- Spawning ----------

	/** Character class to spawn. Leave empty to use the built-in placeholder alien (AAlienCharacter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	TSoftClassPtr<AAlienCharacter> CharacterClass;

	// ---------- Placeholder appearance ----------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	EAlienBodyShape BodyShape = EAlienBodyShape::Blob;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor SkinColor = FLinearColor(0.30f, 0.85f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor AccentColor = FLinearColor(0.08f, 0.35f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor EyeColor = FLinearColor(1.0f, 0.9f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = 1, ClampMax = 3))
	int32 EyeCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bHasAntennae = true;

	/** Floats above the ground instead of walking on feet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bHovers = false;

	/** Standing height at chamber scale 1. The default chamber fits aliens up to ~45 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = 15, ClampMax = 45, Units = "cm"))
	float Height = 32.f;

	// ---------- Behaviour ----------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 5, ClampMax = 80, Units = "cm/s"))
	float WalkSpeed = 22.f;

	/** Random idle time range in seconds (min, max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	FVector2D IdleDuration = FVector2D(1.5f, 4.0f);

	/** Chance to look around instead of walking when an idle period ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 0, ClampMax = 1))
	float LookAroundChance = 0.35f;

	/** Chance to come to the glass when the player walks up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 0, ClampMax = 1))
	float Curiosity = 0.7f;

	/** Distance at which the alien notices the player's head. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (Units = "cm"))
	float NoticePlayerDistance = 160.f;

	/** 0 = calm, 1 = hyperactive (bob speed, hops, antenna wiggle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 0, ClampMax = 1))
	float Energy = 0.5f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};

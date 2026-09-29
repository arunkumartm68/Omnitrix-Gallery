// Alien Museum - small shared types.

#pragma once

#include "CoreMinimal.h"
#include "MuseumTypes.generated.h"

/** What kind of real-world surface a placement ray hit. */
UENUM(BlueprintType)
enum class EMuseumSurfaceType : uint8
{
	None,
	Floor,
	Table,     // any horizontal furniture top (table, storage, bed, couch seat...)
	Other
};

USTRUCT(BlueprintType)
struct BEN10_API FMuseumSurfaceHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	bool bHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	FVector Normal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	EMuseumSurfaceType Surface = EMuseumSurfaceType::None;

	/** MRUK label of the hit anchor (FLOOR, TABLE, ...) or "EDITOR" when using the editor fallback. */
	UPROPERTY(BlueprintReadOnly, Category = "Museum")
	FString Label;

	/** True when the surface is flat and facing up, so a chamber can stand on it. */
	bool IsPlaceable() const { return bHit && Normal.Z > 0.7f && Surface != EMuseumSurfaceType::None; }
};

/** Where a case for an alien goes and how big it is, as AMuseumDirector::FitLifeSize works it out. */
struct FMuseumCaseFit
{
	FVector Location = FVector::ZeroVector;  // bottom centre
	FVector InnerSize = FVector::ZeroVector; // its own inside (cm at scale 1)
	float Scale = 1.f;
	FString Why;                             // what kept it smaller than life size ("the ceiling is too low")
	bool bFits = false;                      // false: not even the museum's normal size fits there (it goes there anyway)
};

/** Collision settings shared by museum actors. */
namespace MuseumCollision
{
	/** Pointer rays use the Visibility channel. */
	inline constexpr ECollisionChannel PointerChannel = ECC_Visibility;
}

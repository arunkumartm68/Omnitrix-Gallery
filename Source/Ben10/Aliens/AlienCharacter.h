// Alien Museum - an autonomous alien that lives inside an AAlienChamber.
//
// Data (UAlienDataAsset) -> AAlienCharacter (body + movement) -> AAlienAIController (decisions).
// Movement uses the CharacterMovementComponent with direct steering towards points picked inside
// the chamber, so no navmesh is needed. That matters because chambers move at runtime and
// rebuilding navigation on Quest would be expensive.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AlienCharacter.generated.h"

class UAlienDataAsset;
class UAlienAppearanceComponent;
class UStaticMeshComponent;
class AAlienChamber;

UCLASS()
class BEN10_API AAlienCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAlienCharacter();

	/** Builds the body, sizes the capsule and sets speeds. Call right after spawning. */
	UFUNCTION(BlueprintCallable, Category = "Alien")
	void InitializeAlien(UAlienDataAsset* InData, AAlienChamber* InChamber);

	UFUNCTION(BlueprintPure, Category = "Alien")
	UAlienDataAsset* GetAlienData() const { return AlienData; }

	UFUNCTION(BlueprintPure, Category = "Alien")
	AAlienChamber* GetHomeChamber() const { return HomeChamber.Get(); }

	// ---- Movement API used by AAlienAIController ----

	/** Walk in a straight line to WorldTarget (Z ignored). SpeedScale 0..1. */
	void MoveToPoint(const FVector& WorldTarget, float SpeedScale = 1.f);
	void StopMoving();
	bool IsMoving() const { return bHasMoveTarget; }

	/** True when the current move made no progress for a while (blocked by an obstacle). */
	bool IsStuck() const { return bStuck; }

	/** Rotate in place to face WorldPoint (only while not walking). */
	void TurnToward(const FVector& WorldPoint);

	void SetLookTarget(const FVector& WorldPoint);
	void ClearLookTarget();
	void SetExcited(bool bInExcited) { bExcited = bInExcited; }

	/** Small happy jump. */
	void Hop();

	/** While the chamber is carried the alien freezes and rides along. */
	void SetHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "Alien")
	bool IsHeld() const { return bIsHeld; }

	/** Current size relative to the data asset (follows the chamber scale). */
	float GetScaleFactor() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UAlienAppearanceComponent> Appearance;

	/** Cheap fake contact shadow: a soft dark disc under the feet (no dynamic shadows on Quest). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UStaticMeshComponent> ContactShadow;

	/** Data used when the alien is placed in a level by hand (normally set by InitializeAlien). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UAlienDataAsset> AlienData;

private:
	void ApplyScaleDependentSettings();
	void UpdateMovement(float DeltaSeconds);
	void UpdateTurning(float DeltaSeconds);
	void UpdateContainment(float DeltaSeconds);

	TWeakObjectPtr<AAlienChamber> HomeChamber;

	FVector MoveTarget = FVector::ZeroVector;
	float MoveSpeedScale = 1.f;
	bool bHasMoveTarget = false;
	bool bStuck = false;
	float ProgressTimer = 0.f;
	float LastDistance = 0.f;
	float StuckTime = 0.f;

	FVector LookTarget = FVector::ZeroVector;
	bool bHasLookTarget = false;

	float DesiredYaw = 0.f;
	bool bHasDesiredYaw = false;

	bool bExcited = false;
	bool bIsHeld = false;
	bool bAppearanceBuilt = false;
	float ContainmentTimer = 0.f;
};

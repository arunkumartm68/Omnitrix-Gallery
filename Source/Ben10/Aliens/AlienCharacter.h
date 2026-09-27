// Alien Museum - an autonomous alien that lives inside an AAlienChamber.
//
// Data (UAlienDataAsset) -> AAlienCharacter (body + movement) -> AAlienAIController (decisions),
// with UAlienActionComponent for the signature moves (Cannonbolt's roll, XLR8's dash...).
// Movement uses the CharacterMovementComponent with direct steering towards points picked inside
// the chamber, so no navmesh is needed. That matters because chambers move at runtime and
// rebuilding navigation on Quest would be expensive.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AlienCharacter.generated.h"

class UAlienDataAsset;
class UAlienAppearanceComponent;
class UAlienActionComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
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

	UFUNCTION(BlueprintPure, Category = "Alien")
	UAlienAppearanceComponent* GetAppearance() const { return Appearance; }

	UFUNCTION(BlueprintPure, Category = "Alien")
	UAlienActionComponent* GetActions() const { return Actions; }

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

	/** Not walking freely in its case: carried with the case, held by the player or floating back. */
	UFUNCTION(BlueprintPure, Category = "Alien")
	bool IsHeld() const { return bIsHeld || bExamined || bReturning; }

	// ---- Taken out of the case by the player, like holding a pet ----

	/** Picked up: it stops walking and colliding, the AI pauses, and it goes wherever the hand holds it. */
	void BeginExamine();

	/** Where the hand holds it this frame (followed smoothly). Zoom scales it (1 = its size in the case). */
	void SetExamineTarget(const FVector& Location, const FQuat& Rotation, float Zoom);

	/** Let go: over its case it drops back in, anywhere else it floats home first. */
	void EndExamine();

	UFUNCTION(BlueprintPure, Category = "Alien")
	bool IsExamined() const { return bExamined; }

	/** Held by the player or floating back to its case. */
	bool IsOutOfCase() const { return bExamined || bReturning; }

	/** A pulsing ring at its feet while a pointer is on it ("you can pick me up"). */
	void SetTargeted(bool bInTargeted);

	/** Current size relative to the data asset (follows the chamber scale). */
	float GetScaleFactor() const;

	/** Multiplies walking speed, acceleration and turn rate (rolls, dashes...). 1 = normal. */
	void SetSpeedMultiplier(float Multiplier);

	/** How hard the alien shoves the loose props it bumps into. 1 = normal. */
	void SetPushStrength(float Multiplier);

	/** Shrinks the contact shadow (1 = normal), e.g. while flying high above it. */
	void SetContactShadowScale(float Factor);

	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UAlienAppearanceComponent> Appearance;

	/** Signature moves and their effects. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UAlienActionComponent> Actions;

	/** Cheap fake contact shadow: a soft dark disc under the feet (no dynamic shadows on Quest). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UStaticMeshComponent> ContactShadow;

	/** Data used when the alien is placed in a level by hand (normally set by InitializeAlien). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alien")
	TObjectPtr<UAlienDataAsset> AlienData;

private:
	void ApplyScaleDependentSettings();

	/** Puts the body's feet (and the contact shadow) on the floor under the capsule. */
	void PlaceBodyOnFloor();

	void UpdateExamine(float DeltaSeconds);
	void UpdateReturn(float DeltaSeconds);

	/** Back in the case at this spot (kept inside the glass); it falls to the floor from there. */
	void SettleInCase(const FVector& WorldLocation);

	/** Where a returning alien flies to: above the middle of its case. */
	FVector GetReturnPoint() const;
	float GetCaseScale() const;
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
	bool bExamined = false;
	bool bReturning = false;
	bool bTargeted = false;
	bool bCelebrateOnLanding = false;
	float CapsuleHalfHeightLocal = 16.f;
	FVector ExamineLocation = FVector::ZeroVector;
	FQuat ExamineRotation = FQuat::Identity;
	float ExamineZoom = 1.f;
	FTransform ReturnFrom = FTransform::Identity;
	float ReturnTime = 0.f;
	float ReturnDuration = 1.f;
	float TargetTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TargetRing;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TargetRingMaterial;

	bool bAppearanceBuilt = false;
	float ContainmentTimer = 0.f;
	float SpeedMultiplier = 1.f;
	FVector ContactShadowScale = FVector(0.2f);
};

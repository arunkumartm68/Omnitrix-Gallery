// Alien Museum - lightweight state machine that drives an AAlienCharacter.
//
// A Behavior Tree would work too, but a small C++ state machine that "thinks" a few times per
// second is cheaper on Quest and easy to tune through UAlienDataAsset.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AlienAIController.generated.h"

class AAlienCharacter;

UENUM(BlueprintType)
enum class EAlienState : uint8
{
	Idle,
	Wander,
	LookAround,
	ReactToPlayer,
	Held,
	Performing UMETA(ToolTip = "Doing one of its signature moves (UAlienActionComponent)")
};

UCLASS()
class BEN10_API AAlienAIController : public AAIController
{
	GENERATED_BODY()

public:
	AAlienAIController();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Alien|AI")
	EAlienState GetAlienState() const { return State; }

	/** Called by the alien when its chamber is picked up / put down. */
	void NotifyHeld(bool bHeld);

	/** Seconds between decisions. Movement itself stays smooth every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien|AI", meta = (ClampMin = 0.05))
	float ThinkInterval = 0.2f;

	/** Seconds before the same alien may react to the player again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien|AI")
	float ReactCooldown = 10.f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void Think();
	void EnterState(EAlienState NewState);
	void ChooseNextActivity();
	bool GetPlayerHead(FVector& OutHead) const;

	UPROPERTY(Transient)
	TObjectPtr<AAlienCharacter> Alien;

	FRandomStream Rng;
	EAlienState State = EAlienState::Idle;
	float StateTime = 0.f;
	float StateDuration = 0.f;
	float ThinkAccumulator = 0.f;
	float ReactCooldownRemaining = 0.f;
	float NextLookAroundTime = 0.f;
	int32 LookAroundSteps = 0;
	bool bPlayerWasNear = false;
	bool bHopped = false;
};

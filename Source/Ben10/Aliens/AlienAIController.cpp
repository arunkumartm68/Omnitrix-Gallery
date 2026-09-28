// Alien Museum - lightweight state machine that drives an AAlienCharacter.

#include "Aliens/AlienAIController.h"
#include "Aliens/AlienCharacter.h"
#include "Aliens/AlienActionComponent.h"
#include "Aliens/AlienSoundComponent.h"
#include "Chamber/AlienChamber.h"
#include "Data/AlienDataAsset.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"

AAlienAIController::AAlienAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	bSetControlRotationFromPawnOrientation = false;
	Rng.GenerateNewSeed();
}

void AAlienAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Alien = Cast<AAlienCharacter>(InPawn);
	ThinkAccumulator = Rng.FRandRange(0.f, ThinkInterval); // spread the thinking of several aliens over frames
	EnterState(EAlienState::Idle);
}

void AAlienAIController::OnUnPossess()
{
	Alien = nullptr;
	Super::OnUnPossess();
}

void AAlienAIController::NotifyHeld(bool bHeld)
{
	EnterState(bHeld ? EAlienState::Held : EAlienState::Idle);
}

void AAlienAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	StateTime += DeltaSeconds;
	ReactCooldownRemaining = FMath::Max(0.f, ReactCooldownRemaining - DeltaSeconds);

	ThinkAccumulator += DeltaSeconds;
	if (ThinkAccumulator >= ThinkInterval)
	{
		ThinkAccumulator = 0.f;
		Think();
	}
}

bool AAlienAIController::GetPlayerHead(FVector& OutHead) const
{
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		OutHead = Camera->GetCameraLocation();
		return true;
	}
	return false;
}

void AAlienAIController::EnterState(EAlienState NewState)
{
	State = NewState;
	StateTime = 0.f;
	bHopped = false;
	if (!Alien)
	{
		return;
	}

	const UAlienDataAsset* Data = Alien->GetAlienData();
	Alien->SetExcited(false);

	switch (NewState)
	{
	case EAlienState::Idle:
	{
		Alien->StopMoving();
		const FVector2D Range = Data ? Data->IdleDuration : FVector2D(1.5f, 4.f);
		StateDuration = Rng.FRandRange(Range.X, FMath::Max(Range.X, Range.Y));
		break;
	}

	case EAlienState::Wander:
	{
		AAlienChamber* Chamber = Alien->GetHomeChamber();
		FVector Target;
		if (Chamber && Chamber->FindRandomWanderPoint(Alien, Rng, Target))
		{
			Alien->ClearLookTarget();
			Alien->MoveToPoint(Target, Rng.FRandRange(0.6f, 1.f));
			StateDuration = 12.f; // safety timeout
		}
		else
		{
			EnterState(EAlienState::Idle);
		}
		break;
	}

	case EAlienState::LookAround:
		Alien->StopMoving();
		Alien->ClearLookTarget();
		LookAroundSteps = Rng.RandRange(2, 4);
		NextLookAroundTime = 0.f;
		StateDuration = 8.f;
		break;

	case EAlienState::ReactToPlayer:
	{
		Alien->SetExcited(true);
		StateDuration = Rng.FRandRange(4.f, 7.f);
		ReactCooldownRemaining = ReactCooldown;
		if (UAlienSoundComponent* Voice = Alien->GetSounds())
		{
			Voice->PlayVoice(EAlienVoice::Alert); // it noticed you
		}
		break;
	}

	case EAlienState::Held:
		Alien->StopMoving();
		StateDuration = 0.f;
		break;

	case EAlienState::Performing:
		StateDuration = 15.f; // safety timeout; the move normally ends long before
		break;
	}
}

void AAlienAIController::ChooseNextActivity()
{
	const UAlienDataAsset* Data = Alien ? Alien->GetAlienData() : nullptr;

	// Now and then a signature move (Cannonbolt rolls, XLR8 dashes...).
	UAlienActionComponent* Moves = Alien ? Alien->GetActions() : nullptr;
	if (Data && Moves && Moves->HasActions() && Rng.FRand() < Data->ActionChance && Moves->StartRandomAction(false))
	{
		EnterState(EAlienState::Performing);
		return;
	}

	const float LookChance = Data ? Data->LookAroundChance : 0.35f;
	EnterState(Rng.FRand() < LookChance ? EAlienState::LookAround : EAlienState::Wander);
}

void AAlienAIController::Think()
{
	if (!Alien || State == EAlienState::Held)
	{
		return;
	}

	const UAlienDataAsset* Data = Alien->GetAlienData();
	AAlienChamber* Chamber = Alien->GetHomeChamber();
	const float Scale = Alien->GetScaleFactor();
	UAlienActionComponent* Moves = Alien->GetActions();

	// ---- Notice the player walking up ----
	FVector Head;
	const bool bHasHead = GetPlayerHead(Head);
	const float NoticeDistance = (Data ? Data->NoticePlayerDistance : 160.f) * FMath::Max(1.f, Scale);
	const bool bPlayerNear = bHasHead && FVector::Dist(Head, Alien->GetActorLocation()) < NoticeDistance;

	// ---- A move plays out by itself ----
	if (State == EAlienState::Performing)
	{
		bPlayerWasNear = bPlayerNear;
		if (!Moves || !Moves->IsPerforming() || StateTime >= StateDuration)
		{
			if (Moves)
			{
				Moves->StopAction();
			}
			EnterState(EAlienState::Idle);
		}
		return;
	}

	if (bPlayerNear && !bPlayerWasNear && State != EAlienState::ReactToPlayer && ReactCooldownRemaining <= 0.f)
	{
		const float Curiosity = Data ? Data->Curiosity : 0.7f;
		if (Rng.FRand() < Curiosity)
		{
			bPlayerWasNear = bPlayerNear;
			EnterState(EAlienState::ReactToPlayer);
			return;
		}
	}
	bPlayerWasNear = bPlayerNear;

	switch (State)
	{
	case EAlienState::Idle:
		// Glance at the visitor when they are close, otherwise look at nothing in particular.
		if (bPlayerNear)
		{
			Alien->SetLookTarget(Head);
		}
		else
		{
			Alien->ClearLookTarget();
		}
		if (StateTime >= StateDuration)
		{
			ChooseNextActivity();
		}
		break;

	case EAlienState::Wander:
		if (!Alien->IsMoving() || StateTime >= StateDuration)
		{
			EnterState(EAlienState::Idle);
		}
		else if (Alien->IsStuck())
		{
			// Blocked by an obstacle: try another spot.
			EnterState(EAlienState::Wander);
		}
		break;

	case EAlienState::LookAround:
		if (StateTime >= NextLookAroundTime)
		{
			if (LookAroundSteps-- <= 0 || StateTime >= StateDuration)
			{
				EnterState(EAlienState::Idle);
				break;
			}
			const float Yaw = Alien->GetActorRotation().Yaw + Rng.FRandRange(-120.f, 120.f);
			const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
			Alien->TurnToward(Alien->GetActorLocation() + Dir * 100.f);
			// Look slightly up or down while turning.
			Alien->SetLookTarget(Alien->GetActorLocation() + Dir * 100.f + FVector(0.f, 0.f, Rng.FRandRange(-20.f, 40.f)));
			NextLookAroundTime = StateTime + Rng.FRandRange(0.8f, 1.6f);
		}
		break;

	case EAlienState::ReactToPlayer:
		if (!bHasHead || !Chamber)
		{
			EnterState(EAlienState::Idle);
			break;
		}
		Alien->SetLookTarget(Head);
		{
			// Walk to the glass nearest to the player, then face them and hop.
			const float Margin = Alien->GetCapsuleComponent()->GetScaledCapsuleRadius() + 2.f * Scale;
			const FVector GlassPoint = Chamber->ClampToMovementBounds(Head, Margin);
			if (FVector::Dist2D(GlassPoint, Alien->GetActorLocation()) > 6.f * Scale && !Alien->IsStuck())
			{
				if (!Alien->IsMoving())
				{
					Alien->MoveToPoint(GlassPoint, 1.f);
				}
			}
			else
			{
				Alien->StopMoving();
				Alien->TurnToward(Head);
				// At the glass: show off (Four Arms flexes, Cannonbolt rolls...), else a happy hop.
				if (!bHopped && Moves && Moves->HasActions() && Moves->StartRandomAction(true))
				{
					EnterState(EAlienState::Performing);
					break;
				}
				const float Energy = Data ? Data->Energy : 0.5f;
				if (!bHopped || Rng.FRand() < 0.08f * Energy)
				{
					Alien->Hop();
					bHopped = true;
				}
			}
		}
		if (StateTime >= StateDuration || !bPlayerNear)
		{
			EnterState(EAlienState::Idle);
		}
		break;

	case EAlienState::Held:
	case EAlienState::Performing:
		break;
	}
}

// Alien Museum - an alien's voice, footsteps and move sounds, played through UMuseumAudio.
//
// Its voice and footsteps come from its data asset (UAlienDataAsset::Sounds), the moves' sounds from the
// museum's library by name. Every sound follows the alien and is heard through its case's glass while it
// is inside. On its own it calls out now and then - rarely, and only when UMuseumAudio lets it (one of the
// few aliens nearest to you, nobody else just called); the AI, the moves and the player's hand ask it for
// the rest: an alert when a visitor walks up, effort in a move, a squeal when picked up.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AlienSoundComponent.generated.h"

class AAlienCharacter;
class UAlienDataAsset;
class UAudioComponent;
class UMuseumAudio;

/** Which of an alien's voices (UAlienDataAsset::Sounds). */
UENUM(BlueprintType)
enum class EAlienVoice : uint8
{
	Call,
	Alert,
	Effort,
	Held
};

UCLASS(ClassGroup = (AlienMuseum))
class BEN10_API UAlienSoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAlienSoundComponent();

	/** Reads its sounds and starts its loop (if it has one). Call after the body is built. */
	void Setup(const UAlienDataAsset* Data);

	/** One of its voices. False if it has none of that kind or is out of hearing. */
	bool PlayVoice(EAlienVoice Voice, float Volume = 1.f);

	/** A library sound (e.g. "Move.Pounce.Leap") that follows the alien. */
	UAudioComponent* PlayEffect(FName Name, float Volume = 1.f, float Pitch = 1.f);

	/** A library sound at a spot in its case (a ball popping, a bounce off the glass). */
	UAudioComponent* PlayEffectAt(FName Name, const FVector& Location, float Volume = 1.f);

	/** A step (a splash in water), only when you are close enough to hear it. */
	void PlayFootstep(float Volume = 1.f);

	/** A looping move sound (Cannonbolt rolling) that follows the alien until StopMoveLoop. */
	void StartMoveLoop(FName Name, float Volume = 1.f);
	void StopMoveLoop(float FadeTime = 0.2f);

	/** Its own loop louder / higher (Stinkfly's wings while he flies). 1, 1 = normal. */
	void SetLoopBoost(float VolumeScale, float PitchScale);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** In its case (not held, not flying home) and your head is not in there with it. */
	bool IsBehindGlass() const;

	/** Its voice's pitch: the data's, a little higher when its case is scaled down. */
	float SizePitch() const;

	bool IsInWater() const;
	UMuseumAudio* GetAudio() const;

	/** Muffles or clears a looping sound when the alien leaves / re-enters its case (or you lean in). */
	void UpdateLoops();
	void SetLoopGlass(UAudioComponent* Loop, bool& bInOutBehindGlass, bool bBehindGlass) const;

	UPROPERTY(Transient)
	TObjectPtr<AAlienCharacter> Alien;

	/** The alien's data (kept alive by AAlienCharacter::AlienData). */
	const UAlienDataAsset* Data = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> LoopAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MoveLoopAudio;

	int32 LastVoice[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	int32 LastStep = INDEX_NONE;
	float CallTimer = 0.f;
	float LoopTimer = 0.f;
	float LoopVolume = 1.f;
	float LoopPitch = 1.f;
	float MoveLoopVolume = 1.f;
	bool bLoopBehindGlass = false;
	bool bMoveLoopBehindGlass = false;
	FRandomStream Rng;
};

// Alien Museum - plays the museum's sounds (one per game world).
//
// Sounds come from UMuseumSoundLibrary (/Game/AlienMuseum/Audio/DA_MuseumSounds, played by name) or an
// alien's own data (UAlienDataAsset::Sounds, through UAlienSoundComponent). All are 3D: on the Quest,
// Resonance Audio renders them binaurally (Config/DefaultEngine.ini), so an alien is heard where it is -
// behind you, above you, inside its case. A sound made inside a case is heard through the glass (muffled
// and quieter) unless your head is in the case too.
//
// It also keeps the museum from getting noisy: an alien may only call out on its own when it is one of
// the few nearest to you and nobody else called out a moment ago.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/MuseumSoundLibrary.h"
#include "MuseumAudio.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundBase;

UCLASS()
class BEN10_API UMuseumAudio : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The audio of WorldContext's world (nullptr outside the game, e.g. in the editor's level). */
	static UMuseumAudio* Get(const UObject* WorldContext);

	/** Where and how to play a sound. */
	struct FPlay
	{
		FVector Location = FVector::ZeroVector;
		/** It follows this component (an alien, a case); Location is then where it starts. */
		USceneComponent* AttachTo = nullptr;
		float Volume = 1.f;
		float Pitch = 1.f;
		EMuseumSoundRange Range = EMuseumSoundRange::Room;
		/** Heard through a case's glass: muffled and quieter. */
		bool bThroughGlass = false;
	};

	/** A library sound by name: a random variation at a random pitch in its range. Null when nothing plays. */
	UAudioComponent* Play(FName Name, const FPlay& How);
	UAudioComponent* PlayAt(FName Name, const FVector& Location, float Volume = 1.f);

	/** One given sound (an alien's voice). How.Range picks the attenuation. */
	UAudioComponent* PlaySound(USoundBase* Sound, const FPlay& How);

	/** A random sound from a list, never the one it picked last time (InOutLast remembers it). */
	USoundBase* Pick(const TArray<TObjectPtr<USoundBase>>& Sounds, int32& InOutLast);

	/** Muffles a playing sound as if heard through glass, or clears that. BaseVolume = its volume unmuffled. */
	void SetThroughGlass(UAudioComponent* Audio, bool bThroughGlass, float BaseVolume) const;

	/** Where the player's head is (the listener). */
	bool GetListener(FVector& OutLocation) const;

	// ---- Keeping the aliens from talking over each other ----

	void RegisterSpeaker(const AActor* Speaker);
	void UnregisterSpeaker(const AActor* Speaker);

	/** May Speaker call out on its own now? Within hearing, one of the nearest few, and a gap after the last call. */
	bool MayCall(const AActor* Speaker) const;

	/** Someone just made a sound with their voice (a call or an alert): the others wait a moment. */
	void NotifyCalled();

	const UMuseumSoundLibrary* GetLibrary() const { return Library; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMuseumSoundLibrary> Library;

	TArray<TWeakObjectPtr<const AActor>> Speakers;

	/** Last variation picked per library sound, and what is playing of those with a limit. */
	TMap<FName, int32> LastVariation;
	TMap<FName, TArray<TWeakObjectPtr<UAudioComponent>>> Playing;

	double LastCallTime = -1000.0;
	FRandomStream Rng;
};

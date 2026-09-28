// Alien Museum - the museum's shared sounds: moves, footsteps, the cases, the panel, the glass, the Omnitrix.
//
// Scripts/make_museum_sounds.py synthesises them (original sounds, nothing recorded or copied) and
// Scripts/import_museum_sounds.py imports them into /Game/AlienMuseum/Audio and fills DA_MuseumSounds.
// UMuseumAudio loads that asset by path and plays its sounds by name ("Move.Pounce.Land"). An alien's own
// voice and footsteps are on its data asset (UAlienDataAsset::Sounds).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MuseumSoundLibrary.generated.h"

class USoundBase;
class USoundAttenuation;

/** How far a sound carries (each range has its own attenuation: 3D position, distance fade). */
UENUM(BlueprintType)
enum class EMuseumSoundRange : uint8
{
	Room UMETA(ToolTip = "Aliens and their moves: heard across the room, fading over a few metres"),
	Near UMETA(ToolTip = "Quiet close-up sounds: footsteps, a case's hum and home-world ambience (about a metre)"),
	Interface UMETA(ToolTip = "The panel and the watch: close to you and always clear")
};

/** One named sound and its variations (a random one plays, never the same twice in a row). */
USTRUCT(BlueprintType)
struct BEN10_API FMuseumSound
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TArray<TObjectPtr<USoundBase>> Variations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = 0))
	float Volume = 1.f;

	/** Each play picks a pitch in this range (1 = as made), so repeats never sound identical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FVector2D Pitch = FVector2D(0.96f, 1.04f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	EMuseumSoundRange Range = EMuseumSoundRange::Room;

	/** At most this many playing at once (0 = no limit); a new one then stops the oldest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = 0))
	int32 MaxPlaying = 0;
};

UCLASS(BlueprintType)
class BEN10_API UMuseumSoundLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	TArray<FMuseumSound> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranges")
	TObjectPtr<USoundAttenuation> RoomAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranges")
	TObjectPtr<USoundAttenuation> NearAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranges")
	TObjectPtr<USoundAttenuation> InterfaceAttenuation;

	/** A sound from inside a case is heard through the glass: its highs are cut above this frequency... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass", meta = (Units = "Hz", ClampMin = 200))
	float GlassCutoff = 2600.f;

	/** ...and it is this much quieter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass", meta = (ClampMin = 0, ClampMax = 1))
	float GlassVolume = 0.7f;

	/** Seconds between any two aliens calling out on their own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voices", meta = (Units = "s", ClampMin = 0))
	float CallGap = 4.f;

	/** Only this many of the aliens nearest to you call out on their own; the others stay quiet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voices", meta = (ClampMin = 1))
	int32 CallingAliens = 2;

	/** Aliens further from you than this never call out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voices", meta = (Units = "cm"))
	float HearingDistance = 450.f;

	/** Footsteps are only played this close to you. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voices", meta = (Units = "cm"))
	float FootstepDistance = 250.f;

	const FMuseumSound* Find(FName Name) const;
	USoundAttenuation* GetAttenuation(EMuseumSoundRange Range) const;
};

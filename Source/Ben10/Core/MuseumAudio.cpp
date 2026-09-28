// Alien Museum - plays the museum's sounds.

#include "Core/MuseumAudio.h"
#include "Ben10.h"
#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	const TCHAR* const LibraryPath = TEXT("/Game/AlienMuseum/Audio/DA_MuseumSounds.DA_MuseumSounds");
}

UMuseumAudio* UMuseumAudio::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UMuseumAudio>() : nullptr;
}

bool UMuseumAudio::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMuseumAudio::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Rng.GenerateNewSeed();
	// /Game/AlienMuseum is always cooked (Config/DefaultGame.ini), so the path survives packaging.
	Library = LoadObject<UMuseumSoundLibrary>(nullptr, LibraryPath);
	if (!Library)
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("No museum sounds at %s (Scripts/import_museum_sounds.py makes them): the museum stays silent"), LibraryPath);
	}
}

UAudioComponent* UMuseumAudio::Play(FName Name, const FPlay& How)
{
	const FMuseumSound* Entry = Library ? Library->Find(Name) : nullptr;
	if (!Entry || Entry->Variations.Num() == 0)
	{
		return nullptr;
	}
	FPlay Final = How;
	Final.Volume *= Entry->Volume;
	Final.Pitch *= Rng.FRandRange(Entry->Pitch.X, FMath::Max(Entry->Pitch.X, Entry->Pitch.Y));
	Final.Range = Entry->Range;
	USoundBase* Sound = Pick(Entry->Variations, LastVariation.FindOrAdd(Name, INDEX_NONE));

	// A sound with a limit: the oldest one still playing makes way for the new one.
	TArray<TWeakObjectPtr<UAudioComponent>>* Active = nullptr;
	if (Entry->MaxPlaying > 0)
	{
		Active = &Playing.FindOrAdd(Name);
		Active->RemoveAll([](const TWeakObjectPtr<UAudioComponent>& Audio) { return !Audio.IsValid() || !Audio->IsPlaying(); });
		while (Active->Num() >= Entry->MaxPlaying)
		{
			if (UAudioComponent* Oldest = (*Active)[0].Get())
			{
				Oldest->FadeOut(0.06f, 0.f);
			}
			Active->RemoveAt(0);
		}
	}
	UAudioComponent* Audio = PlaySound(Sound, Final);
	if (Active && Audio)
	{
		Active->Add(Audio);
	}
	return Audio;
}

UAudioComponent* UMuseumAudio::PlayAt(FName Name, const FVector& Location, float Volume)
{
	FPlay How;
	How.Location = Location;
	How.Volume = Volume;
	return Play(Name, How);
}

UAudioComponent* UMuseumAudio::PlaySound(USoundBase* Sound, const FPlay& How)
{
	UWorld* World = GetWorld();
	if (!Sound || !World || !World->bAllowAudioPlayback)
	{
		return nullptr;
	}
	// Created stopped, so the glass filter is on before the first sample is heard.
	FAudioDevice::FCreateComponentParams Params(World, How.AttachTo ? How.AttachTo->GetOwner() : nullptr);
	Params.AttenuationSettings = Library ? Library->GetAttenuation(How.Range) : nullptr;
	Params.bPlay = false;
	Params.bStopWhenOwnerDestroyed = true;
	Params.SetLocation(How.Location);
	UAudioComponent* Audio = FAudioDevice::CreateComponent(Sound, Params);
	if (!Audio)
	{
		return nullptr; // out of hearing range (short sounds are skipped then), or no audio device
	}
	Audio->bAutoDestroy = true;
	if (How.AttachTo)
	{
		Audio->AttachToComponent(How.AttachTo, FAttachmentTransformRules::KeepWorldTransform);
	}
	Audio->SetPitchMultiplier(FMath::Clamp(How.Pitch, 0.25f, 4.f));
	SetThroughGlass(Audio, How.bThroughGlass, How.Volume);
	Audio->Play();
	return Audio;
}

USoundBase* UMuseumAudio::Pick(const TArray<TObjectPtr<USoundBase>>& Sounds, int32& InOutLast)
{
	const int32 Count = Sounds.Num();
	if (Count == 0)
	{
		return nullptr;
	}
	int32 Index = Rng.RandRange(0, Count - 1);
	if (Count > 1 && Index == InOutLast)
	{
		Index = (Index + 1 + Rng.RandRange(0, Count - 2)) % Count; // any other one
	}
	InOutLast = Index;
	return Sounds[Index];
}

void UMuseumAudio::SetThroughGlass(UAudioComponent* Audio, bool bThroughGlass, float BaseVolume) const
{
	if (!Audio)
	{
		return;
	}
	Audio->SetLowPassFilterEnabled(bThroughGlass);
	if (bThroughGlass)
	{
		Audio->SetLowPassFilterFrequency(Library ? Library->GlassCutoff : 2600.f);
	}
	Audio->SetVolumeMultiplier(BaseVolume * (bThroughGlass ? (Library ? Library->GlassVolume : 0.7f) : 1.f));
}

bool UMuseumAudio::GetListener(FVector& OutLocation) const
{
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		OutLocation = Camera->GetCameraLocation();
		return true;
	}
	return false;
}

void UMuseumAudio::RegisterSpeaker(const AActor* Speaker)
{
	Speakers.RemoveAll([](const TWeakObjectPtr<const AActor>& Other) { return !Other.IsValid(); });
	Speakers.AddUnique(Speaker);
}

void UMuseumAudio::UnregisterSpeaker(const AActor* Speaker)
{
	Speakers.Remove(Speaker);
}

bool UMuseumAudio::MayCall(const AActor* Speaker) const
{
	FVector Listener;
	const UWorld* World = GetWorld();
	if (!Library || !Speaker || !World || !GetListener(Listener) || World->GetTimeSeconds() - LastCallTime < Library->CallGap)
	{
		return false;
	}
	const double Distance = FVector::Dist(Speaker->GetActorLocation(), Listener);
	if (Distance > Library->HearingDistance)
	{
		return false;
	}
	int32 Closer = 0;
	for (const TWeakObjectPtr<const AActor>& Other : Speakers)
	{
		if (Other.IsValid() && Other.Get() != Speaker && FVector::Dist(Other->GetActorLocation(), Listener) < Distance
			&& ++Closer >= Library->CallingAliens)
		{
			return false;
		}
	}
	return true;
}

void UMuseumAudio::NotifyCalled()
{
	if (const UWorld* World = GetWorld())
	{
		LastCallTime = World->GetTimeSeconds();
	}
}

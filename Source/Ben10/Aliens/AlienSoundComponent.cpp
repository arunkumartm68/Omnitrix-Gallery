// Alien Museum - an alien's voice, footsteps and move sounds.

#include "Aliens/AlienSoundComponent.h"
#include "Aliens/AlienActionComponent.h"
#include "Aliens/AlienCharacter.h"
#include "Chamber/AlienChamber.h"
#include "Chamber/ChamberHabitatComponent.h"
#include "Core/MuseumAudio.h"
#include "Data/AlienDataAsset.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"

UAlienSoundComponent::UAlienSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // calls and the glass check need no more
	Rng.GenerateNewSeed();
}

UMuseumAudio* UAlienSoundComponent::GetAudio() const
{
	return UMuseumAudio::Get(this);
}

void UAlienSoundComponent::BeginPlay()
{
	Super::BeginPlay();
	Alien = Cast<AAlienCharacter>(GetOwner());
	if (UMuseumAudio* Audio = GetAudio())
	{
		Audio->RegisterSpeaker(GetOwner());
	}
}

void UAlienSoundComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMuseumAudio* Audio = GetAudio())
	{
		Audio->UnregisterSpeaker(GetOwner());
	}
	StopMoveLoop(0.f);
	if (LoopAudio)
	{
		LoopAudio->Stop();
		LoopAudio = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UAlienSoundComponent::Setup(const UAlienDataAsset* InData)
{
	Alien = Cast<AAlienCharacter>(GetOwner());
	Data = InData;
	if (LoopAudio)
	{
		LoopAudio->Stop();
		LoopAudio = nullptr;
	}
	UMuseumAudio* Audio = GetAudio();
	if (!Data || !Alien || !Audio)
	{
		return;
	}
	const FAlienSounds& Sounds = Data->Sounds;
	// The first call comes after a while, at a different moment for every alien.
	CallTimer = Rng.FRandRange(Sounds.CallInterval.X * 0.4f, FMath::Max(Sounds.CallInterval.X, Sounds.CallInterval.Y));
	LoopVolume = LoopPitch = 1.f;
	if (Sounds.Loop)
	{
		UMuseumAudio::FPlay How;
		How.AttachTo = Alien->GetRootComponent();
		How.Location = Alien->GetActorLocation();
		How.Range = EMuseumSoundRange::Near;
		How.Volume = Sounds.LoopVolume * Sounds.Volume;
		How.Pitch = Sounds.Pitch * SizePitch();
		bLoopBehindGlass = IsBehindGlass();
		How.bThroughGlass = bLoopBehindGlass;
		LoopAudio = Audio->PlaySound(Sounds.Loop, How);
	}
}

bool UAlienSoundComponent::PlayVoice(EAlienVoice Voice, float Volume)
{
	UMuseumAudio* Audio = GetAudio();
	if (!Data || !Alien || !Audio)
	{
		return false;
	}
	const FAlienSounds& Sounds = Data->Sounds;
	const TArray<TObjectPtr<USoundBase>>& List = Voice == EAlienVoice::Alert ? Sounds.Alerts
		: Voice == EAlienVoice::Effort ? Sounds.Efforts
		: Voice == EAlienVoice::Held ? Sounds.Held
		: Sounds.Calls;
	USoundBase* Sound = Audio->Pick(List, LastVoice[static_cast<int32>(Voice)]);
	if (!Sound)
	{
		return false;
	}
	// From its head rather than its middle.
	const float HalfHeight = Alien->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	UMuseumAudio::FPlay How;
	How.AttachTo = Alien->GetRootComponent();
	How.Location = Alien->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * 0.6f);
	How.Volume = Volume * Sounds.Volume;
	How.Pitch = Sounds.Pitch * SizePitch() * Rng.FRandRange(0.96f, 1.04f);
	How.bThroughGlass = IsBehindGlass();
	if (!Audio->PlaySound(Sound, How))
	{
		return false;
	}
	if (Voice == EAlienVoice::Call || Voice == EAlienVoice::Alert)
	{
		Audio->NotifyCalled();
	}
	return true;
}

UAudioComponent* UAlienSoundComponent::PlayEffect(FName Name, float Volume, float Pitch)
{
	UMuseumAudio* Audio = GetAudio();
	if (!Audio || !Alien)
	{
		return nullptr;
	}
	UMuseumAudio::FPlay How;
	How.AttachTo = Alien->GetRootComponent();
	How.Location = Alien->GetActorLocation();
	How.Volume = Volume;
	How.Pitch = Pitch * SizePitch();
	How.bThroughGlass = IsBehindGlass();
	return Audio->Play(Name, How);
}

UAudioComponent* UAlienSoundComponent::PlayEffectAt(FName Name, const FVector& Location, float Volume)
{
	UMuseumAudio* Audio = GetAudio();
	if (!Audio || !Alien)
	{
		return nullptr;
	}
	UMuseumAudio::FPlay How;
	How.Location = Location;
	How.Volume = Volume;
	How.Pitch = SizePitch();
	// It happens in the case even while the alien is out (a ball still bouncing).
	const AAlienChamber* Case = Alien->GetHomeChamber();
	FVector Listener;
	How.bThroughGlass = Case && !(Audio->GetListener(Listener) && Case->DistanceToChamber(Listener) <= 0.f);
	return Audio->Play(Name, How);
}

void UAlienSoundComponent::PlayFootstep(float Volume)
{
	UMuseumAudio* Audio = GetAudio();
	const UMuseumSoundLibrary* Library = Audio ? Audio->GetLibrary() : nullptr;
	FVector Listener;
	if (!Data || !Alien || !Library || !Audio->GetListener(Listener)
		|| FVector::Dist(Listener, Alien->GetActorLocation()) > Library->FootstepDistance)
	{
		return; // too far to hear a step
	}
	const FVector Feet = Alien->GetActorLocation() - FVector(0.f, 0.f, Alien->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float StepVolume = Volume * Data->Sounds.FootstepVolume;
	if (IsInWater())
	{
		PlayEffectAt(TEXT("Step.Water"), Feet, StepVolume);
		return;
	}
	USoundBase* Sound = Audio->Pick(Data->Sounds.Footsteps, LastStep);
	if (!Sound)
	{
		return;
	}
	UMuseumAudio::FPlay How;
	How.Location = Feet; // a step stays where it was made
	How.Range = EMuseumSoundRange::Near;
	How.Volume = StepVolume;
	How.Pitch = SizePitch() * Rng.FRandRange(0.92f, 1.08f);
	How.bThroughGlass = IsBehindGlass();
	Audio->PlaySound(Sound, How);
}

void UAlienSoundComponent::StartMoveLoop(FName Name, float Volume)
{
	StopMoveLoop(0.1f);
	MoveLoopVolume = Volume;
	bMoveLoopBehindGlass = IsBehindGlass();
	MoveLoopAudio = PlayEffect(Name, Volume);
}

void UAlienSoundComponent::StopMoveLoop(float FadeTime)
{
	if (MoveLoopAudio)
	{
		if (FadeTime > 0.f)
		{
			MoveLoopAudio->FadeOut(FadeTime, 0.f);
		}
		else
		{
			MoveLoopAudio->Stop();
		}
		MoveLoopAudio = nullptr;
	}
}

void UAlienSoundComponent::SetLoopBoost(float VolumeScale, float PitchScale)
{
	UMuseumAudio* Audio = GetAudio();
	if (!LoopAudio || !Data || !Audio)
	{
		return;
	}
	LoopVolume = VolumeScale;
	LoopPitch = PitchScale;
	Audio->SetThroughGlass(LoopAudio, bLoopBehindGlass, Data->Sounds.LoopVolume * Data->Sounds.Volume * LoopVolume);
	LoopAudio->SetPitchMultiplier(Data->Sounds.Pitch * SizePitch() * LoopPitch);
}

void UAlienSoundComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Alien || !Data)
	{
		return;
	}
	UpdateLoops();

	// Now and then it calls out on its own - when it is free to, and when the museum lets it.
	CallTimer -= DeltaTime;
	if (CallTimer > 0.f || Data->Sounds.Calls.Num() == 0)
	{
		return;
	}
	UMuseumAudio* Audio = GetAudio();
	const UAlienActionComponent* Moves = Alien->GetActions();
	const bool bFree = !Alien->IsHeld() && !(Moves && Moves->IsPerforming());
	if (bFree && Audio && Audio->MayCall(Alien) && PlayVoice(EAlienVoice::Call))
	{
		const FVector2D& Interval = Data->Sounds.CallInterval;
		CallTimer = Rng.FRandRange(Interval.X, FMath::Max(Interval.X, Interval.Y));
	}
	else
	{
		CallTimer = Rng.FRandRange(2.f, 5.f); // not now: ask again in a moment
	}
}

void UAlienSoundComponent::UpdateLoops()
{
	if (!LoopAudio && !MoveLoopAudio)
	{
		return;
	}
	const bool bBehindGlass = IsBehindGlass();
	SetLoopGlass(LoopAudio, bLoopBehindGlass, bBehindGlass);
	SetLoopGlass(MoveLoopAudio, bMoveLoopBehindGlass, bBehindGlass);
}

void UAlienSoundComponent::SetLoopGlass(UAudioComponent* Loop, bool& bInOutBehindGlass, bool bBehindGlass) const
{
	UMuseumAudio* Audio = GetAudio();
	const UMuseumSoundLibrary* Library = Audio ? Audio->GetLibrary() : nullptr;
	if (!Loop || !Library || bInOutBehindGlass == bBehindGlass)
	{
		return;
	}
	// Its volume without the glass, from what it plays at now.
	const float Base = Loop->VolumeMultiplier / (bInOutBehindGlass ? FMath::Max(0.01f, Library->GlassVolume) : 1.f);
	Audio->SetThroughGlass(Loop, bBehindGlass, Base);
	bInOutBehindGlass = bBehindGlass;
}

bool UAlienSoundComponent::IsBehindGlass() const
{
	const AAlienChamber* Case = Alien ? Alien->GetHomeChamber() : nullptr;
	if (!Case || Alien->IsOutOfCase())
	{
		return false;
	}
	// Leaning into the case, you hear it as it is.
	FVector Listener;
	const UMuseumAudio* Audio = GetAudio();
	return !(Audio && Audio->GetListener(Listener) && Case->DistanceToChamber(Listener) <= 0.f);
}

float UAlienSoundComponent::SizePitch() const
{
	// A smaller case (and alien) sounds a little higher, a bigger one lower.
	return Alien ? FMath::Clamp(1.f / FMath::Sqrt(Alien->GetScaleFactor()), 0.8f, 1.3f) : 1.f;
}

bool UAlienSoundComponent::IsInWater() const
{
	AAlienChamber* Case = Alien ? Alien->GetHomeChamber() : nullptr;
	const UChamberHabitatComponent* Home = Case ? Case->GetHabitat() : nullptr;
	return Home && Home->IsWater();
}

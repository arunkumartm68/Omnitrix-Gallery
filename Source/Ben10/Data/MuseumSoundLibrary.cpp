// Alien Museum - the museum's shared sounds.

#include "Data/MuseumSoundLibrary.h"
#include "Sound/SoundAttenuation.h"

const FMuseumSound* UMuseumSoundLibrary::Find(FName Name) const
{
	return Sounds.FindByPredicate([Name](const FMuseumSound& Sound) { return Sound.Name == Name; });
}

USoundAttenuation* UMuseumSoundLibrary::GetAttenuation(EMuseumSoundRange Range) const
{
	switch (Range)
	{
	case EMuseumSoundRange::Near: return NearAttenuation;
	case EMuseumSoundRange::Interface: return InterfaceAttenuation;
	default: return RoomAttenuation;
	}
}

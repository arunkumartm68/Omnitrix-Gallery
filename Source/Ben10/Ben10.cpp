// Alien Museum - main game module.

#include "Ben10.h"
#include "MR/MuseumOpenXRLayer.h"
#include "Modules/ModuleManager.h"

/** Registers the OpenXR layer that keeps frame synthesis (and its crash-prone swapchains) off. */
class FBen10Module : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		OpenXRLayer.RegisterOpenXRExtensionModularFeature();
	}

	virtual void ShutdownModule() override
	{
		OpenXRLayer.UnregisterOpenXRExtensionModularFeature();
	}

private:
	FMuseumOpenXRLayer OpenXRLayer;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FBen10Module, Ben10, "Ben10");

DEFINE_LOG_CATEGORY(LogAlienMuseum);

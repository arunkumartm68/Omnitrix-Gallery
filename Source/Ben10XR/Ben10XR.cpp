// Alien Museum - early XR module: registers the OpenXR layer before the engine creates its instance.

#include "MuseumOpenXRLayer.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMuseumXR);

class FBen10XRModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		OpenXRLayer.RegisterOpenXRExtensionModularFeature();
		UE_LOG(LogMuseumXR, Log, TEXT("Frame-synthesis filter registered"));
	}

	virtual void ShutdownModule() override
	{
		OpenXRLayer.UnregisterOpenXRExtensionModularFeature();
	}

private:
	FMuseumOpenXRLayer OpenXRLayer;
};

IMPLEMENT_MODULE(FBen10XRModule, Ben10XR);

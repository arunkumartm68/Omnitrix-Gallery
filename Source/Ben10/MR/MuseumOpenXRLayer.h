// Alien Museum - hides the Quest's frame-synthesis extensions from Unreal's OpenXR plugin.
//
// UE 5.7 enables XR_FB_space_warp or XR_EXT_frame_synthesis whenever the runtime offers one - even
// with frame synthesis switched off (xr.OpenXRFrameSynthesis 0) - and then creates two motion-vector
// swapchains every frame. When a frame is heavy (the skinned Wildmutt and Ghostfreak on the collection's
// second page) their image indices drift apart and OpenXRHMD's check(MotionVectorIndex ==
// MotionVectorDepthIndex) kills the app. The museum does not use frame synthesis, so this API layer
// (IOpenXRExtensionPlugin::InsertOpenXRAPILayer) leaves both extensions out of the runtime's extension
// list: Unreal never enables them and never creates those swapchains.

#pragma once

#include "CoreMinimal.h"
#include "IOpenXRExtensionPlugin.h"

class FMuseumOpenXRLayer : public IOpenXRExtensionPlugin
{
public:
	virtual FString GetDisplayName() override { return TEXT("AlienMuseum frame-synthesis filter"); }
	virtual bool InsertOpenXRAPILayer(PFN_xrGetInstanceProcAddr& InOutGetProcAddr) override;
};

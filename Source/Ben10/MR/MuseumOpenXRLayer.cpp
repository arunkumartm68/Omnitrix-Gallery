// Alien Museum - hides the Quest's frame-synthesis extensions from Unreal's OpenXR plugin.

#include "MR/MuseumOpenXRLayer.h"
#include "Ben10.h"

namespace
{
	PFN_xrGetInstanceProcAddr NextGetInstanceProcAddr = nullptr;
	PFN_xrEnumerateInstanceExtensionProperties NextEnumerateExtensions = nullptr;

	bool IsHidden(const char* Name)
	{
		return FCStringAnsi::Strcmp(Name, "XR_FB_space_warp") == 0 || FCStringAnsi::Strcmp(Name, "XR_EXT_frame_synthesis") == 0;
	}

	XRAPI_ATTR XrResult XRAPI_CALL EnumerateExtensionsFiltered(const char* LayerName, uint32_t CapacityInput, uint32_t* CountOutput, XrExtensionProperties* Properties)
	{
		if (!NextEnumerateExtensions || !CountOutput)
		{
			return XR_ERROR_VALIDATION_FAILURE;
		}
		uint32_t Count = 0;
		XrResult Result = NextEnumerateExtensions(LayerName, 0, &Count, nullptr);
		if (XR_FAILED(Result))
		{
			return Result;
		}
		TArray<XrExtensionProperties> All;
		All.SetNum(Count);
		for (XrExtensionProperties& Property : All)
		{
			Property = XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES };
		}
		Result = NextEnumerateExtensions(LayerName, Count, &Count, All.GetData());
		if (XR_FAILED(Result))
		{
			return Result;
		}
		All.SetNum(Count);
		const int32 Before = All.Num();
		All.RemoveAll([](const XrExtensionProperties& Property) { return IsHidden(Property.extensionName); });
		if (All.Num() != Before && CapacityInput == 0)
		{
			UE_LOG(LogAlienMuseum, Log, TEXT("OpenXR: frame-synthesis extensions hidden from the engine (not used; avoids motion-vector swapchains)"));
		}

		*CountOutput = static_cast<uint32_t>(All.Num());
		if (CapacityInput == 0)
		{
			return XR_SUCCESS;
		}
		if (!Properties || CapacityInput < static_cast<uint32_t>(All.Num()))
		{
			return XR_ERROR_SIZE_INSUFFICIENT;
		}
		for (int32 i = 0; i < All.Num(); ++i)
		{
			// Keep the caller's type / next, fill in the rest.
			FCStringAnsi::Strncpy(Properties[i].extensionName, All[i].extensionName, XR_MAX_EXTENSION_NAME_SIZE);
			Properties[i].extensionVersion = All[i].extensionVersion;
		}
		return XR_SUCCESS;
	}

	XRAPI_ATTR XrResult XRAPI_CALL GetInstanceProcAddrFiltered(XrInstance Instance, const char* Name, PFN_xrVoidFunction* Function)
	{
		const XrResult Result = NextGetInstanceProcAddr(Instance, Name, Function);
		if (XR_SUCCEEDED(Result) && Function && *Function && FCStringAnsi::Strcmp(Name, "xrEnumerateInstanceExtensionProperties") == 0)
		{
			NextEnumerateExtensions = reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(*Function);
			*Function = reinterpret_cast<PFN_xrVoidFunction>(&EnumerateExtensionsFiltered);
		}
		return Result;
	}
}

bool FMuseumOpenXRLayer::InsertOpenXRAPILayer(PFN_xrGetInstanceProcAddr& InOutGetProcAddr)
{
	if (!InOutGetProcAddr)
	{
		return false;
	}
	NextGetInstanceProcAddr = InOutGetProcAddr;
	InOutGetProcAddr = &GetInstanceProcAddrFiltered;
	return true;
}

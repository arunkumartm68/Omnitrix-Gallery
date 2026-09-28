// Alien Museum - early XR module (loading phase PostConfigInit).
//
// Holds the OpenXR API layer that keeps the Quest's frame-synthesis extensions away from the engine
// (see MuseumOpenXRLayer.h). The engine creates its OpenXR instance before the renderer starts - long
// before the game module "Ben10" loads - so the layer must be registered from a module that loads as
// early as the OpenXR plugin itself.

using UnrealBuildTool;

public class Ben10XR : ModuleRules
{
	public Ben10XR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"HeadMountedDisplay",
			"XRBase",
			"AugmentedReality", // included by IOpenXRExtensionPlugin.h
			"OpenXRHMD",        // IOpenXRExtensionPlugin
		});
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
	}
}

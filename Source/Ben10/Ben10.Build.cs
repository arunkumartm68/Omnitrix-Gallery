// Alien Museum - main game module.

using UnrealBuildTool;

public class Ben10 : ModuleRules
{
	public Ben10(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Lets code include headers by folder, e.g. "Aliens/AlienCharacter.h".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PhysicsCore", // UPhysicalMaterial (bouncy energy balls)
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"HeadMountedDisplay",
			"XRBase",
			"ProceduralMeshComponent",

			// Meta XR plugin (Epic Native OpenXR backend)
			"OculusXRHMD",
			"OculusXRPassthrough",
			"OculusXRAnchors",
			"OculusXRAsyncRequest",
			"MRUtilityKit",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AndroidPermission",
			"OpenXRHMD",        // MR/MuseumOpenXRLayer: IOpenXRExtensionPlugin
			"AugmentedReality", // included by IOpenXRExtensionPlugin.h
		});
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
	}
}

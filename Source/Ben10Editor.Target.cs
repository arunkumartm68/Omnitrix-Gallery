// Alien Museum - editor target.

using UnrealBuildTool;
using System.Collections.Generic;

public class Ben10EditorTarget : TargetRules
{
	public Ben10EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("Ben10");
		ExtraModuleNames.Add("Ben10XR");
	}
}

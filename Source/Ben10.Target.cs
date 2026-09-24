// Alien Museum - game target (Quest / Android and Windows).

using UnrealBuildTool;
using System.Collections.Generic;

public class Ben10Target : TargetRules
{
	public Ben10Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("Ben10");
	}
}

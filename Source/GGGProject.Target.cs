using UnrealBuildTool;
using System.Collections.Generic;

public class GGGProjectTarget : TargetRules
{
	public GGGProjectTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		bOverrideBuildEnvironment = true;
		GlobalDefinitions.Add("__has_feature(x)=0");
		ExtraModuleNames.Add("GGGProject");
	}
}

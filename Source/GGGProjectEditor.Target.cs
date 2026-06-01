using UnrealBuildTool;
using System.Collections.Generic;

public class GGGProjectEditorTarget : TargetRules
{
	public GGGProjectEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		bOverrideBuildEnvironment = true;
		GlobalDefinitions.Add("__has_feature(x)=0");
		ExtraModuleNames.Add("GGGProject");
	}
}

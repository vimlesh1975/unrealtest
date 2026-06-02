using UnrealBuildTool;

public class GGGProject : ModuleRules
{
	public GGGProject(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"MediaIOCore",
			"BlackmagicMedia",
			"BlackmagicMediaOutput",
			"TimeManagement",
			"MediaAssets",
			"AudioMixer",
			"HTTPServer",
			"Json"
		});
	}
}

using UnrealBuildTool;

public class ElasticUMG : ModuleRules
{
	public ElasticUMG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings",
				"UMG"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"PropertyPath",
				"Slate",
				"SlateCore"
			});
	}
}

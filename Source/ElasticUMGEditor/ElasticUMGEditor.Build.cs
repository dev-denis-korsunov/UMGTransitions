using UnrealBuildTool;

public class ElasticUMGEditor : ModuleRules
{
	public ElasticUMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ElasticUMG",
				"UMG",
				"UMGEditor",
				"InputCore",
				"Slate",
				"SlateCore",
				"GraphEditor",
				"UnrealEd",
				"BlueprintGraph",
				"PropertyEditor",
				"Settings"
			});
	}
}

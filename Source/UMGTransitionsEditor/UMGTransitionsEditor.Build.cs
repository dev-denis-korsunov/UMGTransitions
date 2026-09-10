using UnrealBuildTool;

public class UMGTransitionsEditor : ModuleRules
{
	public UMGTransitionsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMGTransitions",
				"UMG",
				"UMGEditor",
				"InputCore",
				"Slate",
				"SlateCore",
				"AppFramework",
				"GraphEditor",
				"PropertyEditor",
				"UnrealEd",
				"BlueprintGraph"
			});
	}
}

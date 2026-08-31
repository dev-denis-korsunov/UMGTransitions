using UnrealBuildTool;

public class UMGTransitionsTests : ModuleRules
{
	public UMGTransitionsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"FieldNotification",
				"PropertyPath",
				"UMG",
				"UMGTransitions"
			});
	}
}

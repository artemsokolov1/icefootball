using UnrealBuildTool;

public class IceFootball : ModuleRules
{
	public IceFootball(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Sources live in sub-folders (Skate/, Skate/Core/, Skate/Tests/) and are included as "Skate/...".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",   // UPhysicalMaterial
			"AudioMixer",    // USynthComponent (procedural ice sounds, no audio assets needed)
		});
	}
}

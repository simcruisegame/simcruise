// Copyright SimCruise contributors. All Rights Reserved.

using UnrealBuildTool;

public class SimCruise : ModuleRules
{
	public SimCruise(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Json",
			"JsonUtilities",
			"PhysicsCore"
		});

		// Environment: frame timing (RenderCore, RHI) for the graphics presets and perf sampler.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI"
		});
	}
}

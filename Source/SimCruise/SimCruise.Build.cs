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

		// Real-world streaming (Atlas/, World/): runtime-built meshes, decoded
		// aerial photographs and generated textures.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ProceduralMeshComponent",
			"ImageWrapper",
			"RenderCore"
		});
	}
}

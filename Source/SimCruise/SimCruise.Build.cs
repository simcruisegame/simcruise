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

		// Car presentation (Presentation/): runtime glTF meshes, textures and materials.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageCore",
			"ImageWrapper",
			"MeshDescription",
			"StaticMeshDescription",
			"RenderCore",
			"RHI",
			"CinematicCamera"
		});
	}
}

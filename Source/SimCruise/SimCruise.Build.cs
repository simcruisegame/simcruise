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
			"PhysicsCore",
			"Chaos"
		});

		// Real-world streaming (Atlas/, World/): runtime-built meshes, decoded
		// aerial photographs and generated textures.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ProceduralMeshComponent",
			"ImageWrapper",
			"RenderCore"
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

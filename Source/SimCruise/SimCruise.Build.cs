// Copyright SimCruise contributors. All Rights Reserved.

using UnrealBuildTool;

public class SimCruise : ModuleRules
{
	public SimCruise(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Each stream keeps its helpers in file-local anonymous namespaces (Tau,
		// Smooth01, test flags ...). Unity builds paste files together, where
		// those collide, so every file compiles on its own.
		bUseUnity = false;

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

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Real-world streaming (Atlas/, World/): runtime-built meshes, decoded
			// aerial photographs and generated textures.
			"ProceduralMeshComponent",
			// Car presentation (Presentation/): runtime glTF meshes, textures and materials.
			"ImageCore",
			"ImageWrapper",
			"MeshDescription",
			"StaticMeshDescription",
			"CinematicCamera",
			// Rendering resources, plus frame timing for the environment's graphics presets and perf sampler.
			"RenderCore",
			"RHI"
		});
	}
}

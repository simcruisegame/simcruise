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
			// Engine sound (Audio/): a procedural synth component.
			"AudioMixer",
			// Real-world streaming (Atlas/, World/): runtime-built meshes, decoded
			// aerial photographs and generated textures.
			"ProceduralMeshComponent",
			// Car presentation (Presentation/): runtime glTF meshes, textures and materials.
			"ImageCore",
			"ImageWrapper",
			"MeshDescription",
			"StaticMeshDescription",
			"CinematicCamera",
			// The place search and the road atlas (Gameplay/SimCruiseAtlasUI): Slate screens.
			"Slate",
			"SlateCore",
			// Rendering resources, plus frame timing for the environment's graphics presets and perf sampler.
			"RenderCore",
			"RHI",
			// Live world (Live/): public real-time feeds over HTTP, and the local AI detail
			// engine on Unreal's Neural Network Engine (the NNERuntimeORT plugin runs it).
			"HTTP",
			"NNE"
		});

		// USB racing wheels (Gameplay/SimCruiseWheelDevice): Windows GameInput, read directly
		// for the wheel, pedals and force feedback.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "GameInputBase", "GameInputWindowsLibrary" });
			PrivateDefinitions.Add("SIMCRUISE_WITH_GAMEINPUT=1");
		}
		else
		{
			PrivateDefinitions.Add("SIMCRUISE_WITH_GAMEINPUT=0");
		}
	}
}

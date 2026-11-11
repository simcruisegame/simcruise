// Copyright SimCruise contributors. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class SimCruiseTarget : TargetRules
{
	public SimCruiseTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SimCruise");
	}
}

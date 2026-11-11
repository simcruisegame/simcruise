// Copyright SimCruise contributors. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class SimCruiseEditorTarget : TargetRules
{
	public SimCruiseEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SimCruise");
	}
}

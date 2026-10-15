// Copyright (c) 2026 Y.Nakaue. MIT License.

using UnrealBuildTool;

public class AudioPluginHostRuntime : ModuleRules
{
	public AudioPluginHostRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("bcrypt.lib");
		}
	}
}

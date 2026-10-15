// Copyright (c) 2026 Y.Nakaue. MIT License.

using System.IO;
using UnrealBuildTool;

public class AudioPluginHostVST3 : ModuleRules
{
	public AudioPluginHostVST3(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		bUseRTTI = true;
		bEnableExceptions = true;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "AudioPluginHostRuntime" });

		string SdkRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "vst3sdk"));
		if (File.Exists(Path.Combine(SdkRoot, "public.sdk", "source", "vst", "hosting", "module.cpp"))
			&& File.Exists(Path.Combine(SdkRoot, "pluginterfaces", "vst", "ivstaudioprocessor.h")))
		{
			PrivateIncludePaths.Add(SdkRoot);
			PrivateDefinitions.Add("AUDIOPLUGINHOST_WITH_VST3SDK=1");
			PrivateDefinitions.Add("SMTG_USE_STDATOMIC_H=0");
		}
		else
		{
			PrivateDefinitions.Add("AUDIOPLUGINHOST_WITH_VST3SDK=0");
		}
	}
}

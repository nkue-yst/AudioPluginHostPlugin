// Copyright (c) 2026 Y.Nakaue. MIT License.

using UnrealBuildTool;

public class AudioPluginHostEditor : ModuleRules
{
	public AudioPluginHostEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AudioPluginHostRuntime", "AudioPluginHostVST3", "ApplicationCore",
			"AssetDefinition", "AssetRegistry", "AssetTools", "AudioEditor", "AudioMixer", "ContentBrowser", "CoreUObject",
			"DesktopPlatform", "DeveloperToolSettings", "Engine", "InputCore",
			"PropertyEditor", "Slate", "SlateCore", "ToolMenus", "UnrealEd"
		});
	}
}

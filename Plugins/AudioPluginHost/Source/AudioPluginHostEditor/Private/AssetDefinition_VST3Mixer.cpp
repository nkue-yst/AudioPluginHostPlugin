// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "AssetDefinition_VST3Mixer.h"

#include "VST3MixerAsset.h"
#include "VST3MixerAssetEditorToolkit.h"

FText UAssetDefinition_VST3Mixer::GetAssetDisplayName() const
{
	return FText::FromString(TEXT("VST Mixer"));
}

FLinearColor UAssetDefinition_VST3Mixer::GetAssetColor() const
{
	return FLinearColor(0.17f, 0.61f, 0.77f);
}

TSoftClassPtr<UObject> UAssetDefinition_VST3Mixer::GetAssetClass() const
{
	return UVST3MixerAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_VST3Mixer::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Audio };
	return Categories;
}

EAssetCommandResult UAssetDefinition_VST3Mixer::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (auto* Mixer : OpenArgs.LoadObjects<UVST3MixerAsset>())
	{
		auto Toolkit = MakeShared<FVST3MixerAssetEditorToolkit>();
		Toolkit->Initialize(Mixer, OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost);
	}
	return EAssetCommandResult::Handled;
}

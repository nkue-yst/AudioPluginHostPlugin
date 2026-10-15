// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "Toolkits/AssetEditorToolkit.h"

class SVST3Mixer;
class UVST3MixerAsset;
class SDockTab;
class FSpawnTabArgs;

/** Mixer assetごとに画面と保存処理を持つ。 */
class FVST3MixerAssetEditorToolkit final : public FAssetEditorToolkit
{
public:
	void Initialize(UVST3MixerAsset* InMixer, EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost);
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void SaveAsset_Execute() override;
	virtual void GetSaveableObjects(TArray<UObject*>& OutObjects) const override;
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;
	virtual bool CanSaveAssetAs() const override { return false; }
	virtual bool IsSaveAssetAsVisible() const override { return false; }

private:
	TSharedRef<SDockTab> SpawnMixerTab(const FSpawnTabArgs& Args);

	TWeakObjectPtr<UVST3MixerAsset> MixerAsset;
	TSharedPtr<SVST3Mixer> MixerWidget;
};

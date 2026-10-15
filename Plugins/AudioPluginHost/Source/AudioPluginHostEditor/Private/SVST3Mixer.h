// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "Widgets/SCompoundWidget.h"

class FVST3MixerModel;
class SEditableTextBox;
class SHorizontalBox;
class SVerticalBox;
class USoundSubmix;
class UVST3MixerAsset;
class UVST3ModuleAsset;
class UVST3SubmixEffectPreset;
struct FAssetData;
struct FPropertyChangedEvent;

/** UEのSubmixを横並びのチャンネルと挿入スロットで編集する。 */
class SVST3Mixer final : public SCompoundWidget, public FEditorUndoClient
{
public:
	SVST3Mixer();
	SLATE_BEGIN_ARGS(SVST3Mixer) {}
		SLATE_ARGUMENT(UVST3MixerAsset*, MixerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVST3Mixer() override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	bool SaveMixer(FText& OutError);
	bool PrepareToSave(FText& OutError);
	void GetSaveableObjects(TArray<UObject*>& OutObjects) const;

private:
	void Refresh();
	void RebuildChannels();
	void RebuildInspector();
	void SelectBus(TWeakObjectPtr<USoundSubmix> Bus);
	void Report(bool bSuccess, const FText& Error, const FText& Success = FText::GetEmpty());
	void OpenEditor(TWeakObjectPtr<UVST3SubmixEffectPreset> Preset);
	TSharedRef<SWidget> MakeChannel(TWeakObjectPtr<USoundSubmix> Bus);
	TSharedRef<SWidget> MakeInsert(TWeakObjectPtr<USoundSubmix> Bus, int32 Index);
	TSharedRef<SWidget> MakeOutputMenu(TWeakObjectPtr<USoundSubmix> Bus);
	TSharedRef<SWidget> MakeComponentMenu();
	FText GetOutputLabel(TWeakObjectPtr<USoundSubmix> Bus) const;
	FText GetRouteLabel(TWeakObjectPtr<USoundSubmix> Bus) const;
	bool CanEditStructure() const;
	bool CanEditBus(TWeakObjectPtr<USoundSubmix> Bus) const;
	void HandleObjectChanged(UObject* Object, FPropertyChangedEvent& Event);
	void HandleAssetChanged(const FAssetData& Asset);
	void HandleAssetRenamed(const FAssetData& Asset, const FString& OldPath);
	void RequestRefresh();
	EActiveTimerReturnType RefreshDeferred(double CurrentTime, float DeltaTime);

	TSharedPtr<FVST3MixerModel> Model;
	TSharedPtr<SVerticalBox> Navigation;
	TSharedPtr<SHorizontalBox> Channels;
	TSharedPtr<SVerticalBox> Inspector;
	TSharedPtr<SEditableTextBox> BusName;
	TWeakObjectPtr<USoundSubmix> SelectedBus;
	TWeakObjectPtr<USoundSubmix> VolumeBus;
	TWeakObjectPtr<UVST3SubmixEffectPreset> SelectedPreset;
	TStrongObjectPtr<UVST3ModuleAsset> SelectedModule;
	FString SelectedClassId;
	FText Status;
	bool bStatusIsError = false;
	bool bRefreshRequested = false;
	FDelegateHandle ObjectChangedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
};

// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3MixerAssetEditorToolkit.h"

#include "Misc/MessageDialog.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "SVST3Mixer.h"
#include "VST3MixerAsset.h"
#include "Widgets/Docking/SDockTab.h"

namespace
{
	const FName MixerTabId(TEXT("VST3MixerAssetEditor_Mixer"));
}

void FVST3MixerAssetEditorToolkit::Initialize(UVST3MixerAsset* InMixer, const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InToolkitHost)
{
	this->MixerAsset = InMixer;
	const auto Layout = FTabManager::NewLayout(TEXT("VST3MixerAssetEditor_Layout_v1"))
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split(FTabManager::NewStack()->AddTab(MixerTabId, ETabState::OpenedTab)));
	this->InitAssetEditor(Mode, InToolkitHost, TEXT("VST3MixerAssetEditor"), Layout,
		true, true, InMixer);
}

void FVST3MixerAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	this->WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		FText::FromString(TEXT("VST Mixer")));
	InTabManager->RegisterTabSpawner(MixerTabId, FOnSpawnTab::CreateRaw(this,
		&FVST3MixerAssetEditorToolkit::SpawnMixerTab))
		.SetDisplayName(FText::FromString(TEXT("Mixer")))
		.SetGroup(this->WorkspaceMenuCategory.ToSharedRef());
}

void FVST3MixerAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(MixerTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FVST3MixerAssetEditorToolkit::SpawnMixerTab(const FSpawnTabArgs& Args)
{
	this->MixerWidget = SNew(SVST3Mixer).MixerAsset(this->MixerAsset.Get());
	return SNew(SDockTab).TabRole(ETabRole::PanelTab)[this->MixerWidget.ToSharedRef()];
}

FName FVST3MixerAssetEditorToolkit::GetToolkitFName() const
{
	return TEXT("VST3MixerAssetEditor");
}

FText FVST3MixerAssetEditorToolkit::GetBaseToolkitName() const
{
	return FText::FromString(TEXT("VST Mixer"));
}

FString FVST3MixerAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("VST Mixer");
}

FLinearColor FVST3MixerAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.17f, 0.61f, 0.77f);
}

void FVST3MixerAssetEditorToolkit::SaveAsset_Execute()
{
	if (this->MixerWidget.IsValid() == false)
	{
		return;
	}
	FText Error;
	if (this->MixerWidget->SaveMixer(Error) == false)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
	}
}

void FVST3MixerAssetEditorToolkit::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	FAssetEditorToolkit::GetSaveableObjects(OutObjects);
	if (this->MixerWidget.IsValid() == true)
	{
		this->MixerWidget->GetSaveableObjects(OutObjects);
	}
}

bool FVST3MixerAssetEditorToolkit::OnRequestClose(const EAssetEditorCloseReason InCloseReason)
{
	if (this->MixerWidget.IsValid() == false || this->MixerAsset.IsValid() == false
		|| InCloseReason == EAssetEditorCloseReason::AssetUnloadingOrInvalid
		|| InCloseReason == EAssetEditorCloseReason::AssetForceDeleted)
	{
		return true;
	}
	FText Error;
	if (this->MixerWidget->PrepareToSave(Error) == false)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return false;
	}
	TArray<UObject*> Objects;
	this->GetSaveableObjects(Objects);
	const auto bHasUnsavedChanges = Objects.ContainsByPredicate([](const auto* Object)
	{
		return Object != nullptr && Object->GetOutermost()->IsDirty();
	});
	if (bHasUnsavedChanges == false)
	{
		return true;
	}

	const auto Choice = FMessageDialog::Open(EAppMsgType::YesNoCancel, FText::FromString(TEXT(
		"ミキサーと関連アセットの変更を保存して閉じますか？\n"
		"「いいえ」で閉じた場合、変更した音源や外したプリセットなどは、別途Save Allで保存してください。")));
	if (Choice == EAppReturnType::Cancel)
	{
		return false;
	}
	if (Choice == EAppReturnType::Yes && this->MixerWidget->SaveMixer(Error) == false)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return false;
	}
	return true;
}

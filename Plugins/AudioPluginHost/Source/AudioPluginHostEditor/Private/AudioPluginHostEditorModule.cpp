// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "AudioPluginHostEditorModule.h"

#include "AssetRegistry/AssetData.h"
#include "AudioDeviceHandle.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "IDesktopPlatform.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "VST3AudioSession.h"
#include "VST3EditorSession.h"
#include "VST3ModuleImporter.h"
#include "VST3NativeEditorWindow.h"
#include "VST3MixerAsset.h"
#include "VST3SubmixEffectPreset.h"

IMPLEMENT_MODULE(FAudioPluginHostEditorModule, AudioPluginHostEditor)

void FAudioPluginHostEditorModule::StartupModule()
{
	this->WindowTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAudioPluginHostEditorModule::TickWindows), 0.05f);
	IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAudioPluginHostEditorModule::RegisterMenus));
}

void FAudioPluginHostEditorModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
	FTSTicker::GetCoreTicker().RemoveTicker(this->WindowTicker);
	this->WindowTicker.Reset();

	for (const auto& Window : this->EditorWindows)
	{
		Window->Close(false);
	}
	this->EditorWindows.Reset();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

TValueOrError<bool, FText> FAudioPluginHostEditorModule::IsPIEAuthorizedInternal(const bool bIsSimulateInEditor) const
{
	return MakeValue(true);
}

TValueOrError<bool, FText> FAudioPluginHostEditorModule::RequestPIEPermissionInternal(const bool bIsSimulateInEditor) const
{
	for (const auto& Window : this->EditorWindows)
	{
		if (Window->IsStoppedEditor() == false)
		{
			continue;
		}
		FString Error;
		if (Window->PrepareForPIE(Error) == false)
		{
			return MakeError(FText::FromString(FString::Printf(
				TEXT("VST3プリセットの編集状態を保存できません。PIEを開始せず、GUIを確認してください。%s"), *Error)));
		}
	}

	auto* MutableThis = const_cast<FAudioPluginHostEditorModule*>(this);
	MutableThis->EditorWindows.RemoveAll([](const TSharedRef<FVST3NativeEditorWindow>& Window)
	{
		return Window->IsFinished();
	});
	return MakeValue(true);
}

bool FAudioPluginHostEditorModule::TickWindows(const float DeltaTime)
{
	this->EditorWindows.RemoveAll([](const TSharedRef<FVST3NativeEditorWindow>& Window)
	{
		return Window->Tick() == false;
	});
	return true;
}

void FAudioPluginHostEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	auto* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.File"));
	if (Menu == nullptr)
	{
		return;
	}

	auto& Section = Menu->FindOrAddSection(
		TEXT("AudioPluginHost"),
		NSLOCTEXT("AudioPluginHost", "VSTMenuHeading", "VST"),
		FToolMenuInsert(TEXT("Exit"), EToolMenuInsertType::Before));
	Section.AddMenuEntry(
		TEXT("AudioPluginHost.ImportVST3File"),
		FText::FromString(TEXT("Import VST3 File")),
		FText::FromString(TEXT("単一ファイル形式のVST3を明示的にインポートします。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAudioPluginHostEditorModule::ImportVST3File)));
	Section.AddMenuEntry(
		TEXT("AudioPluginHost.ImportVST3Directory"),
		FText::FromString(TEXT("Import VST3 Bundle Directory")),
		FText::FromString(TEXT("Windows VST3バンドルディレクトリを明示的にインポートします。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAudioPluginHostEditorModule::ImportVST3Directory)));
	Section.AddMenuEntry(
		TEXT("AudioPluginHost.OpenActiveVST3Editor"),
		FText::FromString(TEXT("Open VST3 GUI")),
		FText::FromString(TEXT("選択したVST3プリセットのネイティブGUIを開きます。PIE停止中も編集できます。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAudioPluginHostEditorModule::OpenVST3Editor)));
	Section.AddMenuEntry(
		TEXT("AudioPluginHost.OpenVST3Mixer"),
		FText::FromString(TEXT("VST Mixer")),
		FText::FromString(TEXT("選択中のVST Mixerアセットを開きます。")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAudioPluginHostEditorModule::OpenVST3Mixer)));
}

void FAudioPluginHostEditorModule::OpenVST3Mixer()
{
	auto& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> Assets;
	ContentBrowser.Get().GetSelectedAssets(Assets);
	if (Assets.Num() == 1)
	{
		if (auto* Mixer = Cast<UVST3MixerAsset>(Assets[0].GetAsset()); Mixer != nullptr)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Mixer);
			return;
		}
	}
	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(TEXT("Content BrowserでVST Mixerアセットを一つ選択してください。新規作成はAudioカテゴリから行えます。")));
}

void FAudioPluginHostEditorModule::ImportVST3File()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return;
	}

	TArray<FString> SelectedFiles;
	const auto ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (DesktopPlatform->OpenFileDialog(ParentWindowHandle, TEXT("VST3 File"), FString(), FString(), TEXT("VST3 Plugin (*.vst3)|*.vst3"), EFileDialogFlags::None, SelectedFiles)
		&& SelectedFiles.Num() == 1)
	{
		this->ImportSourcePath(SelectedFiles[0]);
	}
}

void FAudioPluginHostEditorModule::ImportVST3Directory()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return;
	}

	FString SelectedDirectory;
	const auto ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("VST3 Bundle Directory"), FString(), SelectedDirectory))
	{
		this->ImportSourcePath(SelectedDirectory);
	}
}

void FAudioPluginHostEditorModule::ImportSourcePath(const FString& SourcePath)
{
	const auto Response = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("このVST3をプロジェクトへコピーし、DevelopmentとShippingのパッケージへ常に含めます。\n配布・再配布する権利を確認済みですか？")));
	if (Response != EAppReturnType::Yes)
	{
		return;
	}

	FVST3ImportRequest Request;
	Request.SourcePath = SourcePath;
	Request.bRedistributionRightsAcknowledged = true;
	FVST3ImportResult Result;
	if (FVST3ModuleImporter::ImportExplicit(Request, Result) == nullptr)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
	}
}

void FAudioPluginHostEditorModule::OpenVST3Editor()
{
	auto& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> Assets;
	ContentBrowser.Get().GetSelectedAssets(Assets);
	UVST3SubmixEffectPreset* SelectedPreset = nullptr;
	int32 PresetCount = 0;
	for (const auto& Asset : Assets)
	{
		if (Asset.AssetClassPath == UVST3SubmixEffectPreset::StaticClass()->GetClassPathName())
		{
			SelectedPreset = Cast<UVST3SubmixEffectPreset>(Asset.GetAsset());
			++PresetCount;
		}
	}
	if (PresetCount != 1 || IsValid(SelectedPreset) == false)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Content BrowserでVST3プリセットを一つ選択してください。")));
		return;
	}

	FText Error;
	if (this->OpenVST3EditorForPreset(SelectedPreset, Error) == false)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
	}
}

bool FAudioPluginHostEditorModule::OpenVST3EditorForPreset(UVST3SubmixEffectPreset* Preset, FText& OutError)
{
	FString Error;
	if (this->OpenVST3EditorForPresetInternal(Preset, Error) == false)
	{
		OutError = FText::FromString(Error);
		return false;
	}
	return true;
}

bool FAudioPluginHostEditorModule::TrySaveEditorsForPreset(UVST3SubmixEffectPreset* Preset, FText& OutError)
{
	if (IsValid(Preset) == false)
	{
		OutError = FText::FromString(TEXT("VST3プリセットが無効です。"));
		return false;
	}
	for (const auto& Window : this->EditorWindows)
	{
		if (Window->IsForPreset(*Preset) == true)
		{
			FString Error;
			if (Window->TryCaptureForSave(Error) == false)
			{
				OutError = FText::FromString(Error);
				return false;
			}
		}
	}
	return true;
}

bool FAudioPluginHostEditorModule::OpenVST3EditorForPresetInternal(UVST3SubmixEffectPreset* Preset, FString& OutError)
{
	if (IsValid(Preset) == false)
	{
		OutError = TEXT("VST3プリセットを選択してください。");
		return false;
	}

	const auto* PIEWorldContext = GEditor != nullptr ? GEditor->GetPIEWorldContext() : nullptr;
	const auto* PIEWorld = PIEWorldContext != nullptr ? PIEWorldContext->World() : nullptr;
	if (PIEWorld == nullptr)
	{
		for (const auto& Window : this->EditorWindows)
		{
			if (Window->IsForStoppedPreset(*Preset) == true)
			{
				return Window->Show(OutError);
			}
		}

		auto StoppedSession = FVST3StoppedEditorSession::Create(*Preset, OutError);
		if (StoppedSession.IsValid() == false)
		{
			return false;
		}
		TSharedRef<IVST3EditorSession> EditorSession = StaticCastSharedRef<IVST3EditorSession>(StoppedSession.ToSharedRef());
		auto Window = MakeShared<FVST3NativeEditorWindow>(EditorSession);
		if (Window->Show(OutError) == false)
		{
			return false;
		}
		this->EditorWindows.Add(Window);
		return true;
	}

	const auto AudioDevice = PIEWorld->GetAudioDevice();
	if (AudioDevice.IsValid() == false)
	{
		OutError = TEXT("PIEの音声デバイスがありません。音声出力を確認してください。");
		return false;
	}

	bool bAmbiguous = false;
	const auto Session = FVST3SessionService::Get().FindUniqueSession(Preset, AudioDevice.GetDeviceID(), bAmbiguous);
	if (Session.IsValid() == false)
	{
		OutError = bAmbiguous == true
			? TEXT("同じプリセットのVST3実行セッションが複数あります。各段に別のプリセットを割り当ててください。")
			: TEXT("PIEの音声デバイスに選択したVST3の実行セッションがありません。Submix Effect Chainとプリセットを確認してください。");
		return false;
	}

	for (const auto& Window : this->EditorWindows)
	{
		if (Window->IsForSession(*Session) == true)
		{
			return Window->Show(OutError);
		}
	}

	TSharedRef<IVST3EditorSession> EditorSession = StaticCastSharedRef<IVST3EditorSession>(
		MakeShared<FVST3LiveEditorSession>(Session.ToSharedRef()));
	auto Window = MakeShared<FVST3NativeEditorWindow>(EditorSession);
	if (Window->Show(OutError) == false)
	{
		return false;
	}

	this->EditorWindows.Add(Window);
	return true;
}

// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "Containers/Ticker.h"
#include "IPIEAuthorizer.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class FVST3NativeEditorWindow;
class UVST3SubmixEffectPreset;

/** インポートメニューと再生中・停止中のVST3 GUIを管理する。 */
class AUDIOPLUGINHOSTEDITOR_API FAudioPluginHostEditorModule final : public IModuleInterface, public IPIEAuthorizer
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	bool OpenVST3EditorForPreset(UVST3SubmixEffectPreset* Preset, FText& OutError);
	bool TrySaveEditorsForPreset(UVST3SubmixEffectPreset* Preset, FText& OutError);

private:
	virtual TValueOrError<bool, FText> IsPIEAuthorizedInternal(bool bIsSimulateInEditor) const override;
	virtual TValueOrError<bool, FText> RequestPIEPermissionInternal(bool bIsSimulateInEditor) const override;
	void RegisterMenus();
	void ImportVST3File();
	void ImportVST3Directory();
	void ImportSourcePath(const FString& SourcePath);
	void OpenVST3Editor();
	void OpenVST3Mixer();
	bool OpenVST3EditorForPresetInternal(UVST3SubmixEffectPreset* Preset, FString& OutError);
	bool TickWindows(float DeltaTime);

	TArray<TSharedRef<FVST3NativeEditorWindow>> EditorWindows;
	FTSTicker::FDelegateHandle WindowTicker;
};

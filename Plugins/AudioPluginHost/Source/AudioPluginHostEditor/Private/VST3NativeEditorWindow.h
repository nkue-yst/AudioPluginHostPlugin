// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "VST3EditorSession.h"

class SWindow;

/** 再生中または停止中の編集セッションに対応するネイティブGUIを所有する。 */
class FVST3NativeEditorWindow final : public TSharedFromThis<FVST3NativeEditorWindow>
{
public:
	explicit FVST3NativeEditorWindow(const TSharedRef<IVST3EditorSession>& InSession);
	~FVST3NativeEditorWindow();

	bool Show(FString& OutError);
	void Close(bool bSaveState = true);
	bool PrepareForPIE(FString& OutError);
	bool TryCaptureForSave(FString& OutError);
	bool Tick();
	bool IsForSession(const FVST3AudioSession& Candidate) const;
	bool IsForStoppedPreset(const UVST3SubmixEffectPreset& Candidate) const;
	bool IsForPreset(const UVST3SubmixEffectPreset& Candidate) const;
	bool IsFinished() const;
	bool IsStoppedEditor() const;

private:
	void DetachView();
	void HandleClosed(const TSharedRef<SWindow>& ClosedWindow);
	void HandleActivated();
	void HandleDeactivated();
	void ResizeParent(FIntPoint Size);
	IVST3EditorController* GetView() const;

	TSharedRef<IVST3EditorSession> Session;
	const uint32 ConfigurationRevision;

	TSharedPtr<SWindow> Window;
	FIntPoint LastSize = FIntPoint::ZeroValue;

	double CaptureStartedAt = 0.0;
	bool bAttached = false;
	bool bSaving = false;
	bool bSaveOnClose = true;
	bool bFinished = false;
};

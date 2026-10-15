// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "VST3AudioSession.h"

class UVST3SubmixEffectPreset;

/** ネイティブGUIが使う操作だけを、再生中と停止中のセッションで共有する。 */
class IVST3EditorSession
{
public:
	virtual ~IVST3EditorSession() = default;
	virtual IVST3EditorController* GetEditor() const = 0;
	virtual UVST3SubmixEffectPreset* GetPreset() const = 0;
	virtual uint32 GetConfigurationRevision() const = 0;
	virtual FString GetWindowTitle() const = 0;
	virtual EVST3StateCaptureResult CaptureState(FString& OutError) = 0;
	virtual void CancelStateCapture() = 0;
	virtual const FVST3AudioSession* GetLiveSession() const = 0;
	virtual bool IsStoppedEditor() const = 0;
};

class FVST3LiveEditorSession final : public IVST3EditorSession
{
public:
	explicit FVST3LiveEditorSession(const TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe>& InSession);
	virtual IVST3EditorController* GetEditor() const override;
	virtual UVST3SubmixEffectPreset* GetPreset() const override;
	virtual uint32 GetConfigurationRevision() const override;
	virtual FString GetWindowTitle() const override;
	virtual EVST3StateCaptureResult CaptureState(FString& OutError) override;
	virtual void CancelStateCapture() override;
	virtual const FVST3AudioSession* GetLiveSession() const override;
	virtual bool IsStoppedEditor() const override;

private:
	TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe> Session;
};

/** 音声セッションへ登録しない、ゲームスレッド専用の編集インスタンス。 */
class FVST3StoppedEditorSession final : public IVST3EditorSession
{
public:
	static TSharedPtr<FVST3StoppedEditorSession> Create(UVST3SubmixEffectPreset& InPreset, FString& OutError);
	virtual ~FVST3StoppedEditorSession() override;
	virtual IVST3EditorController* GetEditor() const override;
	virtual UVST3SubmixEffectPreset* GetPreset() const override;
	virtual uint32 GetConfigurationRevision() const override;
	virtual FString GetWindowTitle() const override;
	virtual EVST3StateCaptureResult CaptureState(FString& OutError) override;
	virtual void CancelStateCapture() override;
	virtual const FVST3AudioSession* GetLiveSession() const override;
	virtual bool IsStoppedEditor() const override;

private:
	FVST3StoppedEditorSession(UVST3SubmixEffectPreset& InPreset, FVST3SessionRequest&& InRequest,
		TUniquePtr<IVST3AudioProcessor>&& InProcessor);
	bool ValidateIdentity(FString& OutError) const;

	TStrongObjectPtr<UVST3SubmixEffectPreset> Preset;
	TUniquePtr<IVST3AudioProcessor> Processor;
	FVST3SessionRequest Request;
	const uint32 ConfigurationRevision;
};

// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3EditorSession.h"

#include "VST3PluginState.h"
#include "VST3SubmixEffectPreset.h"

FVST3LiveEditorSession::FVST3LiveEditorSession(const TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe>& InSession)
	: Session(InSession)
{
}

IVST3EditorController* FVST3LiveEditorSession::GetEditor() const
{
	return this->Session->GetEditor();
}

UVST3SubmixEffectPreset* FVST3LiveEditorSession::GetPreset() const
{
	return this->Session->GetPreset();
}

uint32 FVST3LiveEditorSession::GetConfigurationRevision() const
{
	return this->Session->GetConfigurationRevision();
}

FString FVST3LiveEditorSession::GetWindowTitle() const
{
	const auto* Owner = this->GetPreset();
	return FString::Printf(TEXT("%s — Audio Device %u"), Owner != nullptr ? *Owner->GetName() : TEXT("VST3"), this->Session->GetDeviceId());
}

EVST3StateCaptureResult FVST3LiveEditorSession::CaptureState(FString& OutError)
{
	return this->Session->CaptureState(OutError);
}

void FVST3LiveEditorSession::CancelStateCapture()
{
	this->Session->CancelStateCapture();
}

const FVST3AudioSession* FVST3LiveEditorSession::GetLiveSession() const
{
	return &this->Session.Get();
}

bool FVST3LiveEditorSession::IsStoppedEditor() const
{
	return false;
}

TSharedPtr<FVST3StoppedEditorSession> FVST3StoppedEditorSession::Create(UVST3SubmixEffectPreset& InPreset, FString& OutError)
{
	if (IsInGameThread() == false)
	{
		OutError = TEXT("VST3プリセットのGUIはゲームスレッドで開いてください。");
		return nullptr;
	}

	FVST3SessionRequest Request;
	if (InPreset.BuildSessionRequest(Request, OutError) == false)
	{
		return nullptr;
	}
	Request.SampleRate = 48000;
	Request.MaximumBlockFrames = 64;
	auto Processor = FVST3SessionService::Get().CreateStandaloneProcessor(Request, OutError);
	if (Processor == nullptr)
	{
		return nullptr;
	}
	if (Processor->GetEditor() == nullptr)
	{
		OutError = TEXT("このVST3 Audio EffectにはWindowsネイティブGUIがありません。");
		return nullptr;
	}
	return MakeShareable(new FVST3StoppedEditorSession(InPreset, MoveTemp(Request), MoveTemp(Processor)));
}

FVST3StoppedEditorSession::FVST3StoppedEditorSession(UVST3SubmixEffectPreset& InPreset, FVST3SessionRequest&& InRequest,
	TUniquePtr<IVST3AudioProcessor>&& InProcessor)
	: Preset(&InPreset)
	, Processor(MoveTemp(InProcessor))
	, Request(MoveTemp(InRequest))
	, ConfigurationRevision(InPreset.GetConfigurationRevision())
{
}

FVST3StoppedEditorSession::~FVST3StoppedEditorSession() = default;

bool FVST3StoppedEditorSession::ValidateIdentity(FString& OutError) const
{
	auto* Owner = this->Preset.Get();
	if (IsValid(Owner) == false || Owner->GetConfigurationRevision() != this->ConfigurationRevision)
	{
		OutError = TEXT("編集中にプリセットの設定が変更されました。GUIを閉じて開き直してください。");
		return false;
	}

	FVST3SessionRequest CurrentRequest;
	if (Owner->BuildSessionRequest(CurrentRequest, OutError) == false
		|| CurrentRequest.ModuleHash != this->Request.ModuleHash || CurrentRequest.ClassId != this->Request.ClassId)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = TEXT("編集中にVST3のバイナリまたはクラスが変更されました。");
		}
		return false;
	}
	return true;
}

IVST3EditorController* FVST3StoppedEditorSession::GetEditor() const
{
	const auto* Owner = this->Preset.Get();
	return this->Processor != nullptr && IsValid(Owner) == true
		? this->Processor->GetEditor() : nullptr;
}

UVST3SubmixEffectPreset* FVST3StoppedEditorSession::GetPreset() const
{
	return this->Preset.Get();
}

uint32 FVST3StoppedEditorSession::GetConfigurationRevision() const
{
	return this->ConfigurationRevision;
}

FString FVST3StoppedEditorSession::GetWindowTitle() const
{
	const auto* Owner = this->GetPreset();
	return FString::Printf(TEXT("%s — Preset Editor (Stopped)"), Owner != nullptr ? *Owner->GetName() : TEXT("VST3"));
}

EVST3StateCaptureResult FVST3StoppedEditorSession::CaptureState(FString& OutError)
{
	if (this->ValidateIdentity(OutError) == false)
	{
		return EVST3StateCaptureResult::Failed;
	}

	this->Processor->TickGameThread();
	if (this->Processor->FlushPendingEditorParameters(OutError) == false)
	{
		return EVST3StateCaptureResult::Failed;
	}
	this->Processor->StopProcessing();

	FVST3PluginState State;
	State.ModuleHash = this->Request.ModuleHash;
	State.ClassId = this->Request.ClassId;
	if (this->Processor->CaptureState(State.Component, State.Controller, OutError) == false
		|| this->ValidateIdentity(OutError) == false)
	{
		return EVST3StateCaptureResult::Failed;
	}

	if (this->Preset->SetSerializedState(State) == false)
	{
		OutError = TEXT("保存中にプリセットのモジュールまたはクラスが変更されました。");
		return EVST3StateCaptureResult::Failed;
	}
	return EVST3StateCaptureResult::Succeeded;
}

void FVST3StoppedEditorSession::CancelStateCapture()
{
}

const FVST3AudioSession* FVST3StoppedEditorSession::GetLiveSession() const
{
	return nullptr;
}

bool FVST3StoppedEditorSession::IsStoppedEditor() const
{
	return true;
}

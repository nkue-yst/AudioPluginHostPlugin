// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3AudioSession.h"

#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "VST3PluginState.h"
#include "VST3SubmixEffectPreset.h"

DEFINE_LOG_CATEGORY_STATIC(LogVST3Session, Log, All);

FVST3AudioSession::FVST3AudioSession(TWeakObjectPtr<UVST3SubmixEffectPreset> InPreset, const int32 InSampleRate, const uint32 InDeviceId)
	: Preset(InPreset)
	, SampleRate(InSampleRate)
	, DeviceId(InDeviceId)
{
}

FVST3AudioSession::~FVST3AudioSession() = default;

bool FVST3AudioSession::ProcessStereo(float* Output, const float* Input, const int32 NumFrames)
{
	if (Output == nullptr || Input == nullptr || NumFrames <= 0)
	{
		return false;
	}

	if (Output != Input)
	{
		FMemory::Memcpy(Output, Input, sizeof(float) * static_cast<SIZE_T>(NumFrames) * 2);
	}

	const auto bReturnDry = this->bBypassed.load();
	if (this->bSuspended.load() == true || this->bReleased.load() == true
		|| (bReturnDry == true && this->bCaptureRequested.load() == false) || this->bDryLatched.load() == true)
	{
		return false;
	}

	bool Expected = false;
	if (this->bRenderInProgress.compare_exchange_strong(Expected, true) == false)
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		this->bRenderInProgress.store(false);
	};

	// 管理側が停止要求を出す直前にここへ入った場合も、SDKへ進む前に再確認する。
	if (this->bSuspended.load() == true || this->Processor == nullptr || NumFrames > this->Request.MaximumBlockFrames)
	{
		return false;
	}

	const auto Stride = this->Request.MaximumBlockFrames;
	auto* LeftIn = this->Buffers.GetData();
	auto* RightIn = LeftIn + Stride;
	auto* LeftOut = RightIn + Stride;
	auto* RightOut = LeftOut + Stride;

	for (auto Frame = 0; Frame < NumFrames; ++Frame)
	{
		LeftIn[Frame] = Input[Frame * 2];
		RightIn[Frame] = Input[Frame * 2 + 1];
	}

	FMemory::Memzero(LeftOut, sizeof(float) * NumFrames);
	FMemory::Memzero(RightOut, sizeof(float) * NumFrames);
	if (this->Processor->ProcessAudio(LeftIn, RightIn, LeftOut, RightOut, NumFrames) == false)
	{
		if (++this->ConsecutiveFailures >= 3)
		{
			this->bDryLatched.store(true);
			this->Processor->StopProcessing();
		}
		return false;
	}

	for (auto Frame = 0; Frame < NumFrames; ++Frame)
	{
		if (FMath::IsFinite(LeftOut[Frame]) == false || FMath::IsFinite(RightOut[Frame]) == false)
		{
			this->bDryLatched.store(true);
			this->Processor->StopProcessing();
			return false;
		}
	}

	this->ConsecutiveFailures = 0;
	if (bReturnDry == true)
	{
		return false;
	}

	for (auto Frame = 0; Frame < NumFrames; ++Frame)
	{
		Output[Frame * 2] = LeftOut[Frame];
		Output[Frame * 2 + 1] = RightOut[Frame];
	}
	return true;
}

void FVST3AudioSession::ReleaseFromEffect()
{
	// 最後のコールバック後に呼ばれる。SDKは次のゲームスレッド更新で破棄する。
	this->bReleased.store(true);
}

bool FVST3AudioSession::IsReady() const
{
	return this->bReady == true && this->bReleased.load() == false;
}

bool FVST3AudioSession::IsDryLatched() const
{
	return this->bDryLatched.load();
}

UVST3SubmixEffectPreset* FVST3AudioSession::GetPreset() const
{
	return this->Preset.Get();
}

uint32 FVST3AudioSession::GetDeviceId() const
{
	return this->DeviceId;
}

uint32 FVST3AudioSession::GetConfigurationRevision() const
{
	return this->PreparedRevision;
}

IVST3EditorController* FVST3AudioSession::GetEditor() const
{
	return this->IsReady() == true ? this->Processor->GetEditor() : nullptr;
}

EVST3StateCaptureResult FVST3AudioSession::CaptureState(FString& OutError)
{
	if (this->IsReady() == false)
	{
		OutError = TEXT("保存対象のVST3セッションが終了しました。");
		return EVST3StateCaptureResult::Failed;
	}
	auto* Owner = this->Preset.Get();
	if (Owner == nullptr || Owner->GetConfigurationRevision() != this->PreparedRevision)
	{
		this->CancelStateCapture();
		OutError = TEXT("プリセットが変更されたため、以前の実行状態は保存できません。");
		return EVST3StateCaptureResult::Failed;
	}

	this->bCaptureRequested.store(true);
	if (this->Processor->HasPendingParameters() == true)
	{
		this->bSuspended.store(false);
		return EVST3StateCaptureResult::Pending;
	}

	this->bSuspended.store(true);
	bool Expected = false;
	if (this->bRenderInProgress.compare_exchange_strong(Expected, true) == false)
	{
		return EVST3StateCaptureResult::Pending;
	}

	ON_SCOPE_EXIT
	{
		this->bCaptureRequested.store(false);
		this->bRenderInProgress.store(false);
		this->bSuspended.store(false);
	};

	this->Processor->StopProcessing();
	if (this->ConsecutiveFailures > 0 || this->bDryLatched.load() == true)
	{
		OutError = TEXT("VST3の音声処理に失敗したため、現在の状態を保存できません。");
		return EVST3StateCaptureResult::Failed;
	}

	FVST3PluginState State;
	State.ModuleHash = this->Request.ModuleHash;
	State.ClassId = this->Request.ClassId;
	if (this->Processor->CaptureState(State.Component, State.Controller, OutError) == false)
	{
		return EVST3StateCaptureResult::Failed;
	}

	Owner = this->Preset.Get();
	if (Owner == nullptr || Owner->GetConfigurationRevision() != this->PreparedRevision
		|| Owner->SetSerializedState(State) == false)
	{
		OutError = TEXT("保存中にプリセットのモジュールまたはクラスが変更されました。");
		return EVST3StateCaptureResult::Failed;
	}
	return EVST3StateCaptureResult::Succeeded;
}

void FVST3AudioSession::CancelStateCapture()
{
	this->bCaptureRequested.store(false);
	if (this->IsReady() == true)
	{
		this->bSuspended.store(false);
	}
}

void FVST3AudioSession::Prepare(IVST3AudioProcessorFactory& Factory, UVST3SubmixEffectPreset& Owner)
{
	this->Processor.Reset();
	this->bReady = false;
	this->PreparedRevision = Owner.GetConfigurationRevision();
	this->ConsecutiveFailures = 0;
	this->bDryLatched.store(false);

	FString Error;
	this->Request = {};
	if (Owner.BuildSessionRequest(this->Request, Error) == true)
	{
		this->Request.SampleRate = this->SampleRate;
		this->Buffers.SetNumZeroed(this->Request.MaximumBlockFrames * 4);
		this->Processor = Factory.CreateProcessor(this->Request, Error);
		this->bReady = this->Processor != nullptr;
	}

	if (this->bReady == false)
	{
		UE_LOG(LogVST3Session, Warning, TEXT("%s: %s"), *Owner.GetPathName(), *Error);
	}
}

bool FVST3AudioSession::TickGameThread(IVST3AudioProcessorFactory* Factory)
{
	auto* Owner = this->Preset.Get();
	if (this->bReleased.load() == true || Owner == nullptr)
	{
		return this->Close();
	}

	this->bBypassed.store(Owner->IsHostBypassed());
	if (Factory != nullptr && this->PreparedRevision != Owner->GetConfigurationRevision())
	{
		this->bSuspended.store(true);
		bool Expected = false;
		if (this->bRenderInProgress.compare_exchange_strong(Expected, true) == false)
		{
			return false;
		}

		this->Prepare(*Factory, *Owner);
		this->bRenderInProgress.store(false);
		this->bSuspended.store(this->bReady == false);
	}

	if (this->Processor != nullptr)
	{
		this->Processor->TickGameThread();
	}
	return false;
}

bool FVST3AudioSession::Close()
{
	this->bSuspended.store(true);
	bool Expected = false;
	if (this->bRenderInProgress.compare_exchange_strong(Expected, true) == false)
	{
		return false;
	}

	this->bReady = false;
	this->Processor.Reset();
	this->Buffers.Empty();
	this->Request = {};
	this->bRenderInProgress.store(false);
	return true;
}

FVST3SessionService& FVST3SessionService::Get()
{
	static FVST3SessionService Instance;
	return Instance;
}

void FVST3SessionService::RegisterFactory(TUniquePtr<IVST3AudioProcessorFactory> InFactory)
{
	this->Factory = MoveTemp(InFactory);
	FScopeLock Lock(&this->RegistrationMutex);
	this->bAcceptingSessions = true;
}

TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe> FVST3SessionService::CreateSession(
	TWeakObjectPtr<UVST3SubmixEffectPreset> Preset, const int32 SampleRate, const uint32 DeviceId)
{
	auto Session = MakeShared<FVST3AudioSession, ESPMode::ThreadSafe>(Preset, SampleRate, DeviceId);

	// 登録はエフェクト初期化時だけ。音声callbackはこのmutexへアクセスしない。
	FScopeLock Lock(&this->RegistrationMutex);
	if (this->bAcceptingSessions == true)
	{
		this->PendingSessions.Add(Session);
	}
	return Session;
}

void FVST3SessionService::TickGameThread()
{
	{
		FScopeLock Lock(&this->RegistrationMutex);
		this->Sessions.Append(MoveTemp(this->PendingSessions));
		this->PendingSessions.Reset();
	}

	this->Sessions.RemoveAll([this](const TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe>& Session)
	{
		return Session->TickGameThread(this->Factory.Get());
	});
}

TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> FVST3SessionService::FindSession(const UVST3SubmixEffectPreset* Preset) const
{
	for (const auto& Session : this->Sessions)
	{
		if (Session->IsReady() == true && (Preset == nullptr || Session->GetPreset() == Preset))
		{
			return Session;
		}
	}
	return nullptr;
}

TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> FVST3SessionService::FindSession(
	const UVST3SubmixEffectPreset* Preset, const uint32 DeviceId) const
{
	for (const auto& Session : this->Sessions)
	{
		if (Session->IsReady() == true && Session->GetDeviceId() == DeviceId
			&& (Preset == nullptr || Session->GetPreset() == Preset))
		{
			return Session;
		}
	}
	return nullptr;
}

TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> FVST3SessionService::FindUniqueSession(
	const UVST3SubmixEffectPreset* Preset, const uint32 DeviceId, bool& bOutAmbiguous) const
{
	bOutAmbiguous = false;
	TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> Found;
	for (const auto& Session : this->Sessions)
	{
		if (Session->IsReady() == true && Session->GetPreset() == Preset && Session->GetDeviceId() == DeviceId)
		{
			if (Found.IsValid() == true)
			{
				bOutAmbiguous = true;
				return nullptr;
			}
			Found = Session;
		}
	}
	return Found;
}

TUniquePtr<IVST3AudioProcessor> FVST3SessionService::CreateStandaloneProcessor(
	const FVST3SessionRequest& Request, FString& OutError)
{
	if (IsInGameThread() == false || this->Factory == nullptr)
	{
		OutError = TEXT("VST3の生成サービスをゲームスレッドで利用できません。");
		return nullptr;
	}
	return this->Factory->CreateProcessor(Request, OutError);
}

void FVST3SessionService::Shutdown()
{
	{
		FScopeLock Lock(&this->RegistrationMutex);
		this->bAcceptingSessions = false;
		this->Sessions.Append(MoveTemp(this->PendingSessions));
		this->PendingSessions.Reset();
	}

	for (const auto& Session : this->Sessions)
	{
		// 終了時だけ進行中のcallbackを待ち、SDKモジュールのアンロード前に破棄する。
		while (Session->Close() == false)
		{
			FPlatformProcess::SleepNoStats(0.001f);
		}
	}

	this->Sessions.Reset();
	this->Factory.Reset();
}

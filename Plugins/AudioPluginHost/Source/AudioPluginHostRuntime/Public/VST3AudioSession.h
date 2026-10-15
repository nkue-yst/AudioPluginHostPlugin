// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "VST3Processor.h"
#include <atomic>

class UVST3SubmixEffectPreset;

enum class EVST3StateCaptureResult : uint8
{
	Pending,
	Succeeded,
	Failed
};

/**
 * エフェクトと管理サービスが寿命を共有する。SDKの所有権はゲームスレッドだけが操作する。
 * 管理処理と重なった音声ブロックは、待たずに入力を返す。
 */
class AUDIOPLUGINHOSTRUNTIME_API FVST3AudioSession final
{
public:
	FVST3AudioSession(TWeakObjectPtr<UVST3SubmixEffectPreset> InPreset, int32 InSampleRate, uint32 InDeviceId);
	~FVST3AudioSession();

	bool ProcessStereo(float* Output, const float* Input, int32 NumFrames);
	void ReleaseFromEffect();

	// 以下はゲームスレッド専用。返したビューの寿命はこのセッションが管理する。
	bool IsReady() const;
	bool IsDryLatched() const;
	UVST3SubmixEffectPreset* GetPreset() const;
	uint32 GetDeviceId() const;
	uint32 GetConfigurationRevision() const;
	IVST3EditorController* GetEditor() const;
	EVST3StateCaptureResult CaptureState(FString& OutError);
	void CancelStateCapture();

private:
	friend class FVST3SessionService;
	bool TickGameThread(IVST3AudioProcessorFactory* Factory);
	bool Close();
	void Prepare(IVST3AudioProcessorFactory& Factory, UVST3SubmixEffectPreset& Owner);

	TWeakObjectPtr<UVST3SubmixEffectPreset> Preset;
	const int32 SampleRate;
	const uint32 DeviceId;

	TUniquePtr<IVST3AudioProcessor> Processor;
	FVST3SessionRequest Request;
	TArray<float> Buffers;

	uint32 PreparedRevision = 0;
	int32 ConsecutiveFailures = 0;
	bool bReady = false;

	std::atomic<bool> bReleased
	{
		false
	};
	std::atomic<bool> bSuspended
	{
		true
	};
	std::atomic<bool> bRenderInProgress
	{
		false
	};
	std::atomic<bool> bBypassed
	{
		false
	};
	std::atomic<bool> bCaptureRequested
	{
		false
	};
	std::atomic<bool> bDryLatched
	{
		false
	};
};

/** セッションの登録を受け付け、SDKの生成・更新・破棄をゲームスレッドへ集約する。 */
class AUDIOPLUGINHOSTRUNTIME_API FVST3SessionService final
{
public:
	static FVST3SessionService& Get();
	void RegisterFactory(TUniquePtr<IVST3AudioProcessorFactory> Factory);
	void Shutdown();

	TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe> CreateSession(
		TWeakObjectPtr<UVST3SubmixEffectPreset> Preset, int32 SampleRate, uint32 DeviceId);
	TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> FindSession(const UVST3SubmixEffectPreset* Preset = nullptr) const;
	TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> FindSession(const UVST3SubmixEffectPreset* Preset, uint32 DeviceId) const;
	TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> FindUniqueSession(
		const UVST3SubmixEffectPreset* Preset, uint32 DeviceId, bool& bOutAmbiguous) const;
	TUniquePtr<IVST3AudioProcessor> CreateStandaloneProcessor(const FVST3SessionRequest& Request, FString& OutError);
	void TickGameThread();

private:
	FVST3SessionService() = default;

	FCriticalSection RegistrationMutex;
	bool bAcceptingSessions = true;
	TArray<TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe>> PendingSessions;
	TArray<TSharedRef<FVST3AudioSession, ESPMode::ThreadSafe>> Sessions;
	TUniquePtr<IVST3AudioProcessorFactory> Factory;
};

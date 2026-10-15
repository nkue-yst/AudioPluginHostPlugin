// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"

/** SDKに渡す値は0から1の有限値に限定する。 */
struct FVST3ParameterChange
{
	uint32 ParameterId = 0;
	double NormalizedValue = 0.0;
};

/** UObjectを含まない、生成時点の設定。 */
struct AUDIOPLUGINHOSTRUNTIME_API FVST3SessionRequest
{
	FString BundleRelativePath;
	FString EntryBinaryRelativePath;
	FString ClassId;
	FString ModuleHash;

	TArray<uint8> ComponentState;
	TArray<uint8> ControllerState;

	int32 SampleRate = 48000;
	int32 MaximumBlockFrames = 4096;
};

/** ゲームスレッドから操作するSDK非依存のビュー。所有者はプロセッサ。 */
class AUDIOPLUGINHOSTRUNTIME_API IVST3EditorController
{
public:
	virtual ~IVST3EditorController() = default;
	virtual bool Attach(void* ParentWindowHandle, FString& OutError) = 0;
	virtual void Detach() = 0;
	virtual FIntPoint GetSize() const = 0;
	virtual bool CanResize() const = 0;
	virtual bool Resize(FIntPoint Size) = 0;
	virtual void SetFocused(bool bFocused) = 0;
	virtual void SetResizeCallback(TFunction<void(FIntPoint)> Callback) = 0;
};

/** 生成・破棄・状態操作はゲームスレッド、ProcessAudioだけが音声スレッド。 */
class AUDIOPLUGINHOSTRUNTIME_API IVST3AudioProcessor
{
public:
	virtual ~IVST3AudioProcessor() = default;
	virtual bool ProcessAudio(float* InLeft, float* InRight, float* OutLeft, float* OutRight, int32 NumFrames) = 0;
	virtual void StopProcessing() = 0;
	virtual void TickGameThread() = 0;
	virtual bool HasPendingParameters() const = 0;
	virtual bool FlushPendingEditorParameters(FString& OutError) = 0;
	virtual bool CaptureState(TArray<uint8>& OutComponentState, TArray<uint8>& OutControllerState, FString& OutError) = 0;
	virtual IVST3EditorController* GetEditor() = 0;
};

class AUDIOPLUGINHOSTRUNTIME_API IVST3AudioProcessorFactory
{
public:
	virtual ~IVST3AudioProcessorFactory() = default;
	virtual TUniquePtr<IVST3AudioProcessor> CreateProcessor(const FVST3SessionRequest& Request, FString& OutError) = 0;
};

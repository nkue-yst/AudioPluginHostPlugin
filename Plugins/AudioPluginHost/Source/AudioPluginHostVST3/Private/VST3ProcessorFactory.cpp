// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3ProcessorFactory.h"

#include "VST3PayloadValidation.h"

#if AUDIOPLUGINHOST_WITH_VST3SDK
#include "Algo/Unique.h"
#include "VST3NativeView.h"
#include "VST3ParameterTransfer.h"
#include "VST3PluginInstance.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"

namespace AudioPluginHost::VST3
{
	class FHostedProcessor;

	/** Controllerの通知先を固定し、切断後の通知を無視する。 */
	class FComponentHandler final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Vst::IComponentHandler>>
	{
	public:
		FHostedProcessor* Owner = nullptr;
		virtual Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID Id) override;
		virtual Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID Id, Steinberg::Vst::ParamValue Value) override;
		virtual Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID Id) override;
		virtual Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 Flags) override;
	};

	class FHostedProcessor final : public IVST3AudioProcessor
	{
	public:
		static TUniquePtr<FHostedProcessor> Create(const FVST3SessionRequest& Request, FString& OutError)
		{
			FString Path;
			if (FVST3PayloadValidation::ResolveManagedBundle(Request.BundleRelativePath, Request.EntryBinaryRelativePath, Request.ModuleHash, Path, OutError) == false)
			{
				return nullptr;
			}

			const auto Module = FLoadedModule::Open(Path, OutError);
			if (Module.IsValid() == false)
			{
				return nullptr;
			}

			TUniquePtr<FPluginInstance> Instance;
			for (const auto& Info : Module->Module->getFactory().classInfos())
			{
				if (ToClassId(Info.ID()) == Request.ClassId && Info.category() == kVstAudioEffectClass)
				{
					Instance = FPluginInstance::Create(Module.ToSharedRef(), Info, OutError);
					break;
				}
			}

			if (Instance == nullptr)
			{
				if (OutError.IsEmpty() == true)
				{
					OutError = TEXT("指定したVST3 Audio EffectのClass IDがありません。");
				}
				return nullptr;
			}

			if (Instance->RestoreState(Request.ComponentState, Request.ControllerState, OutError) == false
				|| Instance->Activate(Request.SampleRate, Request.MaximumBlockFrames, OutError) == false)
			{
				return nullptr;
			}

			auto Result = MakeUnique<FHostedProcessor>();
			Result->Instance = MoveTemp(Instance);
			Result->SampleRate = Request.SampleRate;
			Result->MaximumBlockFrames = Request.MaximumBlockFrames;

			auto* Controller = Result->Instance->Controller.get();
			if (Controller != nullptr)
			{
				const auto Count = Controller->getParameterCount();
				if (Count < 0 || Count > 65536)
				{
					OutError = TEXT("VST3のパラメーター数が対応範囲を超えています。");
					return nullptr;
				}
				for (auto Index = 0; Index < Count; ++Index)
				{
					Steinberg::Vst::ParameterInfo Info {};
					if (Controller->getParameterInfo(Index, Info) == Steinberg::kResultOk && Info.id != Steinberg::Vst::kNoParamId)
					{
						Result->ParameterIds.Add(Info.id);
					}
				}
				Result->ParameterIds.Sort();
				Result->ParameterIds.SetNum(Algo::Unique(Result->ParameterIds));
			}

			Result->InputMailbox.Initialize(Result->ParameterIds);
			Result->OutputMailbox.Initialize(Result->ParameterIds);
			Result->InputChanges.Initialize(Result->ParameterIds);
			Result->OutputChanges.Initialize(Result->ParameterIds);

			if (Controller != nullptr)
			{
				Result->Handler = Steinberg::owned(new FComponentHandler());
				Result->Handler->Owner = Result.Get();
				if (Controller->setComponentHandler(Result->Handler.get()) != Steinberg::kResultOk)
				{
					OutError = TEXT("VST3 controllerの通知先を登録できません。");
					return nullptr;
				}
				Result->View = FNativeView::Create(Controller);
			}

			return Result;
		}

		virtual ~FHostedProcessor() override
		{
			this->StopProcessing();
			this->View.Reset();
			if (this->Handler != nullptr)
			{
				this->Handler->Owner = nullptr;
				this->Instance->Controller->setComponentHandler(nullptr);
				this->Handler = nullptr;
			}
			this->Instance.Reset();
		}

		virtual bool ProcessAudio(float* InLeft, float* InRight, float* OutLeft, float* OutRight, const int32 NumFrames) override
		{
			using namespace Steinberg;
			using namespace Steinberg::Vst;
			if (NumFrames <= 0 || NumFrames > this->MaximumBlockFrames || this->bRequiresRestart.load() == true)
			{
				return false;
			}

			if (this->bProcessing == false)
			{
				const auto Result = this->Instance->Processor->setProcessing(true);
				if (Result != kResultOk && Result != kNotImplemented)
				{
					return false;
				}
				this->bProcessing = true;
			}

			this->InputMailbox.Drain([this](const FVST3ParameterChange& Change)
			{
				int32 QueueIndex = 0;
				auto* Queue = this->InputChanges.addParameterData(Change.ParameterId, QueueIndex);
				if (Queue != nullptr)
				{
					int32 PointIndex = 0;
					Queue->addPoint(0, Change.NormalizedValue, PointIndex);
				}
			});
			this->bHasUnprocessedParameters.store(this->InputChanges.getParameterCount() > 0);

			float* InputChannels[] =
			{
				InLeft, InRight
			};
			float* OutputChannels[] =
			{
				OutLeft, OutRight
			};

			AudioBusBuffers Input {};
			Input.numChannels = 2;
			Input.channelBuffers32 = InputChannels;
			AudioBusBuffers Output {};
			Output.numChannels = 2;
			Output.channelBuffers32 = OutputChannels;

			ProcessContext Context {};
			Context.sampleRate = this->SampleRate;
			Context.projectTimeSamples = this->ProcessedFrames;
			Context.continousTimeSamples = this->ProcessedFrames;
			Context.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;

			ProcessData Data {};
			Data.processMode = kRealtime;
			Data.symbolicSampleSize = kSample32;
			Data.numSamples = NumFrames;
			Data.numInputs = 1;
			Data.numOutputs = 1;
			Data.inputs = &Input;
			Data.outputs = &Output;
			Data.inputParameterChanges = &this->InputChanges;
			Data.outputParameterChanges = &this->OutputChanges;
			Data.processContext = &Context;

			const auto bProcessed = this->Instance->Processor->process(Data) == kResultOk;
			this->ProcessedFrames += NumFrames;
			if (bProcessed == true)
			{
				for (auto Index = 0; Index < this->OutputChanges.getParameterCount(); ++Index)
				{
					auto* Queue = this->OutputChanges.getParameterData(Index);
					int32 Offset = 0;
					double Value = 0.0;
					if (Queue->getPoint(0, Offset, Value) == kResultOk)
					{
						this->OutputMailbox.Submit({ Queue->getParameterId(), Value });
					}
				}

				if ((Output.silenceFlags & 1ULL) != 0)
				{
					FMemory::Memzero(OutLeft, sizeof(float) * NumFrames);
				}
				if ((Output.silenceFlags & 2ULL) != 0)
				{
					FMemory::Memzero(OutRight, sizeof(float) * NumFrames);
				}
			}

			if (bProcessed == true)
			{
				// 失敗時は入力を保持し、次のblockで新しいGUI値を上書きして再送する。
				this->InputChanges.Clear();
				this->bHasUnprocessedParameters.store(false);
			}
			this->OutputChanges.Clear();
			return bProcessed;
		}

		virtual void StopProcessing() override
		{
			if (this->Instance != nullptr && this->bProcessing == true)
			{
				this->Instance->Processor->setProcessing(false);
				this->bProcessing = false;
			}
		}

		virtual void TickGameThread() override
		{
			auto* Controller = this->Instance->Controller.get();
			if (Controller == nullptr)
			{
				return;
			}

			this->OutputMailbox.Drain([Controller](const FVST3ParameterChange& Change)
			{
				Controller->setParamNormalized(Change.ParameterId, Change.NormalizedValue);
			});

			const auto Flags = this->RestartFlags.exchange(0);
			if ((Flags & Steinberg::Vst::kParamValuesChanged) != 0)
			{
				for (const auto Id : this->ParameterIds)
				{
					this->InputMailbox.Submit({ Id, Controller->getParamNormalized(Id) });
				}
			}
		}

		virtual bool HasPendingParameters() const override
		{
			return this->InputMailbox.HasPending() || this->bHasUnprocessedParameters.load();
		}

		virtual bool FlushPendingEditorParameters(FString& OutError) override
		{
			if (IsInGameThread() == false)
			{
				OutError = TEXT("VST3の編集状態はゲームスレッドで確定してください。");
				return false;
			}

			this->TickGameThread();
			if (this->HasPendingParameters() == false)
			{
				return true;
			}

			// 停止中も最後のGUI操作をcomponentへ届けるため、無音を一度だけ処理する。
			float SilentLeft[64] {};
			float SilentRight[64] {};
			float OutputLeft[64] {};
			float OutputRight[64] {};
			const auto Frames = FMath::Min(this->MaximumBlockFrames, static_cast<int32>(UE_ARRAY_COUNT(SilentLeft)));
			if (this->ProcessAudio(SilentLeft, SilentRight, OutputLeft, OutputRight, Frames) == false)
			{
				OutError = TEXT("VST3の最後のパラメーター変更をcomponentへ反映できません。");
				return false;
			}

			this->TickGameThread();
			if (this->HasPendingParameters() == true)
			{
				OutError = TEXT("VST3のパラメーター変更が処理後も残っています。");
				return false;
			}
			return true;
		}

		virtual bool CaptureState(TArray<uint8>& OutComponentState, TArray<uint8>& OutControllerState, FString& OutError) override
		{
			using namespace Steinberg;
			MemoryStream ComponentStream;
			if (this->Instance->Component->getState(&ComponentStream) != kResultOk)
			{
				OutError = TEXT("VST3 component stateを取得できません。");
				return false;
			}

			OutComponentState.Reset();
			if (ComponentStream.getSize() > 0)
			{
				OutComponentState.Append(reinterpret_cast<const uint8*>(ComponentStream.getData()), ComponentStream.getSize());
			}

			OutControllerState.Reset();
			if (this->Instance->Controller != nullptr)
			{
				MemoryStream ControllerStream;
				const auto Result = this->Instance->Controller->getState(&ControllerStream);
				if (Result != kResultOk && Result != kNotImplemented)
				{
					OutError = TEXT("VST3 controller stateを取得できません。");
					return false;
				}
				if (Result == kResultOk && ControllerStream.getSize() > 0)
				{
					OutControllerState.Append(reinterpret_cast<const uint8*>(ControllerStream.getData()), ControllerStream.getSize());
				}
			}
			return true;
		}

		virtual IVST3EditorController* GetEditor() override
		{
			return this->View.Get();
		}

		bool SubmitParameter(const uint32 Id, const double Value)
		{
			return this->InputMailbox.Submit({ Id, Value });
		}

		void RequestRestart(const uint32 Flags)
		{
			this->RestartFlags.fetch_or(Flags);
			constexpr uint32 UnsupportedChanges = Steinberg::Vst::kReloadComponent | Steinberg::Vst::kIoChanged
				| Steinberg::Vst::kLatencyChanged | Steinberg::Vst::kParamTitlesChanged;
			if ((Flags & UnsupportedChanges) != 0)
			{
				this->bRequiresRestart.store(true);
			}
		}

	private:
		TUniquePtr<FPluginInstance> Instance;
		TUniquePtr<FNativeView> View;
		Steinberg::IPtr<FComponentHandler> Handler;

		TArray<uint32> ParameterIds;
		FParameterMailbox InputMailbox;
		FParameterMailbox OutputMailbox;
		FParameterChanges InputChanges;
		std::atomic<bool> bHasUnprocessedParameters { false };
		FParameterChanges OutputChanges;

		int32 SampleRate = 0;
		int32 MaximumBlockFrames = 0;
		int64 ProcessedFrames = 0;
		bool bProcessing = false;

		std::atomic<uint32> RestartFlags
		{
			0
		};
		std::atomic<bool> bRequiresRestart
		{
			false
		};
	};

	Steinberg::tresult PLUGIN_API FComponentHandler::beginEdit(const Steinberg::Vst::ParamID Id)
	{
		return this->Owner != nullptr ? Steinberg::kResultOk : Steinberg::kResultFalse;
	}

	Steinberg::tresult PLUGIN_API FComponentHandler::endEdit(const Steinberg::Vst::ParamID Id)
	{
		return this->Owner != nullptr ? Steinberg::kResultOk : Steinberg::kResultFalse;
	}

	Steinberg::tresult PLUGIN_API FComponentHandler::performEdit(const Steinberg::Vst::ParamID Id, const Steinberg::Vst::ParamValue Value)
	{
		return this->Owner != nullptr && this->Owner->SubmitParameter(Id, Value) == true ? Steinberg::kResultOk : Steinberg::kResultFalse;
	}

	Steinberg::tresult PLUGIN_API FComponentHandler::restartComponent(const Steinberg::int32 Flags)
	{
		if (this->Owner == nullptr)
		{
			return Steinberg::kResultFalse;
		}
		this->Owner->RequestRestart(static_cast<uint32>(Flags));
		return Steinberg::kResultOk;
	}
}
#endif

TUniquePtr<IVST3AudioProcessor> FVST3ProcessorFactory::CreateProcessor(const FVST3SessionRequest& Request, FString& OutError)
{
	OutError.Reset();
	if (IsInGameThread() == false || Request.MaximumBlockFrames <= 0 || Request.MaximumBlockFrames > 4096
		|| Request.SampleRate < 8000 || Request.SampleRate > 384000 || FVST3PayloadValidation::IsLowerHex(Request.ClassId, 32) == false)
	{
		OutError = TEXT("VST3のセッション要求または呼び出しスレッドが不正です。");
		return nullptr;
	}
#if AUDIOPLUGINHOST_WITH_VST3SDK
	return AudioPluginHost::VST3::FHostedProcessor::Create(Request, OutError);
#else
	OutError = TEXT("VST3 SDKがありません。SetupVST3SDK.ps1を実行してください。");
	return nullptr;
#endif
}

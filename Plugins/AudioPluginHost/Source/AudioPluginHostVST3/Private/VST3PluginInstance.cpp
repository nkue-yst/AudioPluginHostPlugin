// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3PluginInstance.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Windows/WindowsHWrapper.h"

#if AUDIOPLUGINHOST_WITH_VST3SDK
#include "public.sdk/source/common/memorystream.h"

namespace AudioPluginHost::VST3
{
	FString ToClassId(const ::VST3::UID& Id)
	{
		return BytesToHex(reinterpret_cast<const uint8*>(Id.data()), 16).ToLower();
	}

	FProvider::FProvider(const PluginFactory& Factory, const ClassInfo& Info)
		: PlugProvider(Factory, Info, true)
	{
	}

	bool FProvider::Initialize(Steinberg::FUnknown* Host)
	{
		return this->setupPlugin(Host);
	}

	static bool ToSdkLoadPath(const FString& Path, FString& OutPath)
	{
		const auto AbsolutePath = FPaths::ConvertRelativePathToFull(Path);
		auto EntryPath = AbsolutePath;
		if (IFileManager::Get().DirectoryExists(*AbsolutePath) == true)
		{
			EntryPath /= TEXT("Contents/x86_64-win");
			EntryPath /= FPaths::GetCleanFilename(AbsolutePath);
		}

		if (EntryPath.Len() < MAX_PATH)
		{
			OutPath = Path;
			return true;
		}

		OutPath = AbsolutePath;
		OutPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (OutPath.StartsWith(TEXT("//?/")) == true)
		{
			return true;
		}
		if (OutPath.StartsWith(TEXT("//")) == true)
		{
			OutPath = TEXT("//?/UNC/") + OutPath.RightChop(2);
			return true;
		}
		if (OutPath.Len() >= 3 && FChar::IsAlpha(OutPath[0]) == true
			&& OutPath[1] == TEXT(':') && OutPath[2] == TEXT('/'))
		{
			OutPath = TEXT("//?/") + OutPath;
			return true;
		}
		return false;
	}

	TSharedPtr<FLoadedModule> FLoadedModule::Open(const FString& Path, FString& OutError)
	{
		// 同じDLLのFactoryへ別々のHostを設定しないよう、ロード中のモジュールを共有する。
		static TMap<FString, TWeakPtr<FLoadedModule>> Modules;
		const auto Key = FPaths::ConvertRelativePathToFull(Path).ToLower();
		if (const auto* Cached = Modules.Find(Key); Cached != nullptr)
		{
			if (const auto Existing = Cached->Pin(); Existing.IsValid() == true)
			{
				return Existing;
			}
		}

		auto Result = MakeShared<FLoadedModule>();
		Result->Host = Steinberg::owned(new Steinberg::Vst::HostApplication());
		FString SdkLoadPath;
		if (ToSdkLoadPath(Path, SdkLoadPath) == false)
		{
			OutError = TEXT("VST3モジュールの絶対パスが無効です。");
			return nullptr;
		}

		// SDKのモジュール名抽出に合わせ、長いパスも/区切りで渡す。
		std::string Error;
		Result->Module = ::VST3::Hosting::Module::create(TCHAR_TO_UTF8(*SdkLoadPath), Error);
		if (Result->Module == nullptr)
		{
			OutError = UTF8_TO_TCHAR(Error.c_str());
			return nullptr;
		}

		Result->Module->getFactory().setHostContext(Result->Host.get());
		Modules.Add(Key, Result);
		return Result;
	}

	FLoadedModule::~FLoadedModule()
	{
		if (this->Module != nullptr)
		{
			this->Module->getFactory().setHostContext(nullptr);
			this->Module.reset();
		}
	}

	FPluginInstance::FPluginInstance(const TSharedRef<FLoadedModule>& Module)
		: LoadedModule(Module)
	{
	}

	TUniquePtr<FPluginInstance> FPluginInstance::Create(
		const TSharedRef<FLoadedModule>& Module, const ::VST3::Hosting::ClassInfo& Info, FString& OutError)
	{
		auto Result = MakeUnique<FPluginInstance>(Module);
		Result->Provider = MakeUnique<FProvider>(Module->Module->getFactory(), Info);
		if (Result->Provider->Initialize(Module->Host.get()) == false)
		{
			OutError = TEXT("VST3 component/controllerを初期化できません。");
			return nullptr;
		}

		Result->Component = Result->Provider->getComponentPtr();
		Result->Controller = Result->Provider->getControllerPtr();
		Result->Processor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(Result->Component);
		if (Result->Component == nullptr || Result->Processor == nullptr)
		{
			OutError = TEXT("VST3 Audio Processorを取得できません。");
			return nullptr;
		}

		return Result;
	}

	FPluginInstance::~FPluginInstance()
	{
		if (this->bActive == true)
		{
			this->Component->setActive(false);
		}
		this->Controller = nullptr;
		this->Processor = nullptr;
		this->Component = nullptr;
		this->Provider.Reset();
	}

	bool FPluginInstance::ConfigureStereo(FString& OutError)
	{
		using namespace Steinberg;
		using namespace Steinberg::Vst;
		if (this->Component->getBusCount(kAudio, kInput) != 1 || this->Component->getBusCount(kAudio, kOutput) != 1)
		{
			OutError = TEXT("Audio Main Input/Outputが1本ずつのVST3に対応しています。");
			return false;
		}

		SpeakerArrangement Input = SpeakerArr::kStereo;
		SpeakerArrangement Output = SpeakerArr::kStereo;
		BusInfo InputInfo {};
		BusInfo OutputInfo {};
		if (this->Processor->canProcessSampleSize(kSample32) != kResultTrue
			|| this->Processor->setBusArrangements(&Input, 1, &Output, 1) != kResultOk
			|| this->Component->getBusInfo(kAudio, kInput, 0, InputInfo) != kResultOk
			|| this->Component->getBusInfo(kAudio, kOutput, 0, OutputInfo) != kResultOk
			|| InputInfo.busType != kMain || OutputInfo.busType != kMain
			|| InputInfo.channelCount != 2 || OutputInfo.channelCount != 2)
		{
			OutError = TEXT("VST3がステレオfloat32の入出力を受理しません。");
			return false;
		}
		return true;
	}

	bool FPluginInstance::RestoreState(const TArray<uint8>& ComponentState, const TArray<uint8>& ControllerState, FString& OutError)
	{
		using namespace Steinberg;
		if (ComponentState.IsEmpty() == false)
		{
			// MemoryStreamは書き込みも許すため、プリセットのデータをコピーして渡す。
			auto Bytes = ComponentState;
			MemoryStream Stream(Bytes.GetData(), Bytes.Num());
			if (this->Component->setState(&Stream) != kResultOk)
			{
				OutError = TEXT("VST3 component stateを復元できません。");
				return false;
			}

			Stream.seek(0, IBStream::kIBSeekSet, nullptr);
			if (this->Controller != nullptr)
			{
				const auto Result = this->Controller->setComponentState(&Stream);
				if (Result != kResultOk && Result != kNotImplemented)
				{
					OutError = TEXT("VST3 controllerへcomponent stateを同期できません。");
					return false;
				}
			}
		}

		if (ControllerState.IsEmpty() == false)
		{
			auto Bytes = ControllerState;
			MemoryStream Stream(Bytes.GetData(), Bytes.Num());
			if (this->Controller == nullptr || this->Controller->setState(&Stream) != kResultOk)
			{
				OutError = TEXT("VST3 controller stateを復元できません。");
				return false;
			}
		}
		return true;
	}

	bool FPluginInstance::Activate(const int32 SampleRate, const int32 MaximumBlockFrames, FString& OutError)
	{
		using namespace Steinberg;
		using namespace Steinberg::Vst;

		if (this->ConfigureStereo(OutError) == false)
		{
			return false;
		}

		for (const auto Direction : { kInput, kOutput })
		{
			if (this->Component->activateBus(kAudio, Direction, 0, true) != kResultOk)
			{
				OutError = TEXT("VST3 Main Audio Busを有効化できません。");
				return false;
			}
			for (auto Index = 0; Index < this->Component->getBusCount(kEvent, Direction); ++Index)
			{
				this->Component->activateBus(kEvent, Direction, Index, false);
			}
		}

		ProcessSetup Setup {};
		Setup.processMode = kRealtime;
		Setup.symbolicSampleSize = kSample32;
		Setup.maxSamplesPerBlock = MaximumBlockFrames;
		Setup.sampleRate = SampleRate;
		if (this->Processor->setupProcessing(Setup) != kResultOk || this->Component->setActive(true) != kResultOk)
		{
			OutError = TEXT("VST3 setupProcessing/setActiveに失敗しました。");
			return false;
		}

		this->bActive = true;
		return true;
	}
}
#endif

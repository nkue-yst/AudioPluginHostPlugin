// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"

#if AUDIOPLUGINHOST_WITH_VST3SDK
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

namespace AudioPluginHost::VST3
{
	FString ToClassId(const ::VST3::UID& Id);

	/** SDKのグローバルなPluginContextFactoryを使わず、初期化先を明示する。 */
	class FProvider final : public Steinberg::Vst::PlugProvider
	{
	public:
		FProvider(const PluginFactory& Factory, const ClassInfo& Info);
		bool Initialize(Steinberg::FUnknown* Host);
	};

	/** DLLより先にインスタンスを破棄し、HostはDLLの解放まで保持する。 */
	class FLoadedModule final
	{
	public:
		static TSharedPtr<FLoadedModule> Open(const FString& Path, FString& OutError);
		~FLoadedModule();

		Steinberg::IPtr<Steinberg::Vst::HostApplication> Host;
		::VST3::Hosting::Module::Ptr Module;
	};

	/** Component/Controllerの初期化・終了と、入出力の対応判定を共通化する。 */
	class FPluginInstance final
	{
	public:
		static TUniquePtr<FPluginInstance> Create(const TSharedRef<FLoadedModule>& Module, const ::VST3::Hosting::ClassInfo& Info, FString& OutError);
		explicit FPluginInstance(const TSharedRef<FLoadedModule>& Module);
		~FPluginInstance();

		bool ConfigureStereo(FString& OutError);
		bool RestoreState(const TArray<uint8>& ComponentState, const TArray<uint8>& ControllerState, FString& OutError);
		bool Activate(int32 SampleRate, int32 MaximumBlockFrames, FString& OutError);

		Steinberg::IPtr<Steinberg::Vst::IComponent> Component;
		Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> Processor;
		Steinberg::IPtr<Steinberg::Vst::IEditController> Controller;

	private:
		TSharedRef<FLoadedModule> LoadedModule;
		TUniquePtr<FProvider> Provider;
		bool bActive = false;
	};
}
#endif

// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3ModuleInspector.h"

#if AUDIOPLUGINHOST_WITH_VST3SDK
#include "VST3NativeView.h"
#include "VST3PluginInstance.h"
#endif

bool FVST3ModuleInspector::InspectModule(const FString& InBundlePath, TArray<FVST3ComponentDescriptor>& OutComponents, FString& OutError)
{
	OutComponents.Reset();
	OutError.Reset();
	if (IsInGameThread() == false || InBundlePath.IsEmpty() == true)
	{
		OutError = TEXT("ゲームスレッドからVST3バンドルを指定してください。");
		return false;
	}
#if AUDIOPLUGINHOST_WITH_VST3SDK
	using namespace AudioPluginHost::VST3;
	const auto Module = FLoadedModule::Open(InBundlePath, OutError);
	if (Module.IsValid() == false)
	{
		return false;
	}

	for (const auto& Info : Module->Module->getFactory().classInfos())
	{
		if (Info.category() != kVstAudioEffectClass)
		{
			continue;
		}

		FVST3ComponentDescriptor Descriptor;
		Descriptor.ClassId = ToClassId(Info.ID());
		Descriptor.Name = UTF8_TO_TCHAR(Info.name().c_str());
		Descriptor.Vendor = UTF8_TO_TCHAR(Info.vendor().c_str());
		Descriptor.Version = UTF8_TO_TCHAR(Info.version().c_str());
		Descriptor.Category = UTF8_TO_TCHAR(Info.category().c_str());
		Descriptor.bIsAudioEffect = true;

		FString Error;
		const auto Instance = FPluginInstance::Create(Module.ToSharedRef(), Info, Error);
		if (Instance != nullptr && Instance->ConfigureStereo(Error) == true)
		{
			Descriptor.bSupportsFloat32 = true;
			Descriptor.MainInputChannels = 2;
			Descriptor.MainOutputChannels = 2;
			Descriptor.bHasEditor = FNativeView::Create(Instance->Controller.get()) != nullptr;
		}
		OutComponents.Add(MoveTemp(Descriptor));
	}

	if (OutComponents.IsEmpty() == true)
	{
		OutError = TEXT("VST3 factoryにAudio Effectがありません。");
		return false;
	}
	return true;
#else
	OutError = TEXT("VST3 SDKがありません。SetupVST3SDK.ps1を実行してください。");
	return false;
#endif
}

// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "VST3Processor.h"

#if AUDIOPLUGINHOST_WITH_VST3SDK
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace AudioPluginHost::VST3
{
	class FNativeView;

	class FPlugFrame final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::IPlugFrame>>
	{
	public:
		FNativeView* Owner = nullptr;
		virtual Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* View, Steinberg::ViewRect* Size) override;
	};

	class FNativeView final : public IVST3EditorController
	{
	public:
		static TUniquePtr<FNativeView> Create(Steinberg::Vst::IEditController* Controller);
		virtual ~FNativeView() override;

		virtual bool Attach(void* ParentWindowHandle, FString& OutError) override;
		virtual void Detach() override;
		virtual FIntPoint GetSize() const override;
		virtual bool CanResize() const override;
		virtual bool Resize(FIntPoint Size) override;
		virtual void SetFocused(bool bFocused) override;
		virtual void SetResizeCallback(TFunction<void(FIntPoint)> Callback) override;
		bool ResizeFromPlugin(Steinberg::IPlugView* View, Steinberg::ViewRect* Size);

	private:
		Steinberg::IPtr<Steinberg::IPlugView> View;
		Steinberg::IPtr<FPlugFrame> Frame;

		TFunction<void(FIntPoint)> ResizeCallback;

		bool bAttached = false;
		bool bResizing = false;
	};
}
#endif

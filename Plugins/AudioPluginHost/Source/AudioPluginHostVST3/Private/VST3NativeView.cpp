// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3NativeView.h"

#if AUDIOPLUGINHOST_WITH_VST3SDK
namespace AudioPluginHost::VST3
{
	TUniquePtr<FNativeView> FNativeView::Create(Steinberg::Vst::IEditController* Controller)
	{
		if (Controller == nullptr)
		{
			return nullptr;
		}

		auto Result = MakeUnique<FNativeView>();
		Result->View = Steinberg::owned(Controller->createView(Steinberg::Vst::ViewType::kEditor));
		if (Result->View == nullptr || Result->View->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue)
		{
			return nullptr;
		}
		return Result;
	}

	FNativeView::~FNativeView()
	{
		this->Detach();
	}

	bool FNativeView::Attach(void* ParentWindowHandle, FString& OutError)
	{
		if (this->bAttached == true || ParentWindowHandle == nullptr)
		{
			OutError = TEXT("VST3ビューは接続済みか、親ウィンドウが無効です。");
			return false;
		}

		this->Frame = Steinberg::owned(new FPlugFrame());
		this->Frame->Owner = this;
		this->View->setFrame(this->Frame.get());
		if (this->View->attached(ParentWindowHandle, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk)
		{
			this->Frame->Owner = nullptr;
			this->View->setFrame(nullptr);
			this->Frame = nullptr;
			OutError = TEXT("VST3ネイティブビューを接続できません。");
			return false;
		}

		this->bAttached = true;
		return true;
	}

	void FNativeView::Detach()
	{
		if (this->Frame != nullptr)
		{
			this->Frame->Owner = nullptr;
		}

		if (this->bAttached == true)
		{
			this->View->removed();
			this->bAttached = false;
		}

		if (this->View != nullptr)
		{
			this->View->setFrame(nullptr);
		}
		this->Frame = nullptr;
		this->ResizeCallback = nullptr;
	}

	FIntPoint FNativeView::GetSize() const
	{
		Steinberg::ViewRect Rect {};
		if (this->View->getSize(&Rect) != Steinberg::kResultOk || Rect.getWidth() <= 0 || Rect.getHeight() <= 0)
		{
			return FIntPoint(640, 480);
		}
		return FIntPoint(Rect.getWidth(), Rect.getHeight());
	}

	bool FNativeView::CanResize() const
	{
		return this->View->canResize() == Steinberg::kResultTrue;
	}

	bool FNativeView::Resize(const FIntPoint Size)
	{
		if (Size.X <= 0 || Size.Y <= 0 || this->bResizing == true || this->CanResize() == false)
		{
			return false;
		}

		Steinberg::ViewRect Rect(0, 0, Size.X, Size.Y);
		this->View->checkSizeConstraint(&Rect);
		return this->ResizeFromPlugin(this->View.get(), &Rect);
	}

	void FNativeView::SetFocused(const bool bFocused)
	{
		if (this->bAttached == true)
		{
			this->View->onFocus(bFocused);
		}
	}

	void FNativeView::SetResizeCallback(TFunction<void(FIntPoint)> Callback)
	{
		this->ResizeCallback = MoveTemp(Callback);
	}

	bool FNativeView::ResizeFromPlugin(Steinberg::IPlugView* InView, Steinberg::ViewRect* Size)
	{
		if (InView != this->View.get() || Size == nullptr || Size->getWidth() <= 0 || Size->getHeight() <= 0 || this->bResizing == true)
		{
			return false;
		}

		TGuardValue<bool> Guard(this->bResizing, true);
		if (this->ResizeCallback)
		{
			this->ResizeCallback(FIntPoint(Size->getWidth(), Size->getHeight()));
		}
		return this->View->onSize(Size) == Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API FPlugFrame::resizeView(Steinberg::IPlugView* View, Steinberg::ViewRect* Size)
	{
		return this->Owner != nullptr && this->Owner->ResizeFromPlugin(View, Size) == true ? Steinberg::kResultOk : Steinberg::kResultFalse;
	}
}
#endif

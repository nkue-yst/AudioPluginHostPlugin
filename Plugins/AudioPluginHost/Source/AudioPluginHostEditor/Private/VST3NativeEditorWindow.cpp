// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3NativeEditorWindow.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/PlatformTime.h"
#include "Misc/MessageDialog.h"
#include "VST3SubmixEffectPreset.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogVST3Window, Log, All);

FVST3NativeEditorWindow::FVST3NativeEditorWindow(const TSharedRef<IVST3EditorSession>& InSession)
	: Session(InSession)
	, ConfigurationRevision(InSession->GetConfigurationRevision())
{
}

FVST3NativeEditorWindow::~FVST3NativeEditorWindow()
{
	this->Close(false);
}

IVST3EditorController* FVST3NativeEditorWindow::GetView() const
{
	return this->Session->IsStoppedEditor() == true || this->Session->GetConfigurationRevision() == this->ConfigurationRevision
		? this->Session->GetEditor() : nullptr;
}

bool FVST3NativeEditorWindow::IsForSession(const FVST3AudioSession& Candidate) const
{
	return this->Session->GetLiveSession() == &Candidate && this->bFinished == false;
}

bool FVST3NativeEditorWindow::IsForStoppedPreset(const UVST3SubmixEffectPreset& Candidate) const
{
	return this->Session->IsStoppedEditor() == true && this->Session->GetPreset() == &Candidate && this->bFinished == false;
}

bool FVST3NativeEditorWindow::IsForPreset(const UVST3SubmixEffectPreset& Candidate) const
{
	return this->Session->GetPreset() == &Candidate && this->bFinished == false;
}

bool FVST3NativeEditorWindow::IsFinished() const
{
	return this->bFinished;
}

bool FVST3NativeEditorWindow::IsStoppedEditor() const
{
	return this->Session->IsStoppedEditor();
}

bool FVST3NativeEditorWindow::PrepareForPIE(FString& OutError)
{
	if (this->Session->IsStoppedEditor() == false || this->bFinished == true)
	{
		return true;
	}
	if (this->Session->CaptureState(OutError) != EVST3StateCaptureResult::Succeeded)
	{
		return false;
	}
	this->Close(false);
	return true;
}

bool FVST3NativeEditorWindow::TryCaptureForSave(FString& OutError)
{
	if (this->bFinished == true)
	{
		return true;
	}
	const auto Result = this->Session->CaptureState(OutError);
	if (Result == EVST3StateCaptureResult::Pending)
	{
		this->Session->CancelStateCapture();
		OutError = TEXT("再生中のVST3 GUIを閉じ、状態保存の完了後に再試行してください。");
		return false;
	}
	return Result == EVST3StateCaptureResult::Succeeded;
}

bool FVST3NativeEditorWindow::Show(FString& OutError)
{
	if (this->bSaving == true)
	{
		OutError = TEXT("VST3の状態を保存中です。");
		return false;
	}

	if (this->Window.IsValid() == true)
	{
		this->Window->BringToFront(true);
		return true;
	}

	auto* View = this->GetView();
	if (View == nullptr)
	{
		OutError = TEXT("このVST3セッションにはWindowsネイティブGUIがありません。");
		return false;
	}

	const auto Size = View->GetSize();
	this->Window = SNew(SWindow)
		.Title(FText::FromString(this->Session->GetWindowTitle()))
		.ClientSize(FVector2D(Size.X, Size.Y))
		.UseOSWindowBorder(true)
		.SupportsMaximize(View->CanResize())
		.SizingRule(View->CanResize() == true ? ESizingRule::UserSized : ESizingRule::FixedSize);

	this->Window->SetOnWindowClosed(FOnWindowClosed::CreateSP(this, &FVST3NativeEditorWindow::HandleClosed));
	this->Window->GetOnWindowActivatedEvent().AddSP(this, &FVST3NativeEditorWindow::HandleActivated);
	this->Window->GetOnWindowDeactivatedEvent().AddSP(this, &FVST3NativeEditorWindow::HandleDeactivated);
	FSlateApplication::Get().AddWindow(this->Window.ToSharedRef(), true);

	const TWeakPtr<FVST3NativeEditorWindow> WeakSelf = this->AsShared();
	View->SetResizeCallback([WeakSelf](const FIntPoint NewSize)
	{
		if (const auto Self = WeakSelf.Pin(); Self.IsValid() == true)
		{
			Self->ResizeParent(NewSize);
		}
	});

	const auto NativeWindow = this->Window->GetNativeWindow();
	if (NativeWindow.IsValid() == false || View->Attach(NativeWindow->GetOSWindowHandle(), OutError) == false)
	{
		View->SetResizeCallback(nullptr);
		this->Close(false);
		return false;
	}

	this->bAttached = true;
	this->ResizeParent(View->GetSize());
	return true;
}

void FVST3NativeEditorWindow::ResizeParent(const FIntPoint Size)
{
	if (this->Window.IsValid() == true)
	{
		// 両APIとも物理ピクセルを使うため、DPIは再度掛けない。
		this->Window->Resize(FVector2D(Size.X, Size.Y));
		this->LastSize = Size;
	}
}

void FVST3NativeEditorWindow::DetachView()
{
	if (this->bAttached == true)
	{
		if (auto* View = this->GetView(); View != nullptr)
		{
			View->Detach();
		}
		this->bAttached = false;
	}
}

void FVST3NativeEditorWindow::Close(const bool bSaveState)
{
	this->bSaveOnClose = bSaveState;
	this->DetachView();

	if (bSaveState == false)
	{
		this->Session->CancelStateCapture();
		this->bSaving = false;
		this->bFinished = true;
	}

	if (this->Window.IsValid() == true)
	{
		// モジュール終了時にもSDKビューを先に外し、残ったSlate callbackを無効化する。
		const auto ClosingWindow = this->Window;
		if (bSaveState == false)
		{
			ClosingWindow->SetOnWindowClosed(FOnWindowClosed());
			ClosingWindow->GetOnWindowActivatedEvent().RemoveAll(this);
			ClosingWindow->GetOnWindowDeactivatedEvent().RemoveAll(this);
			this->Window.Reset();
		}
		ClosingWindow->RequestDestroyWindow();
	}
}

void FVST3NativeEditorWindow::HandleClosed(const TSharedRef<SWindow>& ClosedWindow)
{
	this->DetachView();
	this->Window.Reset();
	this->bSaving = this->bSaveOnClose;
	this->bFinished = this->bSaveOnClose == false;
	this->CaptureStartedAt = FPlatformTime::Seconds();
}

void FVST3NativeEditorWindow::HandleActivated()
{
	if (auto* View = this->GetView(); View != nullptr && this->bAttached == true)
	{
		View->SetFocused(true);
	}
}

void FVST3NativeEditorWindow::HandleDeactivated()
{
	if (auto* View = this->GetView(); View != nullptr && this->bAttached == true)
	{
		View->SetFocused(false);
	}
}

bool FVST3NativeEditorWindow::Tick()
{
	if (this->bFinished == true)
	{
		return false;
	}
	if (this->GetView() == nullptr)
	{
		UE_LOG(LogVST3Window, Warning, TEXT("VST3 GUIの編集対象が変更されたため、このウィンドウを閉じます。"));
		this->Close(false);
		return false;
	}

	if (this->bSaving == true)
	{
		FString Error;
		auto Result = this->Session->CaptureState(Error);
		if (Result == EVST3StateCaptureResult::Pending && FPlatformTime::Seconds() - this->CaptureStartedAt >= 2.0)
		{
			Result = EVST3StateCaptureResult::Failed;
			Error = TEXT("VST3状態保存が音声処理の応答待ちでタイムアウトしました。");
		}

		if (Result != EVST3StateCaptureResult::Pending)
		{
			if (Result == EVST3StateCaptureResult::Failed)
			{
				UE_LOG(LogVST3Window, Warning, TEXT("%s"), *Error);
				if (this->Session->IsStoppedEditor() == true)
				{
					this->bSaving = false;
					const auto Choice = FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::Yes,
						FText::FromString(FString::Printf(TEXT("VST3の編集状態を保存できません。\n%s\n\nはい: 編集を続ける\nいいえ: 未保存のネイティブGUI状態を破棄する"), *Error)),
						FText::FromString(TEXT("VST3 GUIの状態保存に失敗")));
					if (Choice == EAppReturnType::Yes)
					{
						FString ReopenError;
						if (this->Show(ReopenError) == false)
						{
							FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
								FString::Printf(TEXT("GUIを再表示できません。VSTメニューから再度開いてください。%s"), *ReopenError)));
						}
						return true;
					}
					this->Close(false);
					return false;
				}
			}
			this->Session->CancelStateCapture();
			this->bSaving = false;
			this->bFinished = true;
			return false;
		}
	}
	else if (this->Window.IsValid() == true && this->bAttached == true)
	{
		const auto Size = this->Window->GetClientSizeInScreen();
		const FIntPoint Pixels(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
		if (Pixels != this->LastSize)
		{
			this->LastSize = Pixels;
			this->GetView()->Resize(Pixels);
		}
	}
	return true;
}

// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "SVST3Mixer.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AudioPluginHostEditorModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "Sound/SoundSubmix.h"
#include "Sound/AudioSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "VST3MixerModel.h"
#include "VST3MixerAsset.h"
#include "VST3ModuleAsset.h"
#include "VST3SubmixEffectPreset.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace VST3MixerStyle
{
	bool IsEditableBus(const USoundSubmix* Bus)
	{
		return IsValid(Bus) == true && Bus->GetOutermost()->GetName().StartsWith(TEXT("/Game/"));
	}

	const FLinearColor Background(0.018f, 0.022f, 0.027f);
	const FLinearColor Panel(0.032f, 0.039f, 0.047f);
	const FLinearColor Slot(0.052f, 0.062f, 0.073f);
	const FLinearColor Accent(0.18f, 0.76f, 0.64f);
	const FLinearColor Muted(0.48f, 0.54f, 0.60f);

	TSharedRef<STextBlock> Label(const FString& Text, const int32 Size = 10)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size));
	}

	void Browse(UObject* Object)
	{
		if (Object != nullptr)
		{
			auto& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			Browser.Get().SyncBrowserToAssets(TArray<FAssetData>{FAssetData(Object)});
		}
	}
} // namespace VST3MixerStyle

SVST3Mixer::SVST3Mixer() = default;

void SVST3Mixer::Construct(const FArguments& InArgs)
{
	this->Model = MakeShared<FVST3MixerModel>(InArgs._MixerAsset);
	if (GEditor != nullptr)
	{
		GEditor->RegisterForUndo(this);
	}
	this->ObjectChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SVST3Mixer::HandleObjectChanged);
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	this->AssetAddedHandle = Registry.OnAssetAdded().AddSP(this, &SVST3Mixer::HandleAssetChanged);
	this->AssetRemovedHandle = Registry.OnAssetRemoved().AddSP(this, &SVST3Mixer::HandleAssetChanged);
	this->AssetRenamedHandle = Registry.OnAssetRenamed().AddSP(this, &SVST3Mixer::HandleAssetRenamed);

	this->ChildSlot
	[SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(VST3MixerStyle::Background)
		.Padding(12)
		[SNew(SVerticalBox) +
			SVerticalBox::Slot().AutoHeight().Padding(4, 4, 4, 16)
			[SNew(SHorizontalBox) +
				SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(this->Model->GetMixer() != nullptr ? this->Model->GetMixer()->GetName() : TEXT("VST Mixer")); })
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))] +
				SHorizontalBox::Slot().FillWidth(1).Padding(18, 0).VAlign(
				VAlign_Center)[SNew(STextBlock)
					.ColorAndOpacity(VST3MixerStyle::Accent)
					.Text_Lambda(
					[this]()
					{
						return GEditor == nullptr || GEditor->PlayWorld == nullptr
						? FText::FromString(TEXT("STOPPED  /  Preset editing"))
						: FText::FromString(TEXT("PIE  /  Live audio"));
					})] +
				SHorizontalBox::Slot().AutoWidth().Padding(
				3, 0)[SNew(SButton)
					.Text(FText::FromString(TEXT("Refresh")))
					.ToolTipText(FText::FromString(TEXT("Content Browserなどで変更したアセットを再取得します。")))
					.OnClicked_Lambda(
					[this]()
					{
						this->Refresh();
						return FReply::Handled();
					})] +
				SHorizontalBox::Slot().AutoWidth().Padding(
				3, 0)[SNew(SButton)
					.Text(FText::FromString(TEXT("Save mixer")))
					.OnClicked_Lambda(
					[this]()
					{
						FText Error;
						this->SaveMixer(Error);
						return FReply::Handled();
					})]] +
			SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 12)[SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FLinearColor(1.0f, 0.58f, 0.24f))
				.Visibility_Lambda([this]() { FText Error; return this->Model->ValidateEditScope(Error) ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { FText Error; this->Model->ValidateEditScope(Error); return Error; })] +
			SVerticalBox::Slot().FillHeight(
			1)[SNew(SSplitter) +
				SSplitter::Slot().Value(0.17f)
				[SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(VST3MixerStyle::Panel)
					.Padding(
					12)[SNew(SVerticalBox) +
						SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[VST3MixerStyle::Label(TEXT("BUSES"), 12)] +
						SVerticalBox::Slot().FillHeight(
						1)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(this->Navigation, SVerticalBox)]] +
						SVerticalBox::Slot().AutoHeight().Padding(
						0, 12, 0, 6)[SAssignNew(this->BusName, SEditableTextBox)
							.Text(FText::FromString(TEXT("NewBus")))
							.HintText(FText::FromString(TEXT("Bus name")))
							.IsEnabled(this, &SVST3Mixer::CanEditStructure)] +
						SVerticalBox::Slot().AutoHeight()[SNew(SButton)
							.Text_Lambda([this]() { return FText::FromString(this->Model->GetRootBus() == nullptr ? TEXT("+ Create root bus") : TEXT("+ Create bus")); })
							.IsEnabled(this, &SVST3Mixer::CanEditStructure)
							.OnClicked_Lambda(
							[this]()
							{
								USoundSubmix* NewBus = nullptr;
								FText Error;
								const auto Name = this->BusName->GetText().ToString();
								const auto bCreated = this->Model->GetRootBus() == nullptr
									? this->Model->CreateRootBus(Name, NewBus, Error)
									: this->Model->CreateBus(Name, this->Model->GetRootBus(), NewBus, Error);
								this->Report(bCreated, Error);
								if (bCreated == true)
								{
									this->SelectedBus = NewBus;
									this->Refresh();
								}
								return FReply::Handled();
							})] +
						SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 5)[VST3MixerStyle::Label(TEXT("ADD EXISTING SUBMIX"), 9)] +
						SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox)
							.AllowedClass(USoundSubmix::StaticClass())
							.IsEnabled(this, &SVST3Mixer::CanEditStructure)
							.OnObjectChanged_Lambda([this](const FAssetData& Asset)
							{
								auto* Bus = Cast<USoundSubmix>(Asset.GetAsset());
								if (Bus == nullptr)
								{
									return;
								}
								FText Error;
								const auto bAdopted = this->Model->AdoptExistingBus(Bus, Error);
								this->Report(bAdopted, Error);
								if (bAdopted == true)
								{
									this->SelectedBus = Bus;
									this->Refresh();
								}
							})] +
						SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[SNew(SButton)
							.Text(FText::FromString(TEXT("Make independent")))
							.Visibility_Lambda([this]() { return this->Model->HasSharedBuses() ? EVisibility::Visible : EVisibility::Collapsed; })
							.IsEnabled_Lambda([this]() { return GEditor != nullptr && GEditor->PlayWorld == nullptr && this->Model->GetRootBus() != nullptr; })
							.ToolTipText(FText::FromString(TEXT("このミキサーのバスと挿入プリセットを複製し、参照を置き換えます。音源の出力先とProject Settingsは変更しません。")))
							.OnClicked_Lambda([this]()
							{
								FText Error;
								const auto bCopied = this->Model->MakeIndependent(Error);
								this->Report(bCopied, Error, FText::FromString(TEXT("バスとプリセットを独立させました。音源の出力先は必要に応じて割り当て直してください。")));
								if (bCopied == true)
								{
									this->SelectedBus.Reset();
									this->Refresh();
								}
								return FReply::Handled();
							})] +
						SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SButton)
							.Text(FText::FromString(TEXT("Reset bus list…")))
							.Visibility_Lambda([this]() { FText Error; return this->Model->ValidateEditScope(Error) ? EVisibility::Collapsed : EVisibility::Visible; })
							.IsEnabled_Lambda([]() { return GEditor != nullptr && GEditor->PlayWorld == nullptr; })
							.OnClicked_Lambda([this]()
							{
								const auto Answer = FMessageDialog::Open(EAppMsgType::YesNo,
									FText::FromString(TEXT("このミキサーのバス登録を空にします。\nSubmix・プリセット・音源の出力先は変更しません。\n必要なバスはADD EXISTING SUBMIXから登録し直せます。続行しますか？")));
								if (Answer == EAppReturnType::Yes)
								{
									FText Error;
									this->Report(this->Model->ResetBusReferences(Error), Error);
									this->Refresh();
								}
								return FReply::Handled();
							})]]] +
				SSplitter::Slot().Value(
				0.58f)[SNew(SVerticalBox) +
					SVerticalBox::Slot().AutoHeight().Padding(
					12, 4, 8, 12)[SNew(STextBlock)
						.Text(FText::FromString(TEXT("INSERTS  →  OUTPUT LEVEL  →  PARENT BUS")))
						.ColorAndOpacity(VST3MixerStyle::Muted)] +
					SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox).Orientation(Orient_Horizontal) +
						SScrollBox::Slot()[SAssignNew(this->Channels, SHorizontalBox)]]] +
				SSplitter::Slot().Value(
				0.25f)[SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(VST3MixerStyle::Panel)
					.Padding(14)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(this->Inspector, SVerticalBox)]]]] +
			SVerticalBox::Slot().AutoHeight().Padding(4, 12, 4, 2)
			[SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{ return this->Status.IsEmpty() == false ? this->Status : this->GetRouteLabel(this->SelectedBus); })
				.ColorAndOpacity_Lambda(
				[this]()
				{
					return this->bStatusIsError ? FSlateColor(FLinearColor(1.0f, 0.38f, 0.28f))
					: FSlateColor(VST3MixerStyle::Muted);
				})]]];
	this->Refresh();
}

SVST3Mixer::~SVST3Mixer()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(this->ObjectChangedHandle);
	if (auto* RegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")); RegistryModule != nullptr)
	{
		auto& Registry = RegistryModule->Get();
		Registry.OnAssetAdded().Remove(this->AssetAddedHandle);
		Registry.OnAssetRemoved().Remove(this->AssetRemovedHandle);
		Registry.OnAssetRenamed().Remove(this->AssetRenamedHandle);
	}
	this->Model->EndVolumeEdit(this->VolumeBus.Get());
	if (GEditor != nullptr)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SVST3Mixer::RequestRefresh()
{
	if (this->bRefreshRequested == false)
	{
		this->bRefreshRequested = true;
		this->RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SVST3Mixer::RefreshDeferred));
	}
}

EActiveTimerReturnType SVST3Mixer::RefreshDeferred(const double CurrentTime, const float DeltaTime)
{
	// フェーダーを操作している間はウィジェットとUndo transactionを維持する。
	if (this->VolumeBus.IsValid() == true)
	{
		return EActiveTimerReturnType::Continue;
	}
	this->bRefreshRequested = false;
	this->Refresh();
	return EActiveTimerReturnType::Stop;
}

void SVST3Mixer::HandleObjectChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object != nullptr
	&& (Object->IsA<USoundSubmix>() == true || Object->IsA<UVST3SubmixEffectPreset>() == true
	|| Object->IsA<UVST3ModuleAsset>() == true || Object->IsA<UVST3MixerAsset>() == true))
	{
		this->RequestRefresh();
	}
}

void SVST3Mixer::HandleAssetChanged(const FAssetData& Asset)
{
	if (Asset.AssetClassPath == UVST3MixerAsset::StaticClass()->GetClassPathName()
	|| Asset.AssetClassPath == USoundSubmix::StaticClass()->GetClassPathName()
	|| Asset.AssetClassPath == UVST3ModuleAsset::StaticClass()->GetClassPathName()
	|| Asset.AssetClassPath == UVST3SubmixEffectPreset::StaticClass()->GetClassPathName())
	{
		this->RequestRefresh();
	}
}

void SVST3Mixer::HandleAssetRenamed(const FAssetData& Asset, const FString& OldPath)
{
	this->HandleAssetChanged(Asset);
}

bool SVST3Mixer::CanEditStructure() const
{
	return this->Model.IsValid() == true && this->Model->CanEditStructure();
}

bool SVST3Mixer::CanEditBus(const TWeakObjectPtr<USoundSubmix> Bus) const
{
	FText Error;
	return VST3MixerStyle::IsEditableBus(Bus.Get()) && this->Model->ValidateEditScope(Error)
		&& this->Model->GetBuses().Contains(Bus);
}

bool SVST3Mixer::SaveMixer(FText& OutError)
{
	const auto bSaved = this->Model->SaveMixerAndDependencies(OutError);
	this->Report(bSaved, OutError, FText::FromString(TEXT("ミキサーと関連するバス・プリセットを保存しました。")));
	return bSaved;
}

bool SVST3Mixer::PrepareToSave(FText& OutError)
{
	return this->Model->PrepareToSave(OutError);
}

void SVST3Mixer::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	this->Model->GetSaveableObjects(OutObjects);
}

void SVST3Mixer::PostUndo(const bool bSuccess)
{
	if (bSuccess == true)
	{
		// UEの通常Undoでは、Submix音量が音声デバイスへ再送されない。
		auto* VolumeProperty = FindFProperty<FProperty>(USoundSubmix::StaticClass(),
			GET_MEMBER_NAME_CHECKED(USoundSubmix, OutputVolumeModulation));
		for (const auto& Bus : this->Model->GetBuses())
		{
			if (Bus.IsValid() == true)
			{
				FPropertyChangedEvent Event(VolumeProperty, EPropertyChangeType::ValueSet);
				static_cast<UObject*>(Bus.Get())->PostEditChangeProperty(Event);
			}
		}
		this->Refresh();
	}
}

void SVST3Mixer::PostRedo(const bool bSuccess)
{
	this->PostUndo(bSuccess);
}

void SVST3Mixer::Report(const bool bSuccess, const FText& Error, const FText& Success)
{
	this->bStatusIsError = bSuccess == false;
	this->Status = bSuccess == true ? Success : Error;
}

void SVST3Mixer::SelectBus(const TWeakObjectPtr<USoundSubmix> Bus)
{
	this->Model->EndVolumeEdit(this->VolumeBus.Get());
	this->VolumeBus.Reset();
	this->SelectedBus = Bus;
	this->SelectedPreset.Reset();
	this->Status = FText::GetEmpty();
	this->bStatusIsError = false;
	this->RebuildInspector();
}

void SVST3Mixer::Refresh()
{
	this->Model->EndVolumeEdit(this->VolumeBus.Get());
	this->VolumeBus.Reset();
	this->Model->Refresh();
	if (this->Model->GetBuses().Contains(this->SelectedBus) == false)
	{
		this->SelectedBus.Reset();
		this->SelectedPreset.Reset();
	}
	if (this->SelectedBus.IsValid() == false && this->Model->GetBuses().Num() > 0)
	{
		this->SelectedBus = this->Model->GetBuses()[0];
	}
	if (this->SelectedPreset.IsValid() == true
		&& (this->SelectedBus.IsValid() == false || this->SelectedBus->SubmixEffectChain.Contains(this->SelectedPreset.Get()) == false))
	{
		this->SelectedPreset.Reset();
	}
	FText ScopeError;
	if (this->Model->ValidateEditScope(ScopeError) == false)
	{
		this->Report(false, ScopeError);
	}
	this->RebuildChannels();
	this->RebuildInspector();
}

FText SVST3Mixer::GetOutputLabel(const TWeakObjectPtr<USoundSubmix> Bus) const
{
	auto* Current = Bus.Get();
	if (Current == nullptr)
	{
		return FText::GetEmpty();
	}
	if (Current == this->Model->GetMasterBus())
	{
		return FText::FromString(TEXT("Hardware output"));
	}
	if (Current->ParentSubmix != nullptr)
	{
		return FText::FromString(Current->ParentSubmix->GetName());
	}
	if (Current->IsDynamic(true) == true)
	{
		return FText::FromString(TEXT("Not connected (dynamic)"));
	}
	const auto* BaseDefault = GetDefault<UAudioSettings>()->BaseDefaultSubmix.TryLoad();
	return FText::FromString(BaseDefault != nullptr && BaseDefault != Current
	? TEXT("Auto → ") + BaseDefault->GetName() : TEXT("Auto → Master"));
}

FText SVST3Mixer::GetRouteLabel(const TWeakObjectPtr<USoundSubmix> Bus) const
{
	const auto* Current = Bus.Get();
	if (Current == nullptr)
	{
		return FText::FromString(TEXT("バスを作成するか、既存のSubmixを選択してください。"));
	}
	FString Route;
	TSet<const USoundSubmixBase*> Visited;
	USoundSubmixBase* Node = Bus.Get();
	auto* BaseDefault = Cast<USoundSubmixBase>(GetDefault<UAudioSettings>()->BaseDefaultSubmix.TryLoad());
	while (Node != nullptr && Visited.Contains(Node) == false)
	{
		Visited.Add(Node);
		Route += Route.IsEmpty() ? Node->GetName() : TEXT("  →  ") + Node->GetName();
		if (Node == this->Model->GetMasterBus())
		{
			return FText::FromString(Route + TEXT("  →  Hardware output"));
		}
		auto* WithParent = Cast<USoundSubmixWithParentBase>(Node);
		if (WithParent != nullptr && WithParent->ParentSubmix != nullptr)
		{
			Node = WithParent->ParentSubmix.Get();
		}
		else if (WithParent != nullptr && WithParent->IsDynamic(true) == false)
		{
			Node = BaseDefault != nullptr && Node != BaseDefault ? BaseDefault : this->Model->GetMasterBus();
		}
		else
		{
			return FText::FromString(Route + TEXT("  →  [No automatic connection]"));
		}
	}
	return FText::FromString(Route + (Node != nullptr ? TEXT("  →  [Routing cycle]") : TEXT("  →  UE default output")));
}

void SVST3Mixer::RebuildChannels()
{
	this->Navigation->ClearChildren();
	this->Channels->ClearChildren();
	for (const auto WeakBus : this->Model->GetBuses())
	{
		if (WeakBus.IsValid() == false)
		{
			continue;
		}
		this->Navigation->AddSlot().AutoHeight().Padding(0, 0, 0, 5)[SNew(SButton)
			.ContentPadding(8)
			.ButtonColorAndOpacity_Lambda(
			[this, WeakBus]() { return this->SelectedBus == WeakBus ? VST3MixerStyle::Accent * 0.45f : VST3MixerStyle::Slot; })
			.OnClicked_Lambda(
			[this, WeakBus]()
			{
				this->SelectBus(WeakBus);
				return FReply::Handled();
			})[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(WeakBus->GetName()))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SNew(STextBlock)
					.Text_Lambda([this, WeakBus]() { return this->GetOutputLabel(WeakBus); })
					.ColorAndOpacity(VST3MixerStyle::Muted)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))]]];
		this->Channels->AddSlot().AutoWidth().Padding(6, 0)[this->MakeChannel(WeakBus)];
	}
	if (this->Model->GetBuses().IsEmpty())
	{
		this->Channels->AddSlot().AutoWidth().Padding(24)[VST3MixerStyle::Label(TEXT("Create a bus to add VST3 inserts."), 13)];
	}
}

TSharedRef<SWidget> SVST3Mixer::MakeChannel(const TWeakObjectPtr<USoundSubmix> Bus)
{
	auto Rack = SNew(SVerticalBox);
	for (auto Index = 0; Bus.IsValid() == true && Index < Bus->SubmixEffectChain.Num(); ++Index)
	{
		Rack->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[this->MakeInsert(Bus, Index)];
	}
	if (Bus.IsValid() == true && Bus->SubmixEffectChain.IsEmpty())
	{
		Rack->AddSlot().AutoHeight().Padding(
		8, 20)[SNew(STextBlock).Text(FText::FromString(TEXT("No inserts"))).ColorAndOpacity(VST3MixerStyle::Muted)];
	}

	return SNew(SBox).WidthOverride(280).MinDesiredHeight(520)
	[SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this, Bus]()
		{ return this->SelectedBus == Bus ? FLinearColor(0.07f, 0.20f, 0.19f) : VST3MixerStyle::Panel; })
		.Padding(
		2)[SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(VST3MixerStyle::Panel)
			.Padding(12)[SNew(SVerticalBox) +
				SVerticalBox::Slot().AutoHeight()[SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(0, 6))
					.OnClicked_Lambda(
					[this, Bus]()
					{
						this->SelectBus(Bus);
						return FReply::Handled();
					})[SNew(STextBlock)
						.Text_Lambda(
						[Bus]()
						{
							return FText::FromString(
							Bus.IsValid()
							? Bus->GetName() +
							(Bus->GetOutermost()->IsDirty()
							? TEXT(" *")
							: TEXT(""))
							: TEXT("Missing bus"));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))]] +
				SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 12)
				[SNew(SComboButton)
					.IsEnabled_Lambda(
					[this, Bus]()
					{ return this->CanEditStructure() && this->CanEditBus(Bus) && Bus.Get() != this->Model->GetRootBus(); })
					.ToolTipText(FText::FromString(TEXT("このバスの出力先。自分自身や子孫には接続できません。")))
					.OnGetMenuContent_Lambda([this, Bus]() { return this->MakeOutputMenu(Bus); })
					.ButtonContent()[SNew(STextBlock)
						.Text_Lambda([this, Bus]() { return this->GetOutputLabel(Bus); })]] +
				SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[VST3MixerStyle::Label(TEXT("INSERTS"), 9)] +
				SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Rack]] +
				SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(SButton)
					.Text(FText::FromString(TEXT("+ Add VST3")))
					.IsEnabled_Lambda([this, Bus]() { return this->CanEditStructure() && this->CanEditBus(Bus); })
					.OnClicked_Lambda(
					[this, Bus]()
					{
						this->SelectBus(Bus);
						return FReply::Handled();
					})] +
				SVerticalBox::Slot().AutoHeight().Padding(0, 8)[VST3MixerStyle::Label(TEXT("OUTPUT  /  dB"), 9)] +
				SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).HeightOverride(150).WidthOverride(
					56)[SNew(SSlider)
						.IsEnabled_Lambda([this, Bus]() { return this->CanEditBus(Bus); })
						.Orientation(Orient_Vertical)
						.SliderHandleColor(VST3MixerStyle::Accent)
						.Value_Lambda(
						[Bus]()
						{ return Bus.IsValid() ? (Bus->OutputVolumeModulation.Value + 96.0f) / 96.0f : 0.0f; })
						.OnMouseCaptureBegin_Lambda(
						[this, Bus]()
						{
							FText Error;
							if (this->Model->BeginVolumeEdit(Bus.Get(), Error) == true)
							{
								this->VolumeBus = Bus;
							}
							else
							{
								this->Report(false, Error);
							}
						})
						.OnMouseCaptureEnd_Lambda(
						[this, Bus]()
						{
							this->Model->EndVolumeEdit(Bus.Get());
							this->VolumeBus.Reset();
						})
						.OnValueChanged_Lambda(
						[this, Bus](const float Value)
						{
							FText Error;
							this->Report(this->Model->SetVolumeDb(Bus.Get(), Value * 96.0f - 96.0f, Error),
							Error);
						})]] +
				SVerticalBox::Slot().AutoHeight().Padding(
				20, 8)[SNew(SNumericEntryBox<float>)
					.IsEnabled_Lambda([this, Bus]() { return this->CanEditBus(Bus); })
					.MinValue(-96.0f)
					.MaxValue(0.0f)
					.AllowSpin(false)
					.Value_Lambda(
					[Bus]() -> TOptional<float>
					{
						return Bus.IsValid() ? TOptional<float>(Bus->OutputVolumeModulation.Value)
						: TOptional<float>();
					})
					.OnValueCommitted_Lambda(
					[this, Bus](float Value, ETextCommit::Type)
					{
						FText Error;
						this->Report(this->Model->SetVolumeDb(Bus.Get(), Value, Error), Error);
					})]]]];
}

TSharedRef<SWidget> SVST3Mixer::MakeInsert(const TWeakObjectPtr<USoundSubmix> Bus, const int32 Index)
{
	const TWeakObjectPtr<UVST3SubmixEffectPreset> Preset = Cast<UVST3SubmixEffectPreset>(Bus->SubmixEffectChain[Index]);
	const auto* Effect = Bus->SubmixEffectChain[Index].Get();
	const auto Name = Effect != nullptr ? Effect->GetName() : TEXT("Empty slot");
	const auto* Module = Preset.IsValid() ? Preset->GetModuleAsset() : nullptr;
	const auto* Component = Module != nullptr ? Module->FindComponent(Preset->GetSelectedClassId()) : nullptr;
	const auto ComponentName = Component != nullptr ? Component->Name : (Preset.IsValid() ? TEXT("VST3") : TEXT("UE Submix Effect"));
	return SNew(SBorder)
	.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
	.BorderBackgroundColor(VST3MixerStyle::Slot)
	.Padding(
	8)[SNew(SVerticalBox) +
		SVerticalBox::Slot()
		.AutoHeight()[SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth().Padding(
			0, 0, 6, 0)[VST3MixerStyle::Label(FString::Printf(TEXT("%02d"), Index + 1), 10)] +
			SHorizontalBox::Slot().FillWidth(1)[SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(0)
				.OnClicked_Lambda(
				[this, Bus, Preset]()
				{
					this->SelectBus(Bus);
					this->SelectedPreset = Preset;
					this->RebuildInspector();
					return FReply::Handled();
				})[SNew(STextBlock)
					.Text(FText::FromString(Name))
					.ToolTipText(FText::FromString(Name))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]] +
		SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[SNew(STextBlock).Text(FText::FromString(ComponentName)).ToolTipText(FText::FromString(ComponentName))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(VST3MixerStyle::Muted)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)] +
		SVerticalBox::Slot().AutoHeight().Padding(
		0, 8, 0,
		0)[SNew(SHorizontalBox) +
			SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[SNew(SCheckBox)
				.IsEnabled_Lambda([this, Bus]() { return this->CanEditBus(Bus); })
				.Visibility(Preset.IsValid() ? EVisibility::Visible : EVisibility::Collapsed)
				.ToolTipText(FText::FromString(TEXT("チェックを外すと、このプリセットのVST3をバイパスします。")))
				.IsChecked_Lambda(
				[Preset]()
				{
					return Preset.IsValid() && Preset->IsHostBypassed() == false ? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda(
				[this, Preset](ECheckBoxState State)
				{
					FText Error;
					this->Report(this->Model->SetPresetBypassed(Preset.Get(), State != ECheckBoxState::Checked, Error),
					Error);
				})[VST3MixerStyle::Label(TEXT("On"), 9)]] +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
				.Text(FText::FromString(TEXT("GUI")))
				.IsEnabled_Lambda([this, Bus, Preset]() { return Preset.IsValid() && this->CanEditBus(Bus); })
				.OnClicked_Lambda(
				[this, Preset]()
				{
					this->OpenEditor(Preset);
					return FReply::Handled();
				})] +
			SHorizontalBox::Slot().AutoWidth().Padding(
			2, 0)[SNew(SButton)
				.Text(FText::FromString(TEXT("↑")))
				.IsEnabled_Lambda([this, Bus, Index]() { return this->CanEditStructure() && this->CanEditBus(Bus) && Index > 0; })
				.OnClicked_Lambda(
				[this, Bus, Index]()
				{
					FText Error;
					const auto bMoved = this->Model->MoveInsert(Bus.Get(), Index, Index - 1, Error);
					this->Report(bMoved, Error);
					if (bMoved == true)
					{
						this->Refresh();
					}
					return FReply::Handled();
				})] +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
				.Text(FText::FromString(TEXT("↓")))
				.IsEnabled_Lambda(
				[this, Bus, Index]()
				{
					return this->CanEditStructure() && this->CanEditBus(Bus) &&
					Index + 1 < Bus->SubmixEffectChain.Num();
				})
				.OnClicked_Lambda(
				[this, Bus, Index]()
				{
					FText Error;
					const auto bMoved =
					this->Model->MoveInsert(Bus.Get(), Index, Index + 1, Error);
					this->Report(bMoved, Error);
					if (bMoved == true)
					{
						this->Refresh();
					}
					return FReply::Handled();
				})] +
			SHorizontalBox::Slot().AutoWidth().Padding(
			2, 0, 0,
			0)[SNew(SButton)
				.Text(FText::FromString(TEXT("×")))
				.IsEnabled_Lambda([this, Bus]() { return this->CanEditStructure() && this->CanEditBus(Bus); })
				.ToolTipText(FText::FromString(TEXT("このスロットだけを外します。プリセットアセットは削除しません。")))
				.OnClicked_Lambda(
				[this, Bus, Index]()
				{
					FText Error;
					const auto bRemoved = this->Model->RemoveInsert(Bus.Get(), Index, Error);
					this->Report(bRemoved, Error);
					if (bRemoved == true)
					{
						this->Refresh();
					}
					return FReply::Handled();
				})]]];
}

TSharedRef<SWidget> SVST3Mixer::MakeOutputMenu(const TWeakObjectPtr<USoundSubmix> Bus)
{
	FMenuBuilder Menu(true, nullptr);
	auto AddTarget = [this, Bus, &Menu](const TWeakObjectPtr<USoundSubmix> Parent, const FText& Label)
	{
		Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda(
		[this, Bus, Parent]()
		{
			FText Error;
			const auto bChanged = this->Model->SetParentBus(Bus.Get(), Parent.Get(), Error);
			this->Report(bChanged, Error);
			if (bChanged == true)
			{
				this->Refresh();
			}
		})));
	};
	// 追加バスの接続先は、このミキサー内のバスに限定する。
	for (const auto Parent : this->Model->GetBuses())
	{
		if (VST3MixerStyle::IsEditableBus(Parent.Get()) == true && Parent != Bus)
		{
			AddTarget(Parent, FText::FromString(Parent->GetName()));
		}
	}
	return Menu.MakeWidget();
}

void SVST3Mixer::OpenEditor(const TWeakObjectPtr<UVST3SubmixEffectPreset> Preset)
{
	FText Error;
	auto& Module = FModuleManager::GetModuleChecked<FAudioPluginHostEditorModule>(TEXT("AudioPluginHostEditor"));
	this->Report(Module.OpenVST3EditorForPreset(Preset.Get(), Error), Error);
}

TSharedRef<SWidget> SVST3Mixer::MakeComponentMenu()
{
	FMenuBuilder Menu(true, nullptr);
	if (this->SelectedModule.IsValid() == true)
	{
		for (const auto& Component : this->SelectedModule->GetComponents())
		{
			if (Component.IsSupportedEffect() == false)
			{
				continue;
			}
			const auto ClassId = Component.ClassId;
			Menu.AddMenuEntry(FText::FromString(Component.Name), FText::FromString(Component.Vendor), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, ClassId]() { this->SelectedClassId = ClassId; })));
		}
	}
	return Menu.MakeWidget();
}

void SVST3Mixer::RebuildInspector()
{
	this->Inspector->ClearChildren();
	this->Inspector->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[VST3MixerStyle::Label(TEXT("BUS & INSERT"), 12)];
	if (this->SelectedBus.IsValid() == false)
	{
		this->Inspector->AddSlot().AutoHeight()[SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(TEXT("Create root busで最初のバスを作成するか、ADD EXISTING SUBMIXで既存のルートを選んでください。音声を通すには音源の出力先をバスへ割り当てます。")))];
		return;
	}
	this->Inspector->AddSlot().AutoHeight().Padding(0, 0, 0,
	12)[SNew(STextBlock).AutoWrapText(true).Text(this->GetRouteLabel(this->SelectedBus)).ColorAndOpacity(VST3MixerStyle::Accent)];
	if (this->SelectedBus.Get() == this->Model->GetRootBus())
	{
		this->Inspector->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock).AutoWrapText(true)
			.Text(FText::FromString(this->SelectedBus.Get() == this->Model->GetMasterBus()
				? TEXT("ROOT BUS / Project Master") : TEXT("ROOT BUS / UEの既存出力へ接続")))];
	}
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
		.Text(FText::FromString(TEXT("Browse bus asset")))
		.OnClicked_Lambda(
		[this]()
		{
			VST3MixerStyle::Browse(this->SelectedBus.Get());
			return FReply::Handled();
		})];
	if (this->CanEditBus(this->SelectedBus) == false)
	{
		this->Inspector->AddSlot().AutoHeight().Padding(0, 12)[SNew(STextBlock).AutoWrapText(true)
			.Text(FText::FromString(TEXT("編集範囲に問題があるため読み取り専用です。共有されたバスはMake independentで独立できます。詳細は下部のメッセージを確認してください。")))];
		return;
	}
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
		.Text(FText::FromString(TEXT("Route selected sounds here")))
		.IsEnabled(this, &SVST3Mixer::CanEditStructure)
		.ToolTipText(FText::FromString(TEXT("Content Browserで選択したSound Wave / Sound Cueなどの音源を、このバスへ送ります。")))
		.OnClicked_Lambda(
		[this]()
		{
			FText Error;
			int32 Count = 0;
			const auto bRouted = this->Model->RouteSelectedSounds(this->SelectedBus.Get(), Count, Error);
			this->Report(bRouted, Error,
			FText::FromString(
			FString::Printf(TEXT("音源%d件の出力先を変更しました。Save mixerで音源も保存します。"), Count)));
			return FReply::Handled();
		})];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
		.Text(FText::FromString(TEXT("Set root as project master…")))
		.IsEnabled_Lambda([this]() { return this->CanEditStructure() && this->SelectedBus.Get() == this->Model->GetRootBus(); })
		.ToolTipText(FText::FromString(TEXT("Project SettingsのMaster Submixを変更します。UEの再起動後に反映されます。")))
		.OnClicked_Lambda(
		[this]()
		{
			FText Error;
			const auto* CurrentMaster = this->Model->GetMasterBus();
			const auto Message = FText::FromString(FString::Printf(
			TEXT("Project SettingsのMaster Submixを変更します。\n%s → %s\n\n変更はUEの再起動後に反映されます。続行しますか？"),
			CurrentMaster != nullptr ? *CurrentMaster->GetName() : TEXT("UE default"), *this->SelectedBus->GetName()));
			if (FMessageDialog::Open(EAppMsgType::YesNo, Message) != EAppReturnType::Yes)
			{
				return FReply::Handled();
			}
			const auto bChanged = this->Model->SetMasterBus(this->SelectedBus.Get(), Error);
			this->Report(bChanged, Error, FText::FromString(TEXT("Master Submixを変更しました。UEの再起動後に反映されます。")));
			if (bChanged == true)
			{
				this->Refresh();
			}
			return FReply::Handled();
		})];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
		.Text(FText::FromString(TEXT("Remove bus from mixer")))
		.IsEnabled(this, &SVST3Mixer::CanEditStructure)
		.ToolTipText(FText::FromString(TEXT("ミキサーの所属から外します。Submixアセットと音源の出力先は保持します。")))
		.OnClicked_Lambda([this]()
		{
			FText Error;
			const auto bRemoved = this->Model->RemoveBusFromMixer(this->SelectedBus.Get(), Error);
			this->Report(bRemoved, Error);
			if (bRemoved == true)
			{
				this->Refresh();
			}
			return FReply::Handled();
		})];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 20, 0, 8)[VST3MixerStyle::Label(TEXT("ADD VST3"), 11)];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock)
		.AutoWrapText(true)
		.ColorAndOpacity(VST3MixerStyle::Muted)
		.Text(FText::FromString(TEXT("インポート済みモジュールから、このスロット専用のプリセットを作成します。")))];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SObjectPropertyEntryBox)
		.AllowedClass(UVST3ModuleAsset::StaticClass())
		.IsEnabled(this, &SVST3Mixer::CanEditStructure)
		.ObjectPath_Lambda([this]() { return this->SelectedModule.IsValid() ? this->SelectedModule->GetPathName() : FString(); })
		.OnObjectChanged_Lambda(
		[this](const FAssetData& Asset)
		{
			this->SelectedModule.Reset(Cast<UVST3ModuleAsset>(Asset.GetAsset()));
			this->SelectedClassId.Reset();
			if (this->SelectedModule.IsValid() == true)
			{
				for (const auto& Component : this->SelectedModule->GetComponents())
				{
					if (Component.IsSupportedEffect() == true)
					{
						this->SelectedClassId = Component.ClassId;
						break;
					}
				}
			}
		})];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SComboButton)
		.IsEnabled(this, &SVST3Mixer::CanEditStructure)
		.OnGetMenuContent(this, &SVST3Mixer::MakeComponentMenu)
		.ButtonContent()[SNew(STextBlock)
			.Text_Lambda(
			[this]()
			{
				const auto* Component
				= this->SelectedModule.IsValid() ? this->SelectedModule->FindComponent(this->SelectedClassId) : nullptr;
				return FText::FromString(Component != nullptr ? Component->Name : TEXT("Select a component"));
			})]];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
		.Text(FText::FromString(TEXT("+ Create insert")))
		.IsEnabled_Lambda(
		[this]() { return this->CanEditStructure() && this->SelectedModule.IsValid() && this->SelectedClassId.IsEmpty() == false; })
		.OnClicked_Lambda(
		[this]()
		{
			FText Error;
			UVST3SubmixEffectPreset* NewPreset = nullptr;
			const auto bAdded = this->Model->CreatePresetAndInsert(
			this->SelectedBus.Get(), this->SelectedModule.Get(), this->SelectedClassId, NewPreset, Error);
			this->Report(bAdded, Error);
			if (bAdded == true)
			{
				this->SelectedPreset = NewPreset;
				this->Refresh();
			}
			return FReply::Handled();
		})];
	this->Inspector->AddSlot().AutoHeight().Padding(0, 14, 0, 5)[VST3MixerStyle::Label(TEXT("OR ADD EXISTING PRESET"), 9)];
	this->Inspector->AddSlot().AutoHeight()[SNew(SObjectPropertyEntryBox)
		.AllowedClass(UVST3SubmixEffectPreset::StaticClass())
		.IsEnabled(this, &SVST3Mixer::CanEditStructure)
		.OnObjectChanged_Lambda(
		[this](const FAssetData& Asset)
		{
			const TWeakObjectPtr<UVST3SubmixEffectPreset> Preset = Cast<UVST3SubmixEffectPreset>(Asset.GetAsset());
			if (Preset.IsValid() == false)
			{
				return;
			}
			FText Error;
			const auto bAdded = this->Model->AddExistingPreset(this->SelectedBus.Get(), Preset.Get(), INDEX_NONE, Error);
			this->Report(bAdded, Error);
			if (bAdded == true)
			{
				this->SelectedPreset = Preset;
				this->Refresh();
			}
		})];
	if (this->SelectedPreset.IsValid() == true)
	{
		this->Inspector->AddSlot().AutoHeight().Padding(0, 20, 0, 8)[VST3MixerStyle::Label(TEXT("SELECTED INSERT"), 11)];
		this->Inspector->AddSlot().AutoHeight().Padding(
		0, 3)[SNew(STextBlock).Text(FText::FromString(this->SelectedPreset->GetName())).AutoWrapText(true)];
		this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
			.Text(FText::FromString(TEXT("Open VST3 GUI")))
			.OnClicked_Lambda(
			[this]()
			{
				this->OpenEditor(this->SelectedPreset);
				return FReply::Handled();
			})];
		this->Inspector->AddSlot().AutoHeight().Padding(0, 3)[SNew(SButton)
			.Text(FText::FromString(TEXT("Browse preset asset")))
			.OnClicked_Lambda(
			[this]()
			{
				VST3MixerStyle::Browse(this->SelectedPreset.Get());
				return FReply::Handled();
			})];
	}
	this->Inspector->AddSlot().AutoHeight().Padding(0, 18, 0, 0)[SNew(STextBlock)
		.AutoWrapText(true)
		.ColorAndOpacity(VST3MixerStyle::Muted)
		.Text(FText::FromString(TEXT("PIE中もGUI・バイパス・音量は操作できます。ルートと挿入順の変更は停止中に行います。")))];
}

// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3MixerModel.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AudioPluginHostEditorModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Factories/SoundSubmixFactory.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundEffectSubmix.h"
#include "Sound/SoundSubmix.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "VST3ModuleAsset.h"
#include "VST3MixerAsset.h"
#include "VST3SubmixEffectPreset.h"

namespace
{
	bool IsProjectAsset(const UObject* Asset)
	{
		return IsValid(Asset) == true && Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/"));
	}

	FProperty* FindSubmixProperty(const FName Name)
	{
		return FindFProperty<FProperty>(USoundSubmix::StaticClass(), Name);
	}

	void NotifySubmixProperty(USoundSubmix& Bus, FProperty* Property, const EPropertyChangeType::Type ChangeType)
	{
		FPropertyChangedEvent Event(Property, ChangeType);
		static_cast<UObject&>(Bus).PostEditChangeProperty(Event);
		Bus.MarkPackageDirty();
	}

	void SyncContentBrowser(UObject* Asset)
	{
		auto& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> Assets;
		Assets.Add(FAssetData(Asset));
		ContentBrowser.Get().SyncBrowserToAssets(Assets);
	}
}

FVST3MixerModel::FVST3MixerModel(UVST3MixerAsset* InMixer)
	: Mixer(InMixer)
{
	this->Refresh();
}

FVST3MixerModel::~FVST3MixerModel() = default;

void FVST3MixerModel::Refresh()
{
	this->Buses.Reset();
	this->LoadedBuses.Reset();
	this->ScopeError = FText::GetEmpty();
	this->bHasSharedBuses = false;

	auto* MixerAsset = this->Mixer.Get();
	if (IsProjectAsset(MixerAsset) == false)
	{
		this->ScopeError = FText::FromString(TEXT("/Game内のVST Mixerアセットを開いてください。"));
		return;
	}
	if (MixerAsset->RootBus.IsNull() == false)
	{
		if (auto* Root = MixerAsset->RootBus.LoadSynchronous(); Root != nullptr)
		{
			this->LoadedBuses.Emplace(Root);
			this->Buses.Add(Root);
		}
	}
	for (const auto& BusReference : MixerAsset->AdditionalBuses)
	{
		if (auto* Bus = BusReference.LoadSynchronous(); Bus != nullptr)
		{
			this->LoadedBuses.Emplace(Bus);
			this->Buses.Add(Bus);
		}
	}
	this->RefreshEditScope();
}

UVST3MixerAsset* FVST3MixerModel::GetMixer() const
{
	return this->Mixer.Get();
}

USoundSubmix* FVST3MixerModel::GetRootBus() const
{
	return this->Mixer.Get() != nullptr ? this->Mixer->RootBus.Get() : nullptr;
}

void FVST3MixerModel::RefreshEditScope()
{
	const auto* MixerAsset = this->Mixer.Get();
	if (MixerAsset == nullptr)
	{
		return;
	}
	if (MixerAsset->RootBus.IsNull() == true && MixerAsset->AdditionalBuses.IsEmpty() == false)
	{
		this->ScopeError = FText::FromString(TEXT("ルートがないミキサーに追加バスが登録されています。"));
		return;
	}

	TSet<FSoftObjectPath> MemberPaths;
	TArray<TSoftObjectPtr<USoundSubmix>> References = MixerAsset->AdditionalBuses;
	if (MixerAsset->RootBus.IsNull() == false)
	{
		References.Insert(MixerAsset->RootBus, 0);
	}
	for (const auto& Reference : References)
	{
		if (Reference.IsNull() == true || IsProjectAsset(Reference.Get()) == false)
		{
			this->ScopeError = FText::FromString(TEXT("所属Submixの参照が壊れているか、/Game外を指しています。"));
			return;
		}
		const auto Path = Reference.ToSoftObjectPath();
		if (MemberPaths.Contains(Path) == true)
		{
			this->ScopeError = FText::FromString(TEXT("同じSubmixがミキサーに重複登録されています。"));
			return;
		}
		MemberPaths.Add(Path);
	}

	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter MixerFilter;
	MixerFilter.PackagePaths.Add(TEXT("/Game"));
	MixerFilter.bRecursivePaths = true;
	MixerFilter.ClassPaths.Add(UVST3MixerAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> MixerAssets;
	Registry.GetAssets(MixerFilter, MixerAssets);
	for (const auto& AssetData : MixerAssets)
	{
		const auto* Other = Cast<UVST3MixerAsset>(AssetData.GetAsset());
		if (Other == nullptr || Other == MixerAsset)
		{
			continue;
		}
		if (MemberPaths.Contains(Other->RootBus.ToSoftObjectPath()) == true)
		{
			this->bHasSharedBuses = true;
		}
		for (const auto& OtherBus : Other->AdditionalBuses)
		{
			if (MemberPaths.Contains(OtherBus.ToSoftObjectPath()) == true)
			{
				this->bHasSharedBuses = true;
			}
		}
	}
	auto* Root = this->GetRootBus();
	for (const auto& WeakBus : this->Buses)
	{
		auto* Bus = WeakBus.Get();
		if (Bus == Root)
		{
			continue;
		}
		TSet<USoundSubmix*> Visited;
		auto* Parent = Cast<USoundSubmix>(Bus != nullptr ? Bus->ParentSubmix.Get() : nullptr);
		while (Parent != nullptr && Parent != Root && Visited.Contains(Parent) == false)
		{
			if (this->Buses.Contains(Parent) == false)
			{
				this->ScopeError = FText::FromString(TEXT("追加バスの途中の親も、このミキサーに採用してください。"));
				return;
			}
			Visited.Add(Parent);
			Parent = Cast<USoundSubmix>(Parent->ParentSubmix.Get());
		}
		if (Parent != Root)
		{
			this->ScopeError = FText::FromString(TEXT("追加バスがルートへ到達しません。Submixの親接続を確認してください。"));
			return;
		}
	}

	// ルートの外部出力も含め、読み込んだ既存経路の循環を拒否する。
	TSet<USoundSubmixBase*> Visited;
	auto* Current = static_cast<USoundSubmixBase*>(Root);
	while (Current != nullptr)
	{
		if (Visited.Contains(Current) == true)
		{
			this->ScopeError = FText::FromString(TEXT("ルートの外部出力に循環するSubmix接続があります。"));
			return;
		}
		Visited.Add(Current);
		const auto* WithParent = Cast<USoundSubmixWithParentBase>(Current);
		Current = WithParent != nullptr ? WithParent->ParentSubmix.Get() : nullptr;
	}
}

bool FVST3MixerModel::ValidateEditScope(FText& OutError) const
{
	OutError = this->ScopeError;
	if (OutError.IsEmpty() == true && this->bHasSharedBuses == true)
	{
		OutError = FText::FromString(TEXT("別のVST Mixerも同じSubmixを参照しています。Make independentで独立させてください。"));
	}
	return OutError.IsEmpty();
}

bool FVST3MixerModel::HasSharedBuses() const
{
	return this->bHasSharedBuses;
}

bool FVST3MixerModel::ValidateBusForEdit(USoundSubmix* Bus, FText& OutError) const
{
	if (this->ValidateEditScope(OutError) == false)
	{
		return false;
	}
	if (IsProjectAsset(Bus) == false || this->Buses.Contains(Bus) == false)
	{
		OutError = FText::FromString(TEXT("このミキサーに所属する/Game内のSubmixを選択してください。"));
		return false;
	}
	return true;
}

const TArray<TWeakObjectPtr<USoundSubmix>>& FVST3MixerModel::GetBuses() const
{
	return this->Buses;
}

USoundSubmix* FVST3MixerModel::GetMasterBus() const
{
	if (auto* Configured = Cast<USoundSubmix>(GetDefault<UAudioSettings>()->MasterSubmix.TryLoad()); Configured != nullptr)
	{
		return Configured;
	}
	return Cast<USoundSubmix>(FSoftObjectPath(
		TEXT("/Engine/EngineSounds/Submixes/MasterSubmixDefault.MasterSubmixDefault")).TryLoad());
}

bool FVST3MixerModel::CanEditStructure() const
{
	return GEditor == nullptr || GEditor->PlayWorld == nullptr;
}

bool FVST3MixerModel::CreateRootBus(const FString& Name, USoundSubmix*& OutBus, FText& OutError)
{
	if (this->GetRootBus() != nullptr)
	{
		OutBus = nullptr;
		OutError = FText::FromString(TEXT("ルートバスは既に存在します。"));
		return false;
	}
	return this->CreateBus(Name, nullptr, OutBus, OutError);
}

bool FVST3MixerModel::CreateBus(const FString& Name, USoundSubmix* Parent, USoundSubmix*& OutBus, FText& OutError)
{
	OutBus = nullptr;
	this->Refresh();
	if (this->CanEditStructure() == false)
	{
		OutError = FText::FromString(TEXT("PIE中はSubmixの構造を変更できません。"));
		return false;
	}
	if (this->ValidateEditScope(OutError) == false)
	{
		return false;
	}
	if ((this->GetRootBus() == nullptr && Parent != nullptr)
		|| (this->GetRootBus() != nullptr && (Parent == nullptr || this->ValidateBusForEdit(Parent, OutError) == false)))
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("親はこのミキサーに所属するバスを選択してください。"));
		}
		return false;
	}

	const auto CleanName = Name.TrimStartAndEnd().Replace(TEXT(" "), TEXT("_"));
	const FString Folder = this->Mixer->GetOutermost()->GetName() + TEXT("_Assets/Buses");
	if (CleanName.IsEmpty() == true || FPackageName::IsValidLongPackageName(Folder / CleanName) == false)
	{
		OutError = FText::FromString(TEXT("Submix名に使用できない文字が含まれています。"));
		return false;
	}

	auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	FString PackageName;
	FString AssetName;
	Tools.CreateUniqueAssetName(Folder / CleanName, TEXT(""), PackageName, AssetName);
	auto* Factory = NewObject<USoundSubmixFactory>();
	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: Submixを作成")));
	OutBus = Cast<USoundSubmix>(Tools.CreateAsset(AssetName, Folder, USoundSubmix::StaticClass(), Factory));
	if (OutBus == nullptr)
	{
		OutError = FText::FromString(TEXT("Submixアセットを作成できません。"));
		return false;
	}
	this->Mixer->Modify();
	if (Parent != nullptr)
	{
		Parent->Modify();
		OutBus->Modify();
		OutBus->SetParentSubmix(Parent);
		if (auto* Property = FindFProperty<FProperty>(USoundSubmixWithParentBase::StaticClass(),
			GET_MEMBER_NAME_CHECKED(USoundSubmixWithParentBase, ParentSubmix)); Property != nullptr)
		{
			NotifySubmixProperty(*OutBus, Property, EPropertyChangeType::ValueSet);
		}
		Parent->MarkPackageDirty();
		this->TrackAssetForSave(Parent);
		this->Mixer->AdditionalBuses.Add(OutBus);
	}
	else
	{
		this->Mixer->RootBus = OutBus;
	}
	this->Mixer->MarkPackageDirty();
	this->TrackAssetForSave(OutBus);
	this->Refresh();
	SyncContentBrowser(OutBus);
	return true;
}

bool FVST3MixerModel::AdoptExistingBus(USoundSubmix* Bus, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateEditScope(OutError) == false)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIE中はバスを採用できません。"));
		}
		return false;
	}
	if (IsProjectAsset(Bus) == false || this->Buses.Contains(Bus) == true)
	{
		OutError = FText::FromString(TEXT("/Game内の未登録Submixを選択してください。"));
		return false;
	}

	// 既存バスが他のミキサーに所属する場合は、共有状態を作らない。
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UVST3MixerAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	const FSoftObjectPath BusPath(Bus);
	for (const auto& Asset : Assets)
	{
		const auto* Other = Cast<UVST3MixerAsset>(Asset.GetAsset());
		if (Other == nullptr || Other == this->Mixer.Get())
		{
			continue;
		}
		if (Other->RootBus.ToSoftObjectPath() == BusPath || Other->AdditionalBuses.ContainsByPredicate(
			[&BusPath](const TSoftObjectPtr<USoundSubmix>& Reference)
			{
				return Reference.ToSoftObjectPath() == BusPath;
			}))
		{
			OutError = FText::FromString(TEXT("このSubmixは別のVST Mixerに所属しています。"));
			return false;
		}
	}

	auto* Root = this->GetRootBus();
	if (Root != nullptr)
	{
		TSet<USoundSubmix*> Visited;
		auto* Parent = Cast<USoundSubmix>(Bus->ParentSubmix.Get());
		while (Parent != nullptr && Parent != Root && Visited.Contains(Parent) == false)
		{
			if (this->Buses.Contains(Parent) == false)
			{
				OutError = FText::FromString(TEXT("ルートから順に、途中の親バスを先に採用してください。"));
				return false;
			}
			Visited.Add(Parent);
			Parent = Cast<USoundSubmix>(Parent->ParentSubmix.Get());
		}
		if (Parent != Root)
		{
			OutError = FText::FromString(TEXT("採用する追加バスは、既存の親経路でルートへ到達する必要があります。"));
			return false;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: 既存バスを採用")));
	this->Mixer->Modify();
	if (Root == nullptr)
	{
		this->Mixer->RootBus = Bus;
	}
	else
	{
		this->Mixer->AdditionalBuses.Add(Bus);
	}
	this->Mixer->MarkPackageDirty();
	this->Refresh();
	return true;
}

bool FVST3MixerModel::RemoveBusFromMixer(USoundSubmix* Bus, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false)
	{
		return false;
	}
	if (Bus == this->GetRootBus() && this->Mixer->AdditionalBuses.IsEmpty() == false)
	{
		OutError = FText::FromString(TEXT("ルートを外す前に追加バスを外してください。"));
		return false;
	}
	for (const auto& ListedBus : this->Buses)
	{
		if (ListedBus.Get() != Bus && ListedBus.IsValid() == true && ListedBus->ParentSubmix.Get() == Bus)
		{
			OutError = FText::FromString(TEXT("子バスが接続されています。子を先に外すか親を変更してください。"));
			return false;
		}
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: バスの所属を解除")));
	this->Mixer->Modify();
	if (Bus == this->GetRootBus())
	{
		this->Mixer->RootBus.Reset();
	}
	else
	{
		this->Mixer->AdditionalBuses.Remove(Bus);
	}
	this->Mixer->MarkPackageDirty();
	this->Refresh();
	return true;
}

bool FVST3MixerModel::ResetBusReferences(FText& OutError)
{
	if (this->CanEditStructure() == false || IsProjectAsset(this->Mixer.Get()) == false)
	{
		OutError = FText::FromString(TEXT("PIEを停止し、/Game内のVST Mixerを開いてください。"));
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: バス参照をクリア")));
	this->Mixer->Modify();
	this->Mixer->RootBus.Reset();
	this->Mixer->AdditionalBuses.Reset();
	this->Mixer->MarkPackageDirty();
	this->Refresh();
	return true;
}

bool FVST3MixerModel::MakeIndependent(FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || IsProjectAsset(this->Mixer.Get()) == false
		|| this->bHasSharedBuses == false)
	{
		OutError = FText::FromString(TEXT("PIEを停止し、共有Submixを持つVST Mixerを開いてください。"));
		return false;
	}
	if (this->Buses.IsEmpty() == true || this->GetRootBus() == nullptr)
	{
		OutError = FText::FromString(TEXT("複製するルートSubmixがありません。"));
		return false;
	}
	if (this->ScopeError.IsEmpty() == false)
	{
		OutError = this->ScopeError;
		return false;
	}

	// 複製元のGUIで編集中の設定も、コピーへ引き継ぐ。
	auto& EditorModule = FModuleManager::LoadModuleChecked<FAudioPluginHostEditorModule>(TEXT("AudioPluginHostEditor"));
	for (const auto& Bus : this->Buses)
	{
		for (const auto& Effect : Bus->SubmixEffectChain)
		{
			if (auto* Preset = Cast<UVST3SubmixEffectPreset>(Effect.Get()); Preset != nullptr
				&& EditorModule.TrySaveEditorsForPreset(Preset, OutError) == false)
			{
				return false;
			}
		}
	}

	const FString AssetFolder = this->Mixer->GetOutermost()->GetName() + TEXT("_Assets");
	const FString BusFolder = AssetFolder / TEXT("Buses");
	const FString PresetFolder = AssetFolder / TEXT("Presets");
	auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TMap<USoundSubmix*, USoundSubmix*> BusCopies;
	TMap<USoundEffectSubmixPreset*, USoundEffectSubmixPreset*> PresetCopies;
	TArray<UObject*> CreatedAssets;
	auto RollBack = [&CreatedAssets]()
	{
		for (auto* Asset : CreatedAssets)
		{
			FAssetRegistryModule::AssetDeleted(Asset);
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->MarkAsGarbage();
			Asset->GetOutermost()->SetDirtyFlag(false);
		}
	};

	// 保存元は触らず、全バスと全挿入プリセットの複製を先に作る。
	for (const auto& WeakBus : this->Buses)
	{
		auto* Source = WeakBus.Get();
		if (Source == nullptr)
		{
			RollBack();
			OutError = FText::FromString(TEXT("複製元のSubmixを読み込めません。"));
			return false;
		}
		FString PackageName;
		FString AssetName;
		Tools.CreateUniqueAssetName(BusFolder / Source->GetName(), TEXT(""), PackageName, AssetName);
		auto* Copy = Cast<USoundSubmix>(Tools.DuplicateAsset(AssetName, BusFolder, Source));
		if (Copy == nullptr)
		{
			RollBack();
			OutError = FText::FromString(TEXT("Submixの複製に失敗しました。"));
			return false;
		}
		CreatedAssets.Add(Copy);
		BusCopies.Add(Source, Copy);
	}
	for (const auto& Pair : BusCopies)
	{
		for (const auto& Effect : Pair.Key->SubmixEffectChain)
		{
			auto* SourcePreset = Effect.Get();
			if (SourcePreset == nullptr)
			{
				RollBack();
				OutError = FText::FromString(TEXT("複製元のinsertプリセットを読み込めません。"));
				return false;
			}
			if (PresetCopies.Contains(SourcePreset) == true)
			{
				continue;
			}
			FString PackageName;
			FString AssetName;
			Tools.CreateUniqueAssetName(PresetFolder / SourcePreset->GetName(), TEXT(""), PackageName, AssetName);
			auto* Copy = Cast<USoundEffectSubmixPreset>(Tools.DuplicateAsset(AssetName, PresetFolder, SourcePreset));
			if (Copy == nullptr)
			{
				RollBack();
				OutError = FText::FromString(TEXT("insertプリセットの複製に失敗しました。"));
				return false;
			}
			CreatedAssets.Add(Copy);
			PresetCopies.Add(SourcePreset, Copy);
		}
	}

	// 親子リストに残った旧参照を消してから、コピーだけで内部経路を作る。
	for (const auto& Pair : BusCopies)
	{
		auto* Copy = Pair.Value;
		Copy->Modify();
		Copy->ParentSubmix = nullptr;
		Copy->ChildSubmixes.Reset();
		Copy->SubmixEffectChain.Reset();
		for (const auto& Effect : Pair.Key->SubmixEffectChain)
		{
			Copy->SubmixEffectChain.Add(PresetCopies.FindChecked(Effect.Get()));
		}
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: 共有Submixを独立複製")));
	for (const auto& Pair : BusCopies)
	{
		auto* OldParent = Pair.Key->ParentSubmix.Get();
		auto* InternalParent = Cast<USoundSubmix>(OldParent);
		auto* NewParent = InternalParent != nullptr && BusCopies.Contains(InternalParent) == true
			? BusCopies.FindChecked(InternalParent) : OldParent;
		if (NewParent != nullptr)
		{
			NewParent->Modify();
			this->TrackAssetForSave(NewParent);
		}
		Pair.Value->Modify();
		Pair.Value->SetParentSubmix(NewParent, false);
		Pair.Value->MarkPackageDirty();
		this->TrackAssetForSave(Pair.Value);
	}
	for (const auto& Pair : PresetCopies)
	{
		this->TrackAssetForSave(Pair.Value);
	}
	this->Mixer->Modify();
	this->Mixer->RootBus = BusCopies.FindChecked(this->GetRootBus());
	for (auto& Reference : this->Mixer->AdditionalBuses)
	{
		Reference = BusCopies.FindChecked(Reference.Get());
	}
	this->Mixer->PostEditChange();
	this->Mixer->MarkPackageDirty();
	this->Refresh();
	return true;
}

bool FVST3MixerModel::SetMasterBus(USoundSubmix* Bus, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false
		|| Bus != this->GetRootBus() || Bus->ParentSubmix != nullptr)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIEを停止し、親を持たないルートバスを選択してください。"));
		}
		return false;
	}

	auto* Settings = GetMutableDefault<UAudioSettings>();
	const auto Previous = Settings->MasterSubmix;
	Settings->Modify();
	Settings->MasterSubmix = FSoftObjectPath(Bus);
	if (Settings->TryUpdateDefaultConfigFile() == false)
	{
		Settings->MasterSubmix = Previous;
		OutError = FText::FromString(TEXT("Master Submixをプロジェクト設定に保存できません。"));
		return false;
	}
	this->Refresh();
	return true;
}

bool FVST3MixerModel::SetParentBus(USoundSubmix* Bus, USoundSubmix* Parent, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false
		|| Bus == this->GetRootBus()
		|| (Parent != nullptr && this->ValidateBusForEdit(Parent, OutError) == false))
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIE中またはルートバスの出力先は変更できません。"));
		}
		return false;
	}
	if (Parent == nullptr)
	{
		OutError = FText::FromString(TEXT("追加バスはミキサー内の親へ接続してください。"));
		return false;
	}
	if (Bus == Parent || (Parent != nullptr && (SubmixUtils::AreSubmixFormatsCompatible(Bus, Parent) == false
		|| SubmixUtils::FindInGraph(Bus, Parent, false) == true)))
	{
		OutError = FText::FromString(TEXT("Submixの出力先が循環するか、音声形式が一致しません。"));
		return false;
	}
	if (Bus->ParentSubmix == Parent)
	{
		return true;
	}
	auto* OldParent = Bus->ParentSubmix.Get();
	if (OldParent != nullptr && IsProjectAsset(OldParent) == false)
	{
		OutError = FText::FromString(TEXT("Engine標準Submixの接続はこの画面から変更できません。"));
		return false;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: 出力先を変更")));
	if (OldParent != nullptr)
	{
		OldParent->Modify();
	}
	if (Parent != nullptr)
	{
		Parent->Modify();
	}
	Bus->Modify();
	Bus->SetParentSubmix(Parent);
	if (auto* Property = FindFProperty<FProperty>(USoundSubmixWithParentBase::StaticClass(),
		GET_MEMBER_NAME_CHECKED(USoundSubmixWithParentBase, ParentSubmix)); Property != nullptr)
	{
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Bus)->PostEditChangeProperty(Event);
	}
	Bus->MarkPackageDirty();
	if (OldParent != nullptr)
	{
		OldParent->MarkPackageDirty();
	}
	if (Parent != nullptr)
	{
		Parent->MarkPackageDirty();
	}
	this->TrackAssetForSave(Bus);
	this->TrackAssetForSave(OldParent);
	this->TrackAssetForSave(Parent);
	return true;
}

bool FVST3MixerModel::AddExistingPreset(USoundSubmix* Bus, UVST3SubmixEffectPreset* Preset,
	const int32 Index, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIE中はVST3の挿入位置を変更できません。"));
		}
		return false;
	}
	const int32 InsertIndex = Index == INDEX_NONE ? Bus->SubmixEffectChain.Num() : Index;
	if (IsProjectAsset(Preset) == false || InsertIndex < 0 || InsertIndex > Bus->SubmixEffectChain.Num())
	{
		OutError = FText::FromString(TEXT("挿入するプリセットまたは位置が無効です。"));
		return false;
	}
	// 別ミキサーや通常のSubmixからも、同じ保存状態を共有させない。
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(USoundSubmix::StaticClass()->GetClassPathName());
	TArray<FAssetData> SubmixAssets;
	Registry.GetAssets(Filter, SubmixAssets);
	for (const auto& Asset : SubmixAssets)
	{
		const auto* ExistingBus = Cast<USoundSubmix>(Asset.GetAsset());
		if (ExistingBus != nullptr && ExistingBus->SubmixEffectChain.Contains(Preset) == true)
		{
			OutError = FText::FromString(TEXT("同じVST3プリセットは複数段へ割り当てられません。別のプリセットを作成してください。"));
			return false;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: VST3を追加")));
	auto* Property = FindSubmixProperty(GET_MEMBER_NAME_CHECKED(USoundSubmix, SubmixEffectChain));
	Bus->Modify();
	static_cast<UObject*>(Bus)->PreEditChange(Property);
	Bus->SubmixEffectChain.Insert(Preset, InsertIndex);
	NotifySubmixProperty(*Bus, Property, EPropertyChangeType::ArrayAdd);
	this->TrackAssetForSave(Bus);
	this->TrackAssetForSave(Preset);
	return true;
}

bool FVST3MixerModel::CreatePresetAndInsert(USoundSubmix* Bus, UVST3ModuleAsset* Module,
	const FString& ClassId, UVST3SubmixEffectPreset*& OutPreset, FText& OutError)
{
	OutPreset = nullptr;
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIE中はVST3を追加できません。"));
		}
		return false;
	}
	const auto* Component = IsValid(Module) == true ? Module->FindComponent(ClassId.ToLower()) : nullptr;
	if (Component == nullptr || Component->IsSupportedEffect() == false)
	{
		OutError = FText::FromString(TEXT("ステレオfloat32対応のVST3 Audio Effectを選択してください。"));
		return false;
	}

	const FString Folder = this->Mixer->GetOutermost()->GetName() + TEXT("_Assets/Presets");
	auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	FString PackageName;
	FString AssetName;
	Tools.CreateUniqueAssetName(Folder / TEXT("P_VST3"), TEXT(""), PackageName, AssetName);
	auto* Package = CreatePackage(*PackageName);
	OutPreset = NewObject<UVST3SubmixEffectPreset>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (OutPreset->SetModuleAndComponent(Module, ClassId) == false)
	{
		OutPreset->ClearFlags(RF_Public | RF_Standalone);
		OutPreset->MarkAsGarbage();
		OutPreset = nullptr;
		OutError = FText::FromString(TEXT("VST3プリセットにモジュールを設定できません。"));
		return false;
	}
	FAssetRegistryModule::AssetCreated(OutPreset);
	OutPreset->MarkPackageDirty();
	if (this->AddExistingPreset(Bus, OutPreset, Bus->SubmixEffectChain.Num(), OutError) == false)
	{
		FAssetRegistryModule::AssetDeleted(OutPreset);
		OutPreset->ClearFlags(RF_Public | RF_Standalone);
		OutPreset->MarkAsGarbage();
		OutPreset = nullptr;
		return false;
	}
	// インポート直後の台帳も保存し、再起動後にプリセットの参照を解決できるようにする。
	this->TrackAssetForSave(Module);
	SyncContentBrowser(OutPreset);
	return true;
}

bool FVST3MixerModel::RemoveInsert(USoundSubmix* Bus, const int32 Index, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIE中はVST3の構造を変更できません。"));
		}
		return false;
	}
	if (Bus->SubmixEffectChain.IsValidIndex(Index) == false)
	{
		OutError = FText::FromString(TEXT("削除するinsertがありません。"));
		return false;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: Insertを削除")));
	auto* Property = FindSubmixProperty(GET_MEMBER_NAME_CHECKED(USoundSubmix, SubmixEffectChain));
	Bus->Modify();
	static_cast<UObject*>(Bus)->PreEditChange(Property);
	this->TrackAssetForSave(Bus->SubmixEffectChain[Index].Get());
	Bus->SubmixEffectChain.RemoveAt(Index);
	NotifySubmixProperty(*Bus, Property, EPropertyChangeType::ArrayRemove);
	this->TrackAssetForSave(Bus);
	return true;
}

bool FVST3MixerModel::MoveInsert(USoundSubmix* Bus, const int32 From, const int32 To, FText& OutError)
{
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIE中はVST3の順序を変更できません。"));
		}
		return false;
	}
	if (Bus->SubmixEffectChain.IsValidIndex(From) == false || Bus->SubmixEffectChain.IsValidIndex(To) == false)
	{
		OutError = FText::FromString(TEXT("移動するinsertの位置が無効です。"));
		return false;
	}
	if (From == To)
	{
		return true;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: Insertを並べ替え")));
	auto* Property = FindSubmixProperty(GET_MEMBER_NAME_CHECKED(USoundSubmix, SubmixEffectChain));
	Bus->Modify();
	static_cast<UObject*>(Bus)->PreEditChange(Property);
	const auto Effect = Bus->SubmixEffectChain[From];
	Bus->SubmixEffectChain.RemoveAt(From);
	Bus->SubmixEffectChain.Insert(Effect, To);
	NotifySubmixProperty(*Bus, Property, EPropertyChangeType::ArrayMove);
	this->TrackAssetForSave(Bus);
	return true;
}

bool FVST3MixerModel::SetPresetBypassed(UVST3SubmixEffectPreset* Preset, const bool bBypass, FText& OutError)
{
	this->Refresh();
	if (this->ValidateEditScope(OutError) == false)
	{
		return false;
	}
	const auto bIsInsert = this->Buses.ContainsByPredicate([WeakPreset = TWeakObjectPtr<UVST3SubmixEffectPreset>(Preset)](const auto& Bus)
	{
		return Bus.IsValid() == true && Bus->SubmixEffectChain.Contains(WeakPreset.Get());
	});
	if (IsProjectAsset(Preset) == false || bIsInsert == false)
	{
		OutError = FText::FromString(TEXT("このミキサーの挿入プリセットを選択してください。"));
		return false;
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: VST3のバイパスを変更")));
	const bool bHadRuntimeBypass = Preset->HasRuntimeHostBypass();
	Preset->Modify();
	Preset->SetHostBypassed(bBypass);
	if (bHadRuntimeBypass == true)
	{
		Preset->SetRuntimeHostBypassed(bBypass);
	}
	this->TrackAssetForSave(Preset);
	return true;
}

bool FVST3MixerModel::BeginVolumeEdit(USoundSubmix* Bus, FText& OutError)
{
	this->Refresh();
	if (this->ValidateBusForEdit(Bus, OutError) == false)
	{
		return false;
	}
	if (this->VolumeTransaction.IsValid() == true)
	{
		OutError = FText::FromString(TEXT("別のバスの音量を編集中です。"));
		return false;
	}

	this->VolumeTransaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("VST Mixer: バス音量を変更")));
	this->VolumeBus = Bus;
	Bus->Modify();
	this->TrackAssetForSave(Bus);
	static_cast<UObject*>(Bus)->PreEditChange(FindSubmixProperty(GET_MEMBER_NAME_CHECKED(USoundSubmix, OutputVolumeModulation)));
	return true;
}

bool FVST3MixerModel::SetVolumeDb(USoundSubmix* Bus, const float Db, FText& OutError)
{
	if (this->VolumeTransaction.IsValid() == false)
	{
		this->Refresh();
	}
	if (this->ValidateBusForEdit(Bus, OutError) == false || FMath::IsFinite(Db) == false || Db < -96.0f || Db > 0.0f)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("バス音量は-96 dBから0 dBの範囲で指定してください。"));
		}
		return false;
	}
	if (this->VolumeTransaction.IsValid() == true && this->VolumeBus.Get() != Bus)
	{
		OutError = FText::FromString(TEXT("別のバスの音量を編集中です。"));
		return false;
	}
	if (FMath::IsNearlyEqual(Bus->OutputVolumeModulation.Value, Db) == true)
	{
		return true;
	}

	auto* Property = FindSubmixProperty(GET_MEMBER_NAME_CHECKED(USoundSubmix, OutputVolumeModulation));
	if (this->VolumeTransaction.IsValid() == false)
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: バス音量を変更")));
		Bus->Modify();
		this->TrackAssetForSave(Bus);
		static_cast<UObject*>(Bus)->PreEditChange(Property);
		Bus->OutputVolumeModulation.Value = Db;
		NotifySubmixProperty(*Bus, Property, EPropertyChangeType::ValueSet);
		return true;
	}

	Bus->OutputVolumeModulation.Value = Db;
	NotifySubmixProperty(*Bus, Property, EPropertyChangeType::Interactive);
	return true;
}

void FVST3MixerModel::EndVolumeEdit(USoundSubmix* Bus)
{
	if (this->VolumeTransaction.IsValid() == false || this->VolumeBus.Get() != Bus)
	{
		return;
	}

	if (IsValid(Bus) == true)
	{
		NotifySubmixProperty(*Bus, FindSubmixProperty(GET_MEMBER_NAME_CHECKED(USoundSubmix, OutputVolumeModulation)),
			EPropertyChangeType::ValueSet);
	}
	this->VolumeTransaction.Reset();
	this->VolumeBus.Reset();
}

bool FVST3MixerModel::RouteSelectedSounds(USoundSubmix* Bus, int32& OutCount, FText& OutError)
{
	OutCount = 0;
	auto& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssets;
	Browser.Get().GetSelectedAssets(SelectedAssets);
	TArray<USoundBase*> Sounds;
	for (const auto& Asset : SelectedAssets)
	{
		if (auto* Sound = Cast<USoundBase>(Asset.GetAsset()); IsProjectAsset(Sound) == true)
		{
			Sounds.Add(Sound);
		}
	}
	return this->RouteSounds(Bus, Sounds, OutCount, OutError);
}

bool FVST3MixerModel::RouteSounds(USoundSubmix* Bus, const TArray<USoundBase*>& Sounds,
	int32& OutCount, FText& OutError)
{
	OutCount = 0;
	this->Refresh();
	if (this->CanEditStructure() == false || this->ValidateBusForEdit(Bus, OutError) == false)
	{
		if (OutError.IsEmpty() == true)
		{
			OutError = FText::FromString(TEXT("PIEを停止してから音声の出力先を変更してください。"));
		}
		return false;
	}
	if (Sounds.IsEmpty() == true || Sounds.ContainsByPredicate([](const auto* Sound)
		{ return IsProjectAsset(Sound) == false; }))
	{
		OutError = FText::FromString(TEXT("Content Browserで/Game内のSound Waveなど音声アセットを選択してください。"));
		return false;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("VST Mixer: 音声の出力バスを変更")));
	auto* RoutingProperty = FindFProperty<FProperty>(USoundBase::StaticClass(),
		GET_MEMBER_NAME_CHECKED(USoundBase, SoundSubmixObject));
	for (auto* Sound : Sounds)
	{
		Sound->Modify();
		static_cast<UObject*>(Sound)->PreEditChange(RoutingProperty);
		Sound->bEnableBaseSubmix = true;
		Sound->SoundSubmixObject = Bus;
		FPropertyChangedEvent Event(RoutingProperty, EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Sound)->PostEditChangeProperty(Event);
		Sound->MarkPackageDirty();
		this->TrackAssetForSave(Sound);
		++OutCount;
	}
	return true;
}

void FVST3MixerModel::TrackAssetForSave(UObject* Asset)
{
	if (IsProjectAsset(Asset) == true && this->PendingSaveAssets.ContainsByPredicate(
		[WeakAsset = TWeakObjectPtr<UObject>(Asset)](const auto& Pending)
		{ return Pending.Get() == WeakAsset.Get(); }) == false)
	{
		this->PendingSaveAssets.Emplace(Asset);
	}
}

bool FVST3MixerModel::PrepareToSave(FText& OutError)
{
	this->Refresh();
	if (IsProjectAsset(this->Mixer.Get()) == false)
	{
		OutError = FText::FromString(TEXT("/Game内のVST Mixerアセットを開いてください。"));
		return false;
	}
	this->EndVolumeEdit(this->VolumeBus.Get());
	this->TrackAssetForSave(this->Mixer.Get());
	for (const auto& Bus : this->Buses)
	{
		if (Bus.IsValid() == false)
		{
			continue;
		}
		this->TrackAssetForSave(Bus.Get());
		this->TrackAssetForSave(Bus->ParentSubmix.Get());
		for (const auto& Effect : Bus->SubmixEffectChain)
		{
			this->TrackAssetForSave(Effect.Get());
		}
	}

	// 全GUIの状態取得が成功してから、関係するパッケージを保存する。
	auto& EditorModule = FModuleManager::LoadModuleChecked<FAudioPluginHostEditorModule>(TEXT("AudioPluginHostEditor"));
	for (const auto& Asset : this->PendingSaveAssets)
	{
		if (auto* Preset = Cast<UVST3SubmixEffectPreset>(Asset.Get()); Preset != nullptr
			&& EditorModule.TrySaveEditorsForPreset(Preset, OutError) == false)
		{
			return false;
		}
	}
	return true;
}

void FVST3MixerModel::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	if (IsProjectAsset(this->Mixer.Get()) == true)
	{
		OutObjects.AddUnique(this->Mixer.Get());
	}
	for (const auto& Asset : this->PendingSaveAssets)
	{
		if (IsProjectAsset(Asset.Get()) == true)
		{
			OutObjects.AddUnique(Asset.Get());
		}
	}
	for (const auto& Bus : this->Buses)
	{
		if (IsProjectAsset(Bus.Get()) == false)
		{
			continue;
		}
		OutObjects.AddUnique(Bus.Get());
		for (const auto& Effect : Bus->SubmixEffectChain)
		{
			if (IsProjectAsset(Effect.Get()) == true)
			{
				OutObjects.AddUnique(Effect.Get());
			}
		}
	}
}

bool FVST3MixerModel::SaveMixerAndDependencies(FText& OutError)
{
	if (this->PrepareToSave(OutError) == false)
	{
		return false;
	}
	TArray<UPackage*> Packages;
	TArray<UObject*> Objects;
	this->GetSaveableObjects(Objects);
	for (const auto* Asset : Objects)
	{
		Packages.AddUnique(Asset->GetOutermost());
	}
	if (UEditorLoadingAndSavingUtils::SavePackages(Packages, true) == false)
	{
		OutError = FText::FromString(TEXT("ミキサーまたは関連アセットの保存に失敗しました。再試行してください。"));
		return false;
	}
	this->PendingSaveAssets.Reset();
	return true;
}

// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3SubmixEffectPreset.h"

#include "Misc/DataValidation.h"
#include "VST3ModuleAsset.h"
#include "VST3Processor.h"
#include "VST3SubmixEffect.h"

bool UVST3SubmixEffectPreset::CanFilter() const
{
	return false;
}

bool UVST3SubmixEffectPreset::HasAssetActions() const
{
	return true;
}

FText UVST3SubmixEffectPreset::GetAssetActionName() const
{
	return FText::FromString(TEXT("VST3 Submix Effect"));
}

UClass* UVST3SubmixEffectPreset::GetSupportedClass() const
{
	return StaticClass();
}

USoundEffectPreset* UVST3SubmixEffectPreset::CreateNewPreset(UObject* InParent, const FName Name, const EObjectFlags Flags) const
{
	return NewObject<UVST3SubmixEffectPreset>(InParent, Name, Flags);
}

FSoundEffectBase* UVST3SubmixEffectPreset::CreateNewEffect() const
{
	// エフェクトはUEが所有し、Presetは弱参照で渡す。
	return new FVST3SubmixEffect(TWeakObjectPtr<UVST3SubmixEffectPreset>(const_cast<UVST3SubmixEffectPreset*>(this)));
}

void UVST3SubmixEffectPreset::Init()
{
	// Initは音声スレッドからも呼ばれる。
}

const UVST3ModuleAsset* UVST3SubmixEffectPreset::GetModuleAsset() const
{
	return this->ModuleAsset.Get();
}

const FString& UVST3SubmixEffectPreset::GetSelectedClassId() const
{
	return this->SelectedClassId;
}

bool UVST3SubmixEffectPreset::IsHostBypassed() const
{
	return this->bHasRuntimeHostBypass == true ? this->bRuntimeHostBypassed : this->bHostBypassed;
}

bool UVST3SubmixEffectPreset::HasRuntimeHostBypass() const
{
	return this->bHasRuntimeHostBypass;
}

uint32 UVST3SubmixEffectPreset::GetConfigurationRevision() const
{
	return this->ConfigurationRevision;
}

bool UVST3SubmixEffectPreset::SetModuleAndComponent(UVST3ModuleAsset* InModuleAsset, const FString& InClassId)
{
	if (IsInGameThread() == false)
	{
		return false;
	}

	const auto ClassId = InClassId.ToLower();
	const auto* Component = InModuleAsset != nullptr ? InModuleAsset->FindComponent(ClassId) : nullptr;
	if (Component == nullptr || Component->IsSupportedEffect() == false)
	{
		return false;
	}

	if (this->ModuleAsset.Get() == InModuleAsset && this->SelectedClassId == ClassId
		&& (this->PluginState.IsEmpty() == true || this->PluginState.Matches(InModuleAsset->GetContentHash(), ClassId) == true))
	{
		return true;
	}

	this->ModuleAsset = InModuleAsset;
	this->LoadedModule = InModuleAsset;
	this->SelectedClassId = ClassId;
	this->PluginState = {};
	++this->ConfigurationRevision;
	this->MarkPackageDirty();
	return true;
}

void UVST3SubmixEffectPreset::SetHostBypassed(const bool bInHostBypassed)
{
	if (IsInGameThread() == true && this->bHostBypassed != bInHostBypassed)
	{
		this->bHostBypassed = bInHostBypassed;
		this->MarkPackageDirty();
	}
}

void UVST3SubmixEffectPreset::SetRuntimeHostBypassed(const bool bInHostBypassed)
{
	if (IsInGameThread() == true)
	{
		this->bHasRuntimeHostBypass = true;
		this->bRuntimeHostBypassed = bInHostBypassed;
	}
}

void UVST3SubmixEffectPreset::ClearRuntimeHostBypassed()
{
	if (IsInGameThread() == true)
	{
		this->bHasRuntimeHostBypass = false;
	}
}

bool UVST3SubmixEffectPreset::BuildSessionRequest(FVST3SessionRequest& OutRequest, FString& OutError)
{
	OutRequest = {};
	this->LoadedModule = this->ModuleAsset.LoadSynchronous();
	const auto* Module = this->LoadedModule.Get();
	const auto* Component = Module != nullptr ? Module->FindComponent(this->SelectedClassId) : nullptr;
	if (Component == nullptr || Component->IsSupportedEffect() == false)
	{
		OutError = TEXT("ステレオfloat32対応のVST3モジュールとクラスを選択してください。");
		return false;
	}
	if (this->PluginState.IsEmpty() == false && this->PluginState.Matches(Module->GetContentHash(), this->SelectedClassId) == false)
	{
		OutError = TEXT("保存状態とVST3のバイナリまたはクラスが一致しません。再選択してください。");
		return false;
	}

	OutRequest.BundleRelativePath = Module->GetBundleRelativePath();
	OutRequest.EntryBinaryRelativePath = Module->GetEntryBinaryRelativePath();
	OutRequest.ModuleHash = Module->GetContentHash();
	OutRequest.ClassId = this->SelectedClassId;
	OutRequest.ComponentState = this->PluginState.Component;
	OutRequest.ControllerState = this->PluginState.Controller;
	return true;
}

bool UVST3SubmixEffectPreset::SetSerializedState(const FVST3PluginState& InState)
{
	if (IsInGameThread() == false)
	{
		return false;
	}

	const auto* Module = this->ModuleAsset.LoadSynchronous();
	if (Module == nullptr || InState.Matches(Module->GetContentHash(), this->SelectedClassId) == false)
	{
		return false;
	}
	if (this->PluginState.Version == InState.Version && this->PluginState.ModuleHash == InState.ModuleHash
		&& this->PluginState.ClassId == InState.ClassId && this->PluginState.Component == InState.Component
		&& this->PluginState.Controller == InState.Controller)
	{
		return true;
	}

	this->Modify();
	this->PluginState = InState;
	this->MarkPackageDirty();
	// 再生中のインスタンスは作り直さない。
	return true;
}

TArray<FString> UVST3SubmixEffectPreset::GetComponentClassIds() const
{
	TArray<FString> ClassIds;
	const auto* Module = this->ModuleAsset.LoadSynchronous();
	if (Module != nullptr)
	{
		for (const auto& Component : Module->GetComponents())
		{
			if (Component.IsSupportedEffect() == true)
			{
				ClassIds.Add(Component.ClassId);
			}
		}
	}
	return ClassIds;
}

#if WITH_EDITOR
void UVST3SubmixEffectPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UVST3SubmixEffectPreset, ModuleAsset)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UVST3SubmixEffectPreset, SelectedClassId))
	{
		this->PluginState = {};
		this->LoadedModule = this->ModuleAsset.LoadSynchronous();
		this->SelectedClassId = this->SelectedClassId.ToLower();
		const auto* Component = this->LoadedModule != nullptr ? this->LoadedModule->FindComponent(this->SelectedClassId) : nullptr;
		if (Component == nullptr || Component->IsSupportedEffect() == false)
		{
			const auto ClassIds = this->GetComponentClassIds();
			this->SelectedClassId = ClassIds.IsEmpty() == false ? ClassIds[0] : FString();
		}
		++this->ConfigurationRevision;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UVST3SubmixEffectPreset::PostEditUndo()
{
	Super::PostEditUndo();
	this->LoadedModule = nullptr;
	if (this->bHasRuntimeHostBypass == true)
	{
		this->SetRuntimeHostBypassed(this->bHostBypassed);
	}
	++this->ConfigurationRevision;
}

EDataValidationResult UVST3SubmixEffectPreset::IsDataValid(FDataValidationContext& Context) const
{
	const auto* Module = this->ModuleAsset.LoadSynchronous();
	const auto* Component = Module != nullptr ? Module->FindComponent(this->SelectedClassId) : nullptr;
	if (Component == nullptr || Component->IsSupportedEffect() == false
		|| (this->PluginState.IsEmpty() == false && this->PluginState.Matches(Module->GetContentHash(), this->SelectedClassId) == false))
	{
		Context.AddError(FText::FromString(TEXT("VST3の選択または保存状態が現在のモジュールと一致しません。")));
		return EDataValidationResult::Invalid;
	}
	return Module->IsDataValid(Context);
}
#endif

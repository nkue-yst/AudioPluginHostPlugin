// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3ModuleAsset.h"

#include "Misc/DataValidation.h"
#include "VST3PayloadValidation.h"

bool FVST3ComponentDescriptor::IsValidClassId() const
{
	return FVST3PayloadValidation::IsLowerHex(this->ClassId, 32);
}

bool FVST3ComponentDescriptor::IsSupportedEffect() const
{
	return this->IsValidClassId() == true && this->bIsAudioEffect == true && this->bSupportsFloat32 == true
		&& this->MainInputChannels == 2 && this->MainOutputChannels == 2;
}

const FString& UVST3ModuleAsset::GetBundleRelativePath() const
{
	return this->BundleRelativePath;
}

const FString& UVST3ModuleAsset::GetEntryBinaryRelativePath() const
{
	return this->EntryBinaryRelativePath;
}

const FString& UVST3ModuleAsset::GetContentHash() const
{
	return this->ContentHash;
}

const TArray<FVST3ComponentDescriptor>& UVST3ModuleAsset::GetComponents() const
{
	return this->Components;
}

const FVST3ComponentDescriptor* UVST3ModuleAsset::FindComponent(const FString& InClassId) const
{
	return this->Components.FindByPredicate([&InClassId](const FVST3ComponentDescriptor& Descriptor)
	{
		return Descriptor.ClassId == InClassId;
	});
}

#if WITH_EDITORONLY_DATA
const FString& UVST3ModuleAsset::GetImportSourcePath() const
{
	return this->SourcePath;
}
#endif

bool UVST3ModuleAsset::SetImportedModule(const FString& InBundleRelativePath, const FString& InEntryBinaryRelativePath,
	const FString& InContentHash, const TArray<FVST3ComponentDescriptor>& InComponents, const FString& InSourcePath,
	const bool bInRedistributionRightsAcknowledged)
{
	if (FVST3PayloadValidation::IsSafeManagedRelativePath(InBundleRelativePath) == false
		|| FVST3PayloadValidation::IsSafeManagedRelativePath(InEntryBinaryRelativePath) == false
		|| (InEntryBinaryRelativePath != InBundleRelativePath && InEntryBinaryRelativePath.StartsWith(InBundleRelativePath + TEXT("/")) == false)
		|| FVST3PayloadValidation::IsLowerHex(InContentHash, 64) == false || bInRedistributionRightsAcknowledged == false
		|| InComponents.ContainsByPredicate([](const FVST3ComponentDescriptor& Component) { return Component.IsSupportedEffect(); }) == false)
	{
		return false;
	}

	TSet<FString> ClassIds;
	for (const auto& Component : InComponents)
	{
		if (Component.IsValidClassId() == false || ClassIds.Contains(Component.ClassId) == true)
		{
			return false;
		}
		ClassIds.Add(Component.ClassId);
	}

	this->BundleRelativePath = InBundleRelativePath;
	this->EntryBinaryRelativePath = InEntryBinaryRelativePath;
	this->ContentHash = InContentHash;
	this->Components = InComponents;
#if WITH_EDITORONLY_DATA
	this->SourcePath = InSourcePath;
	this->bRedistributionRightsAcknowledged = bInRedistributionRightsAcknowledged;
#endif
	return true;
}

#if WITH_EDITOR
EDataValidationResult UVST3ModuleAsset::IsDataValid(FDataValidationContext& Context) const
{
	FString Bundle;
	FString Error;
	if (FVST3PayloadValidation::ResolveManagedBundle(this->BundleRelativePath, this->EntryBinaryRelativePath,
		this->ContentHash, Bundle, Error) == false)
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}

	if (this->Components.ContainsByPredicate([](const FVST3ComponentDescriptor& Component) { return Component.IsSupportedEffect(); }) == false)
	{
		Context.AddError(FText::FromString(TEXT("対応するVST3 Audio Effectがありません。")));
		return EDataValidationResult::Invalid;
	}
#if WITH_EDITORONLY_DATA
	if (this->bRedistributionRightsAcknowledged == false)
	{
		Context.AddError(FText::FromString(TEXT("VST3の配布権が確認されていません。")));
		return EDataValidationResult::Invalid;
	}
#endif
	return EDataValidationResult::Valid;
}
#endif

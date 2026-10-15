// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3MixerAssetFactory.h"

#include "VST3MixerAsset.h"

UVST3MixerAssetFactory::UVST3MixerAssetFactory()
{
	this->SupportedClass = UVST3MixerAsset::StaticClass();
	this->bCreateNew = true;
	this->bEditorImport = false;
	this->bEditAfterNew = true;
}

UObject* UVST3MixerAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
	const EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVST3MixerAsset>(InParent, InClass, InName, Flags | RF_Transactional);
}

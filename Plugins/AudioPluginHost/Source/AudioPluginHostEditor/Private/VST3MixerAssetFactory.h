// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "Factories/Factory.h"
#include "VST3MixerAssetFactory.generated.h"

UCLASS()
class UVST3MixerAssetFactory final : public UFactory
{
	GENERATED_BODY()

public:
	UVST3MixerAssetFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
		EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

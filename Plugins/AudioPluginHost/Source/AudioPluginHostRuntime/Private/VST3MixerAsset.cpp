// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3MixerAsset.h"

const TSoftObjectPtr<USoundSubmix>& UVST3MixerAsset::GetRootBus() const
{
	return this->RootBus;
}

const TArray<TSoftObjectPtr<USoundSubmix>>& UVST3MixerAsset::GetAdditionalBuses() const
{
	return this->AdditionalBuses;
}

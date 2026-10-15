// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VST3MixerAsset.generated.h"

class USoundSubmix;
class FVST3MixerModel;

/** ミキサーに所属するSubmixだけを記録する。音声処理の設定は各Submixとプリセットが保持する。 */
UCLASS(BlueprintType)
class AUDIOPLUGINHOSTRUNTIME_API UVST3MixerAsset final : public UDataAsset
{
	GENERATED_BODY()

public:
	const TSoftObjectPtr<USoundSubmix>& GetRootBus() const;
	const TArray<TSoftObjectPtr<USoundSubmix>>& GetAdditionalBuses() const;

private:
	friend class FVST3MixerModel;

	UPROPERTY(VisibleAnywhere, Category = "Mixer")
	TSoftObjectPtr<USoundSubmix> RootBus;

	UPROPERTY(VisibleAnywhere, Category = "Mixer")
	TArray<TSoftObjectPtr<USoundSubmix>> AdditionalBuses;
};

// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "VST3PluginState.generated.h"

/** 保存した状態と、それを受け入れられるバイナリ・クラスを一体で保持する。 */
USTRUCT()
struct AUDIOPLUGINHOSTRUNTIME_API FVST3PluginState
{
	GENERATED_BODY()

	static constexpr int32 CurrentVersion = 1;

	UPROPERTY()
	int32 Version = CurrentVersion;

	UPROPERTY()
	FString ModuleHash;

	UPROPERTY()
	FString ClassId;

	UPROPERTY()
	TArray<uint8> Component;

	UPROPERTY()
	TArray<uint8> Controller;

	bool IsEmpty() const
	{
		return this->Component.IsEmpty() == true && this->Controller.IsEmpty() == true;
	}

	bool Matches(const FString& Hash, const FString& Class) const
	{
		return this->Version == CurrentVersion && this->ModuleHash == Hash && this->ClassId == Class;
	}
};

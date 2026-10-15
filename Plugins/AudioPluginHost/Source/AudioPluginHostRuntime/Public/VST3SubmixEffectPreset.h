// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundEffectSubmix.h"
#include "VST3PluginState.h"
#include "VST3SubmixEffectPreset.generated.h"

class UVST3ModuleAsset;
struct FVST3SessionRequest;

/** VST3の選択と保存状態を保持する。SDK操作はセッションへ任せる。 */
UCLASS(BlueprintType, EditInlineNew)
class AUDIOPLUGINHOSTRUNTIME_API UVST3SubmixEffectPreset final : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	virtual bool CanFilter() const override;
	virtual bool HasAssetActions() const override;
	virtual FText GetAssetActionName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual USoundEffectPreset* CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const override;
	virtual FSoundEffectBase* CreateNewEffect() const override;
	virtual void Init() override;

	const UVST3ModuleAsset* GetModuleAsset() const;
	const FString& GetSelectedClassId() const;
	bool IsHostBypassed() const;
	bool HasRuntimeHostBypass() const;
	uint32 GetConfigurationRevision() const;
	bool BuildSessionRequest(FVST3SessionRequest& OutRequest, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "VST3")
	bool SetModuleAndComponent(UVST3ModuleAsset* InModuleAsset, const FString& InClassId);

	UFUNCTION(BlueprintCallable, Category = "VST3")
	void SetHostBypassed(bool bInHostBypassed);

	/** 再生中だけ使うバイパス。保存済みプリセットは変更しない。 */
	void SetRuntimeHostBypassed(bool bInHostBypassed);
	void ClearRuntimeHostBypassed();

	bool SetSerializedState(const FVST3PluginState& InState);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	UFUNCTION()
	TArray<FString> GetComponentClassIds() const;

	UPROPERTY(EditAnywhere, Category = "VST3")
	TSoftObjectPtr<UVST3ModuleAsset> ModuleAsset;

	UPROPERTY(EditAnywhere, Category = "VST3", meta = (GetOptions = "GetComponentClassIds"))
	FString SelectedClassId;

	UPROPERTY(EditAnywhere, Category = "VST3")
	bool bHostBypassed = false;

	bool bHasRuntimeHostBypass = false;
	bool bRuntimeHostBypassed = false;

	UPROPERTY()
	FVST3PluginState PluginState;

	/** 実行要求を作るときにロードし、プリセットの寿命中はGC参照を保持する。 */
	UPROPERTY(Transient)
	TObjectPtr<UVST3ModuleAsset> LoadedModule;

	uint32 ConfigurationRevision = 1;
};

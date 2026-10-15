// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VST3ModuleAsset.generated.h"

class FDataValidationContext;

/** VST3 factory が公開した、読み取り専用のコンポーネント情報。 */
USTRUCT(BlueprintType)
struct AUDIOPLUGINHOSTRUNTIME_API FVST3ComponentDescriptor
{
	GENERATED_BODY()

	/** VST3 TUID を小文字 32 桁の 16 進表記で保持する。 */
	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString ClassId;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString Name;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString Vendor;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString Version;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString Category;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	bool bIsAudioEffect = false;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	bool bHasEditor = false;

	/** component 初期化後に 32-bit float process を受け入れたかを記録する。 */
	UPROPERTY(VisibleAnywhere, Category = "VST3")
	bool bSupportsFloat32 = false;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	int32 MainInputChannels = 0;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	int32 MainOutputChannels = 0;

	bool IsValidClassId() const;
	bool IsSupportedEffect() const;
};

/**
 * プロジェクト管理下へ複製した VST3 モジュールの台帳アセット。
 * パスはプロジェクト Content を基準にして保存し、ランタイムでは管理ルート外を拒否する。
 */
UCLASS(BlueprintType)
class AUDIOPLUGINHOSTRUNTIME_API UVST3ModuleAsset final : public UDataAsset
{
	GENERATED_BODY()

public:
	const FString& GetBundleRelativePath() const;
	const FString& GetEntryBinaryRelativePath() const;
	const FString& GetContentHash() const;
	const TArray<FVST3ComponentDescriptor>& GetComponents() const;
	const FVST3ComponentDescriptor* FindComponent(const FString& InClassId) const;

#if WITH_EDITORONLY_DATA
	const FString& GetImportSourcePath() const;
#endif

	/** インポータ専用。検証済みのスナップショットを一括設定する。 */
	bool SetImportedModule(
		const FString& InBundleRelativePath,
		const FString& InEntryBinaryRelativePath,
		const FString& InContentHash,
		const TArray<FVST3ComponentDescriptor>& InComponents,
		const FString& InSourcePath,
		bool bInRedistributionRightsAcknowledged);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString BundleRelativePath;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	FString EntryBinaryRelativePath;

	/** 小文字 64 桁の SHA-256。モジュール全体の決定的な識別子。 */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "VST3")
	FString ContentHash;

	UPROPERTY(VisibleAnywhere, Category = "VST3")
	TArray<FVST3ComponentDescriptor> Components;

#if WITH_EDITORONLY_DATA
	/** 元ファイルの確認用。パッケージには含めない。 */
	UPROPERTY(VisibleAnywhere, Category = "Import Audit")
	FString SourcePath;

	UPROPERTY(VisibleAnywhere, Category = "Import Audit")
	bool bRedistributionRightsAcknowledged = false;
#endif

};

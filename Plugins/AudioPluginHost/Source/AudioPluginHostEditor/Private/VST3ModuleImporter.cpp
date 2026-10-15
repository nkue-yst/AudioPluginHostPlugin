// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3ModuleImporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "VST3ModuleAsset.h"
#include "VST3ModuleInspector.h"
#include "VST3PayloadValidation.h"

namespace AudioPluginHost::Import
{
	static FString ContentRelative(const FString& Path)
	{
		auto Result = Path;
		const auto ContentRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		FPaths::MakePathRelativeTo(Result, *ContentRoot);
		FPaths::NormalizeFilename(Result);
		return Result;
	}

	static UVST3ModuleAsset* FindByHash(const FString& Hash)
	{
		auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UVST3ModuleAsset::StaticClass()->GetClassPathName(), Assets);
		for (const auto& Asset : Assets)
		{
			FString AssetHash;
			if (Asset.GetTagValue(TEXT("ContentHash"), AssetHash) == true && AssetHash == Hash)
			{
				return Cast<UVST3ModuleAsset>(Asset.GetAsset());
			}
		}
		return nullptr;
	}
}

UVST3ModuleAsset* FVST3ModuleImporter::ImportExplicit(const FVST3ImportRequest& Request, FVST3ImportResult& OutResult)
{
	OutResult = {};
	FVST3ImportPlan Plan;

	if (FVST3ImportService::BuildPlan(Request, Plan, OutResult.Message) == false)
	{
		return nullptr;
	}

	const auto bExisted = IFileManager::Get().DirectoryExists(*Plan.ManagedBundlePath)
		|| IFileManager::Get().FileExists(*Plan.ManagedBundlePath);

	if (FVST3ImportService::CopyPayload(Plan, OutResult.Message) == false)
	{
		return nullptr;
	}

	ON_SCOPE_EXIT
	{
		if (bExisted == false && OutResult.bSucceeded == false)
		{
			const auto Directory = FPaths::GetPath(Plan.ManagedBundlePath);
			FString Error;
			if (FVST3PayloadValidation::IsManagedPathWithoutReparsePoints(Directory, Error) == true)
			{
				IFileManager::Get().DeleteDirectory(*Directory, false, true);
			}
		}
	};

	if (FVST3ImportService::ConfigurePackagingSettings(OutResult.Message) == false)
	{
		return nullptr;
	}

	if (auto* Existing = AudioPluginHost::Import::FindByHash(Plan.BundleSha256); Existing != nullptr)
	{
		OutResult.bSucceeded = true;
		OutResult.Message = TEXT("同一ハッシュのVST3モジュールを再利用しました。");
		return Existing;
	}

	TArray<FVST3ComponentDescriptor> Components;
	// コピー後に実際のロード先を検査し、元ファイルとの差や依存リソースの欠損を検出する。
	if (FVST3ModuleInspector::InspectModule(Plan.ManagedBundlePath, Components, OutResult.Message) == false)
	{
		return nullptr;
	}

	if (Components.ContainsByPredicate([](const FVST3ComponentDescriptor& Component) { return Component.IsSupportedEffect(); }) == false)
	{
		OutResult.Message = TEXT("ステレオfloat32に対応するVST3 Audio Effectがありません。");
		return nullptr;
	}

	auto& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	FString VerifiedBundle;
	if (FVST3PayloadValidation::ResolveManagedBundle(AudioPluginHost::Import::ContentRelative(Plan.ManagedBundlePath),
		AudioPluginHost::Import::ContentRelative(Plan.ManagedEntryBinaryPath), Plan.BundleSha256, VerifiedBundle, OutResult.Message) == false)
	{
		return nullptr;
	}

	FString PackageName;
	FString AssetName;
	AssetTools.CreateUniqueAssetName(TEXT("/Game/AudioPluginHost/Modules/VST3_") + Plan.BundleSha256.Left(12), TEXT(""), PackageName, AssetName);
	auto* Package = CreatePackage(*PackageName);
	auto* Module = NewObject<UVST3ModuleAsset>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (Module->SetImportedModule(AudioPluginHost::Import::ContentRelative(Plan.ManagedBundlePath),
		AudioPluginHost::Import::ContentRelative(Plan.ManagedEntryBinaryPath), Plan.BundleSha256, Components,
		Plan.SourceBundlePath, Request.bRedistributionRightsAcknowledged) == false)
	{
		Module->ClearFlags(RF_Public | RF_Standalone);
		Module->MarkAsGarbage();
		OutResult.Message = TEXT("検証済みのVST3情報をアセットへ保存できません。");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Module);
	Module->MarkPackageDirty();
	OutResult.bSucceeded = true;
	OutResult.Message = TEXT("VST3モジュールをインポートしました。");
	return Module;
}

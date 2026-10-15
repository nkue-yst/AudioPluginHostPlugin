// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3ImportService.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/Archive.h"
#include "Settings/ProjectPackagingSettings.h"
#include "VST3PayloadValidation.h"

namespace AudioPluginHost::Import
{
	static bool IsWin64Binary(const FString& Path)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
		if (Reader == nullptr || Reader->TotalSize() < 64)
		{
			return false;
		}

		uint8 Header[64] {};
		Reader->Serialize(Header, sizeof(Header));
		if (Reader->IsError() == true || Header[0] != 'M' || Header[1] != 'Z')
		{
			return false;
		}

		const auto Offset = static_cast<uint32>(Header[60]) | (static_cast<uint32>(Header[61]) << 8)
			| (static_cast<uint32>(Header[62]) << 16) | (static_cast<uint32>(Header[63]) << 24);
		if (Offset > Reader->TotalSize() - 26)
		{
			return false;
		}

		Reader->Seek(Offset);
		uint8 Pe[26] {};
		Reader->Serialize(Pe, sizeof(Pe));
		return Reader->IsError() == false && Pe[0] == 'P' && Pe[1] == 'E' && Pe[2] == 0 && Pe[3] == 0
			&& Pe[4] == 0x64 && Pe[5] == 0x86 && Pe[24] == 0x0b && Pe[25] == 0x02;
	}

	static bool ValidateCopy(const FVST3ImportPlan& Plan, const FString& Bundle, FString& OutError)
	{
		FString Hash;
		if (FVST3PayloadValidation::ComputeBundleHash(Bundle, Hash, OutError) == false)
		{
			return false;
		}

		if (Hash != Plan.BundleSha256)
		{
			OutError = TEXT("コピー元または配置済みVST3の内容が計画時のハッシュと一致しません。");
			return false;
		}
		return true;
	}
}

bool FVST3ImportService::BuildPlan(const FVST3ImportRequest& Request, FVST3ImportPlan& OutPlan, FString& OutError)
{
	OutPlan = {};
	if (Request.bRedistributionRightsAcknowledged == false)
	{
		OutError = TEXT("VST3の配布権を確認してください。");
		return false;
	}

	auto Source = FPaths::ConvertRelativePathToFull(Request.SourcePath);
	FPaths::NormalizeDirectoryName(Source);
	if (FPaths::GetExtension(Source).Equals(TEXT("vst3"), ESearchCase::IgnoreCase) == false)
	{
		OutError = TEXT(".vst3ファイルまたはバンドルディレクトリを選択してください。");
		return false;
	}

	TArray<FString> Files;
	if (FVST3PayloadValidation::EnumerateBundleFiles(Source, Files, OutError) == false)
	{
		return false;
	}

	const auto bDirectory = IFileManager::Get().DirectoryExists(*Source);
	FString Entry = Source;
	if (bDirectory == true)
	{
		// SDKがロードする標準配置を検査する。
		Entry = Source / TEXT("Contents/x86_64-win") / FPaths::GetCleanFilename(Source);
	}

	if (AudioPluginHost::Import::IsWin64Binary(Entry) == false)
	{
		OutError = TEXT("標準配置のWin64 x64 VST3エントリバイナリがありません。");
		return false;
	}

	FString Hash;
	if (FVST3PayloadValidation::ComputeBundleHash(Source, Hash, OutError) == false)
	{
		return false;
	}

	OutPlan.SourceBundlePath = Source;
	OutPlan.SourceEntryBinaryPath = Entry;
	OutPlan.BundleSha256 = Hash;
	OutPlan.bSourceIsDirectory = bDirectory;
	OutPlan.ManagedBundlePath = FVST3PayloadValidation::GetManagedPayloadRoot() / Hash / FPaths::GetCleanFilename(Source);
	OutPlan.ManagedEntryBinaryPath = bDirectory == true
		? OutPlan.ManagedBundlePath / FVST3PayloadValidation::GetBundleRelativePath(Source, Entry)
		: OutPlan.ManagedBundlePath;
	return true;
}

bool FVST3ImportService::CopyPayload(const FVST3ImportPlan& Plan, FString& OutError)
{
	if (FVST3PayloadValidation::IsLowerHex(Plan.BundleSha256, 64) == false
		|| FPaths::IsUnderDirectory(Plan.ManagedBundlePath, FVST3PayloadValidation::GetManagedPayloadRoot()) == false)
	{
		OutError = TEXT("VST3の配置計画が不正です。");
		return false;
	}

	if (IFileManager::Get().DirectoryExists(*Plan.ManagedBundlePath) == true || IFileManager::Get().FileExists(*Plan.ManagedBundlePath) == true)
	{
		return FVST3PayloadValidation::IsManagedPathWithoutReparsePoints(Plan.ManagedBundlePath, OutError)
			&& AudioPluginHost::Import::ValidateCopy(Plan, Plan.ManagedBundlePath, OutError);
	}

	const auto StagingRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AudioPluginHost/Import"));
	const auto StagingDirectory = StagingRoot / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const auto StagedBundle = StagingDirectory / FPaths::GetCleanFilename(Plan.SourceBundlePath);
	const auto DestinationDirectory = FPaths::GetPath(Plan.ManagedBundlePath);
	if (IFileManager::Get().MakeDirectory(*StagingDirectory, true) == false)
	{
		OutError = TEXT("VST3の一時コピー先を作成できません。");
		return false;
	}

	ON_SCOPE_EXIT
	{
		// この呼び出しで作成した一時ディレクトリだけを片付けます。
		if (FPaths::IsUnderDirectory(StagingDirectory, StagingRoot) == true)
		{
			IFileManager::Get().DeleteDirectory(*StagingDirectory, false, true);
		}
	};

	TArray<FString> Files;
	if (FVST3PayloadValidation::EnumerateBundleFiles(Plan.SourceBundlePath, Files, OutError) == false)
	{
		return false;
	}

	for (const auto& File : Files)
	{
		const auto Destination = Plan.bSourceIsDirectory == true
			? StagedBundle / FVST3PayloadValidation::GetBundleRelativePath(Plan.SourceBundlePath, File) : StagedBundle;
		if (IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true) == false
			|| IFileManager::Get().Copy(*Destination, *File, false, false) != COPY_OK)
		{
			OutError = FString::Printf(TEXT("VST3ファイルをコピーできません: %s"), *File);
			return false;
		}
	}

	if (AudioPluginHost::Import::ValidateCopy(Plan, StagedBundle, OutError) == false)
	{
		return false;
	}

	const auto ManagedRoot = FVST3PayloadValidation::GetManagedPayloadRoot();
	if (IFileManager::Get().MakeDirectory(*ManagedRoot, true) == false
		|| FVST3PayloadValidation::IsManagedPathWithoutReparsePoints(ManagedRoot, OutError) == false)
	{
		return false;
	}

	// 同じハッシュの配置先は上書きしない。
	if (IFileManager::Get().DirectoryExists(*DestinationDirectory) == true
		|| FPlatformFileManager::Get().GetPlatformFile().MoveFile(*DestinationDirectory, *StagingDirectory) == false)
	{
		OutError = TEXT("VST3の配置先が既に存在するか、検証済みコピーを移動できません。");
		return false;
	}
	return true;
}

bool FVST3ImportService::ConfigurePackagingSettings(FString& OutError)
{
	auto* Settings = GetMutableDefault<UProjectPackagingSettings>();
	const FDirectoryPath Modules
	{
		TEXT("/Game/AudioPluginHost/Modules")
	};
	const FDirectoryPath Payload
	{
		TEXT("AudioPluginHost/NativeModules")
	};

	bool bChanged = false;
	auto AddDirectory = [&bChanged](TArray<FDirectoryPath>& Directories, const FDirectoryPath& Value)
	{
		if (Directories.ContainsByPredicate([&Value](const FDirectoryPath& Existing)
			{ return Existing.Path.Equals(Value.Path, ESearchCase::IgnoreCase); }) == false)
		{
			Directories.Add(Value);
			bChanged = true;
		}
	};

	AddDirectory(Settings->DirectoriesToAlwaysCook, Modules);
	AddDirectory(Settings->DirectoriesToAlwaysStageAsNonUFS, Payload);

	if (bChanged == true && Settings->TryUpdateDefaultConfigFile() == false)
	{
		OutError = TEXT("VST3のパッケージ設定をDefaultGame.iniへ保存できません。");
		return false;
	}
	return true;
}

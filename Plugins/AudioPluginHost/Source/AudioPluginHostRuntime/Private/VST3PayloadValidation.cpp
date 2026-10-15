// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3PayloadValidation.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <bcrypt.h>
#endif

namespace AudioPluginHost::Payload
{
	static bool CheckPath(const FString& InPath, FString& OutError)
	{
#if PLATFORM_WINDOWS
		auto Path = FPaths::ConvertRelativePathToFull(InPath);
		FPaths::NormalizeDirectoryName(Path);
		for (;;)
		{
			const auto Attributes = GetFileAttributesW(*(Path.EndsWith(TEXT(":")) == true ? Path + TEXT("/") : Path));
			if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				OutError = FString::Printf(TEXT("VST3ペイロードの欠損またはリンクを検出しました: %s"), *Path);
				return false;
			}

			const auto Parent = FPaths::GetPath(Path);
			if (Parent.IsEmpty() == true || Parent == Path)
			{
				break;
			}
			Path = Parent;
		}
		return true;
#else
		OutError = TEXT("VST3ペイロードはWindowsでのみ使用できます。");
		return false;
#endif
	}

	static bool CollectFiles(const FString& Directory, TArray<FString>& OutFiles, FString& OutError)
	{
		// リンクを検査してから子ディレクトリへ進み、再帰探索で管理外へ抜けないようにする。
		return FPlatformFileManager::Get().GetPlatformFile().IterateDirectory(*Directory,
			[&OutFiles, &OutError](const TCHAR* Path, const bool bIsDirectory)
			{
				if (CheckPath(Path, OutError) == false)
				{
					return false;
				}

				if (bIsDirectory == true)
				{
					return CollectFiles(Path, OutFiles, OutError);
				}
				OutFiles.Add(Path);
				return true;
			});
	}

#if PLATFORM_WINDOWS
	class FSha256 final
	{
	public:
		~FSha256()
		{
			if (this->Hash != nullptr)
			{
				BCryptDestroyHash(this->Hash);
			}
			if (this->Algorithm != nullptr)
			{
				BCryptCloseAlgorithmProvider(this->Algorithm, 0);
			}
		}

		bool Initialize()
		{
			ULONG ObjectSize = 0;
			ULONG ResultSize = 0;
			if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&this->Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) == false
				|| BCRYPT_SUCCESS(BCryptGetProperty(this->Algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&ObjectSize), sizeof(ObjectSize), &ResultSize, 0)) == false)
			{
				return false;
			}

			this->Storage.SetNumUninitialized(ObjectSize);
			return BCRYPT_SUCCESS(BCryptCreateHash(this->Algorithm, &this->Hash, this->Storage.GetData(), ObjectSize, nullptr, 0, 0));
		}

		bool Append(const void* Data, const uint32 Size)
		{
			return BCRYPT_SUCCESS(BCryptHashData(this->Hash, const_cast<PUCHAR>(static_cast<const UCHAR*>(Data)), Size, 0));
		}

		bool AppendSize(const int64 Value)
		{
			uint8 Bytes[8] {};
			for (auto Index = 0; Index < 8; ++Index)
			{
				Bytes[Index] = static_cast<uint8>((Value >> (Index * 8)) & 0xff);
			}
			return this->Append(Bytes, sizeof(Bytes));
		}

		bool Finish(FString& OutHash)
		{
			uint8 Bytes[32] {};
			if (BCRYPT_SUCCESS(BCryptFinishHash(this->Hash, Bytes, sizeof(Bytes), 0)) == false)
			{
				return false;
			}

			OutHash = BytesToHex(Bytes, UE_ARRAY_COUNT(Bytes)).ToLower();
			return true;
		}

	private:
		BCRYPT_ALG_HANDLE Algorithm = nullptr;
		BCRYPT_HASH_HANDLE Hash = nullptr;
		TArray<uint8> Storage;
	};
#endif
}

bool FVST3PayloadValidation::IsLowerHex(const FString& Value, const int32 Length)
{
	if (Value.Len() != Length)
	{
		return false;
	}

	for (const auto Character : Value)
	{
		if ((Character < TEXT('0') || Character > TEXT('9')) && (Character < TEXT('a') || Character > TEXT('f')))
		{
			return false;
		}
	}
	return true;
}

bool FVST3PayloadValidation::IsSafeManagedRelativePath(const FString& Path)
{
	if (Path.Contains(TEXT(":")) == true || Path.Contains(TEXT("\\")) == true
		|| Path.StartsWith(TEXT("AudioPluginHost/NativeModules/"), ESearchCase::CaseSensitive) == false)
	{
		return false;
	}

	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("/"), false);
	return Segments.ContainsByPredicate([](const FString& Segment)
	{
		return Segment.IsEmpty() == true || Segment == TEXT(".") || Segment == TEXT("..");
	}) == false;
}

FString FVST3PayloadValidation::GetManagedPayloadRoot()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("AudioPluginHost/NativeModules"));
}

FString FVST3PayloadValidation::GetBundleRelativePath(const FString& BundlePath, const FString& FilePath)
{
	if (IFileManager::Get().DirectoryExists(*BundlePath) == false)
	{
		return FPaths::GetCleanFilename(FilePath);
	}

	auto Relative = FPaths::ConvertRelativePathToFull(FilePath);
	auto Root = FPaths::ConvertRelativePathToFull(BundlePath);
	FPaths::NormalizeDirectoryName(Root);
	FPaths::MakePathRelativeTo(Relative, *(Root + TEXT("/")));
	FPaths::NormalizeFilename(Relative);
	return Relative;
}

bool FVST3PayloadValidation::IsManagedPathWithoutReparsePoints(const FString& AbsolutePath, FString& OutError)
{
	const auto Candidate = FPaths::ConvertRelativePathToFull(AbsolutePath);
	const auto Root = GetManagedPayloadRoot();
	if (FPaths::IsUnderDirectory(Candidate, Root) == false && FPaths::IsSamePath(Candidate, Root) == false)
	{
		OutError = TEXT("VST3ペイロードがプロジェクトの管理ルート外を指しています。");
		return false;
	}

	return AudioPluginHost::Payload::CheckPath(Candidate, OutError);
}

bool FVST3PayloadValidation::EnumerateBundleFiles(const FString& BundlePath, TArray<FString>& OutFiles, FString& OutError)
{
	OutFiles.Reset();

	if (AudioPluginHost::Payload::CheckPath(BundlePath, OutError) == false)
	{
		return false;
	}
	if (IFileManager::Get().DirectoryExists(*BundlePath) == true)
	{
		if (AudioPluginHost::Payload::CollectFiles(BundlePath, OutFiles, OutError) == false)
		{
			return false;
		}
	}
	else
	{
		OutFiles.Add(BundlePath);
	}

	if (OutFiles.IsEmpty() == true)
	{
		OutError = TEXT("VST3バンドルにファイルがありません。");
		return false;
	}

	OutFiles.Sort([&BundlePath](const FString& Left, const FString& Right)
	{
		return GetBundleRelativePath(BundlePath, Left).Compare(GetBundleRelativePath(BundlePath, Right), ESearchCase::IgnoreCase) < 0;
	});
	return true;
}

bool FVST3PayloadValidation::ComputeBundleHash(const FString& BundlePath, FString& OutHash, FString& OutError)
{
	OutHash.Reset();
	TArray<FString> Files;

	if (EnumerateBundleFiles(BundlePath, Files, OutError) == false)
	{
		return false;
	}
#if PLATFORM_WINDOWS
	AudioPluginHost::Payload::FSha256 Hash;

	if (Hash.Initialize() == false)
	{
		OutError = TEXT("SHA-256を初期化できません。");
		return false;
	}

	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(64 * 1024);
	for (const auto& File : Files)
	{
		const auto Relative = GetBundleRelativePath(BundlePath, File).ToLower();
		const auto Utf8 = StringCast<UTF8CHAR>(*Relative);
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
		if (Reader == nullptr || Reader->TotalSize() < 0
			|| Hash.AppendSize(Utf8.Length()) == false || Hash.Append(Utf8.Get(), Utf8.Length()) == false
			|| Hash.AppendSize(Reader->TotalSize()) == false)
		{
			OutError = FString::Printf(TEXT("VST3ファイルをハッシュへ追加できません: %s"), *File);
			return false;
		}

		auto Remaining = Reader->TotalSize();
		while (Remaining > 0)
		{
			const auto Count = static_cast<int32>(FMath::Min<int64>(Remaining, Buffer.Num()));
			Reader->Serialize(Buffer.GetData(), Count);
			if (Reader->IsError() == true || Hash.Append(Buffer.GetData(), Count) == false)
			{
				OutError = FString::Printf(TEXT("VST3ファイルの読み取りに失敗しました: %s"), *File);
				return false;
			}
			Remaining -= Count;
		}
	}

	if (Hash.Finish(OutHash) == true)
	{
		return true;
	}
#endif
	OutError = TEXT("VST3ペイロードのSHA-256を計算できません。");
	return false;
}

bool FVST3PayloadValidation::ResolveManagedBundle(const FString& BundleRelativePath, const FString& EntryRelativePath,
	const FString& ExpectedHash, FString& OutBundlePath, FString& OutError)
{
	OutBundlePath.Reset();
	if (IsSafeManagedRelativePath(BundleRelativePath) == false || IsSafeManagedRelativePath(EntryRelativePath) == false
		|| IsLowerHex(ExpectedHash, 64) == false)
	{
		OutError = TEXT("VST3の管理相対パスまたはSHA-256が不正です。");
		return false;
	}

	const auto Bundle = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / BundleRelativePath);
	const auto Entry = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / EntryRelativePath);
	if (IsManagedPathWithoutReparsePoints(Bundle, OutError) == false || IsManagedPathWithoutReparsePoints(Entry, OutError) == false)
	{
		return false;
	}

	if ((FPaths::IsSamePath(Bundle, Entry) == false && FPaths::IsUnderDirectory(Entry, Bundle) == false)
		|| IFileManager::Get().FileExists(*Entry) == false)
	{
		OutError = TEXT("VST3エントリバイナリがバンドル内にありません。");
		return false;
	}

	FString Hash;
	if (ComputeBundleHash(Bundle, Hash, OutError) == false)
	{
		return false;
	}

	if (Hash != ExpectedHash)
	{
		OutError = TEXT("VST3ペイロードのSHA-256がアセットと一致しません。再インポートしてください。");
		return false;
	}

	OutBundlePath = Bundle;
	return true;
}

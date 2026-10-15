// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"

/** インポートとロードで同じパス・ハッシュ規則を使う。 */
class AUDIOPLUGINHOSTRUNTIME_API FVST3PayloadValidation final
{
public:
	static bool IsLowerHex(const FString& Value, int32 Length);
	static bool IsSafeManagedRelativePath(const FString& Path);
	static FString GetManagedPayloadRoot();
	static FString GetBundleRelativePath(const FString& BundlePath, const FString& FilePath);
	static bool IsManagedPathWithoutReparsePoints(const FString& AbsolutePath, FString& OutError);
	static bool EnumerateBundleFiles(const FString& BundlePath, TArray<FString>& OutFiles, FString& OutError);
	static bool ComputeBundleHash(const FString& BundlePath, FString& OutHash, FString& OutError);
	static bool ResolveManagedBundle(const FString& BundleRelativePath, const FString& EntryRelativePath,
		const FString& ExpectedHash, FString& OutBundlePath, FString& OutError);
};

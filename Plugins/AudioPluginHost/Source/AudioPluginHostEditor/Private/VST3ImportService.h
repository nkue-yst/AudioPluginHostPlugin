// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"

struct FVST3ImportRequest
{
	FString SourcePath;
	bool bRedistributionRightsAcknowledged = false;
};

/** コピー元と配置先を固定する計画。 */
struct FVST3ImportPlan
{
	FString SourceBundlePath;
	FString SourceEntryBinaryPath;
	FString BundleSha256;
	FString ManagedBundlePath;
	FString ManagedEntryBinaryPath;
	bool bSourceIsDirectory = false;
};

struct FVST3ImportResult
{
	FString Message;
	bool bSucceeded = false;
};

/** SDKを呼ばず、ファイル検証・コピー・パッケージ設定だけを扱います。 */
class FVST3ImportService final
{
public:
	static bool BuildPlan(const FVST3ImportRequest& Request, FVST3ImportPlan& OutPlan, FString& OutError);
	static bool CopyPayload(const FVST3ImportPlan& Plan, FString& OutError);
	static bool ConfigurePackagingSettings(FString& OutError);
};

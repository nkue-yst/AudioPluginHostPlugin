// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "VST3ModuleAsset.h"

/** 実行時と同じ初期化処理で対応形式を調べる。SDKの型は公開しない。 */
class AUDIOPLUGINHOSTVST3_API FVST3ModuleInspector final
{
public:
	/**
	 * ゲームスレッドからバンドルまたは単一ファイルを指定する。
	 * 各Audio Effectを初期化し、ステレオfloat32とネイティブビューへの対応を調べます。
	 */
	static bool InspectModule(const FString& InBundlePath, TArray<FVST3ComponentDescriptor>& OutComponents, FString& OutError);
};

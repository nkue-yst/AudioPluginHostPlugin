// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "VST3ImportService.h"

class UVST3ModuleAsset;

/** 検証済みペイロードを、ハッシュごとの変更しないアセットとして登録する。 */
class FVST3ModuleImporter final
{
public:
	static UVST3ModuleAsset* ImportExplicit(const FVST3ImportRequest& Request, FVST3ImportResult& OutResult);
};

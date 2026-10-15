// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "VST3Processor.h"

class FVST3ProcessorFactory final : public IVST3AudioProcessorFactory
{
public:
	virtual TUniquePtr<IVST3AudioProcessor> CreateProcessor(const FVST3SessionRequest& Request, FString& OutError) override;
};

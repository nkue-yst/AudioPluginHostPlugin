// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Containers/Ticker.h"

class AUDIOPLUGINHOSTRUNTIME_API FAudioPluginHostRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

private:
	bool TickGameThread(float DeltaTime);

	FTSTicker::FDelegateHandle TickerHandle;
};

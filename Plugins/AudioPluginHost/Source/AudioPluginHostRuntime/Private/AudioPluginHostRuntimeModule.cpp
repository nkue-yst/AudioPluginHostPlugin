// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "AudioPluginHostRuntimeModule.h"

#include "Modules/ModuleManager.h"
#include "VST3AudioSession.h"

void FAudioPluginHostRuntimeModule::StartupModule()
{
	this->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAudioPluginHostRuntimeModule::TickGameThread));
}

void FAudioPluginHostRuntimeModule::ShutdownModule()
{
	if (this->TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(this->TickerHandle);
		this->TickerHandle.Reset();
	}

	FVST3SessionService::Get().Shutdown();
}

bool FAudioPluginHostRuntimeModule::TickGameThread(const float DeltaTime)
{
	FVST3SessionService::Get().TickGameThread();
	return true;
}

IMPLEMENT_MODULE(FAudioPluginHostRuntimeModule, AudioPluginHostRuntime)

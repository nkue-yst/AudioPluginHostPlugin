// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "Modules/ModuleManager.h"
#include "VST3AudioSession.h"
#include "VST3ProcessorFactory.h"

class FAudioPluginHostVST3Module final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FVST3SessionService::Get().RegisterFactory(MakeUnique<FVST3ProcessorFactory>());
	}

	virtual void ShutdownModule() override
	{
		// 実装を含むDLLがアンロードされる前に、全プロセッサをゲームスレッドで破棄する。
		FVST3SessionService::Get().Shutdown();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}
};

IMPLEMENT_MODULE(FAudioPluginHostVST3Module, AudioPluginHostVST3)

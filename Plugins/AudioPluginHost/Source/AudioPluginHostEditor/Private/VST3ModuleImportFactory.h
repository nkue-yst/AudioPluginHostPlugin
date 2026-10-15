// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VST3ModuleImportFactory.generated.h"

/** Content Browserからの明示VST3インポートを受けるFactory。 */
UCLASS()
class UVST3ModuleImportFactory final : public UFactory
{
	GENERATED_BODY()

public:
	UVST3ModuleImportFactory();

	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

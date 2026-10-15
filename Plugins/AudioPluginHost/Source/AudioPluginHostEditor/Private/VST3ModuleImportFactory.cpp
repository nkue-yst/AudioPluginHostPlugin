// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3ModuleImportFactory.h"

#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "VST3ModuleAsset.h"
#include "VST3ModuleImporter.h"

UVST3ModuleImportFactory::UVST3ModuleImportFactory()
{
	this->bCreateNew = false;
	this->bEditorImport = true;
	this->SupportedClass = UVST3ModuleAsset::StaticClass();
	this->Formats.Add(TEXT("vst3;VST3 Plugin"));
}

bool UVST3ModuleImportFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("vst3"), ESearchCase::IgnoreCase);
}

UObject* UVST3ModuleImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	static_cast<void>(InClass);
	static_cast<void>(InParent);
	static_cast<void>(InName);
	static_cast<void>(Flags);
	static_cast<void>(Parms);
	static_cast<void>(Warn);

	const auto Response = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("このVST3をプロジェクトへコピーし、DevelopmentとShippingのパッケージへ常に含めます。\n配布・再配布する権利を確認済みですか？")));
	if (Response != EAppReturnType::Yes)
	{
		bOutOperationCanceled = true;
		return nullptr;
	}

	FVST3ImportRequest Request;
	Request.SourcePath = Filename;
	Request.bRedistributionRightsAcknowledged = true;
	FVST3ImportResult Result;
	auto* Asset = FVST3ModuleImporter::ImportExplicit(Request, Result);
	if (Asset == nullptr)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
	}
	return Asset;
}

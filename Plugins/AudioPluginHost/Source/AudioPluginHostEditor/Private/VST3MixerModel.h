// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FScopedTransaction;
class USoundSubmix;
class USoundSubmixBase;
class USoundBase;
class UVST3ModuleAsset;
class UVST3MixerAsset;
class UVST3SubmixEffectPreset;

/** UEのSubmix routingとEffect Chainを編集する、Slateに依存しないMixerモデル。 */
class FVST3MixerModel final
{
public:
	explicit FVST3MixerModel(UVST3MixerAsset* InMixer);
	~FVST3MixerModel();

	void Refresh();
	const TArray<TWeakObjectPtr<USoundSubmix>>& GetBuses() const;
	UVST3MixerAsset* GetMixer() const;
	USoundSubmix* GetRootBus() const;
	USoundSubmix* GetMasterBus() const;
	bool CanEditStructure() const;
	bool ValidateEditScope(FText& OutError) const;
	bool HasSharedBuses() const;

	bool CreateRootBus(const FString& Name, USoundSubmix*& OutBus, FText& OutError);
	bool CreateBus(const FString& Name, USoundSubmix* Parent, USoundSubmix*& OutBus, FText& OutError);
	bool AdoptExistingBus(USoundSubmix* Bus, FText& OutError);
	bool RemoveBusFromMixer(USoundSubmix* Bus, FText& OutError);
	bool ResetBusReferences(FText& OutError);
	bool MakeIndependent(FText& OutError);
	bool SaveMixerAndDependencies(FText& OutError);
	bool PrepareToSave(FText& OutError);
	void GetSaveableObjects(TArray<UObject*>& OutObjects) const;
	bool SetMasterBus(USoundSubmix* Bus, FText& OutError);
	bool SetParentBus(USoundSubmix* Bus, USoundSubmix* Parent, FText& OutError);
	bool AddExistingPreset(USoundSubmix* Bus, UVST3SubmixEffectPreset* Preset, int32 Index, FText& OutError);
	bool CreatePresetAndInsert(USoundSubmix* Bus, UVST3ModuleAsset* Module, const FString& ClassId,
		UVST3SubmixEffectPreset*& OutPreset, FText& OutError);
	bool RemoveInsert(USoundSubmix* Bus, int32 Index, FText& OutError);
	bool MoveInsert(USoundSubmix* Bus, int32 From, int32 To, FText& OutError);
	bool SetPresetBypassed(UVST3SubmixEffectPreset* Preset, bool bBypass, FText& OutError);
	bool BeginVolumeEdit(USoundSubmix* Bus, FText& OutError);
	bool SetVolumeDb(USoundSubmix* Bus, float Db, FText& OutError);
	void EndVolumeEdit(USoundSubmix* Bus);
	bool RouteSelectedSounds(USoundSubmix* Bus, int32& OutCount, FText& OutError);
	bool RouteSounds(USoundSubmix* Bus, const TArray<USoundBase*>& Sounds, int32& OutCount, FText& OutError);

private:
	bool ValidateBusForEdit(USoundSubmix* Bus, FText& OutError) const;
	void RefreshEditScope();
	void TrackAssetForSave(UObject* Asset);
	TStrongObjectPtr<UVST3MixerAsset> Mixer;
	TArray<TWeakObjectPtr<USoundSubmix>> Buses;
	TArray<TStrongObjectPtr<USoundSubmix>> LoadedBuses;
	// 所属や挿入位置を外した後も、変更済みアセットを保存できるよう保持する。
	TArray<TStrongObjectPtr<UObject>> PendingSaveAssets;
	TUniquePtr<FScopedTransaction> VolumeTransaction;
	TWeakObjectPtr<USoundSubmix> VolumeBus;
	FText ScopeError;
	bool bHasSharedBuses = false;
};

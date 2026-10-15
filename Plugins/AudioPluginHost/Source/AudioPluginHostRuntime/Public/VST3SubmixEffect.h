// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundEffectSubmix.h"

class FVST3AudioSession;
class UVST3SubmixEffectPreset;

/** Unrealのバッファをセッションへ渡すだけのSubmix Effect。 */
class AUDIOPLUGINHOSTRUNTIME_API FVST3SubmixEffect final : public FSoundEffectSubmix
{
public:
	explicit FVST3SubmixEffect(TWeakObjectPtr<UVST3SubmixEffectPreset> InPreset);
	virtual ~FVST3SubmixEffect() override;
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

private:
	virtual void Init(const FSoundEffectSubmixInitData& InInitData) override;

	TWeakObjectPtr<UVST3SubmixEffectPreset> SourcePreset;
	TSharedPtr<FVST3AudioSession, ESPMode::ThreadSafe> Session;
};

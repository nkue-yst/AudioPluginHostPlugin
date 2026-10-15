// Copyright (c) 2026 Y.Nakaue. MIT License.

#include "VST3SubmixEffect.h"

#include "VST3AudioSession.h"
#include "VST3SubmixEffectPreset.h"

FVST3SubmixEffect::FVST3SubmixEffect(TWeakObjectPtr<UVST3SubmixEffectPreset> InPreset)
	: SourcePreset(InPreset)
{
}

FVST3SubmixEffect::~FVST3SubmixEffect()
{
	if (this->Session.IsValid() == true)
	{
		this->Session->ReleaseFromEffect();
	}
}

void FVST3SubmixEffect::Init(const FSoundEffectSubmixInitData& InInitData)
{
	if (this->Session.IsValid() == true)
	{
		this->Session->ReleaseFromEffect();
	}

	this->Session = FVST3SessionService::Get().CreateSession(this->SourcePreset, FMath::RoundToInt(InInitData.SampleRate), InInitData.DeviceID);
}

void FVST3SubmixEffect::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	if (OutData.AudioBuffer == nullptr)
	{
		return;
	}

	auto& Output = *OutData.AudioBuffer;
	const auto* Input = InData.AudioBuffer;
	const auto CopyCount = Input != nullptr ? FMath::Min(Input->Num(), Output.Num()) : 0;
	if (CopyCount > 0 && Input->GetData() != Output.GetData())
	{
		FMemory::Memcpy(Output.GetData(), Input->GetData(), sizeof(float) * CopyCount);
	}

	if (CopyCount < Output.Num())
	{
		FMemory::Memzero(Output.GetData() + CopyCount, sizeof(float) * (Output.Num() - CopyCount));
	}

	if (InData.NumChannels != 2 || OutData.NumChannels != 2 || InData.NumFrames <= 0
		|| InData.NumFrames > CopyCount / 2 || this->Session.IsValid() == false)
	{
		return;
	}

	this->Session->ProcessStereo(Output.GetData(), Input->GetData(), InData.NumFrames);
}

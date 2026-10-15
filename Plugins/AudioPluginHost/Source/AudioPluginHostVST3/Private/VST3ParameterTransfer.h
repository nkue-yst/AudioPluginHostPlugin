// Copyright (c) 2026 Y.Nakaue. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "VST3Processor.h"
#include <atomic>

#if AUDIOPLUGINHOST_WITH_VST3SDK
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace AudioPluginHost::VST3
{
	/** 各パラメーターの最終値だけを配送する。列挙と確保は初期化時に完了する。 */
	class FParameterMailbox final
	{
	public:
		void Initialize(const TArray<uint32>& SortedIds)
		{
			this->Ids = SortedIds;
			this->Values = MakeUnique<FValue[]>(this->Ids.Num());
		}

		bool Submit(const FVST3ParameterChange& Change)
		{
			if (FMath::IsFinite(Change.NormalizedValue) == false || Change.NormalizedValue < 0.0 || Change.NormalizedValue > 1.0)
			{
				return false;
			}

			const auto Index = Algo::LowerBound(this->Ids, Change.ParameterId);
			if (this->Ids.IsValidIndex(Index) == false || this->Ids[Index] != Change.ParameterId)
			{
				return false;
			}

			uint64 Bits = 0;
			FMemory::Memcpy(&Bits, &Change.NormalizedValue, sizeof(Bits));
			this->Values[Index].Bits.store(Bits);
			this->Values[Index].bDirty.store(true);
			return true;
		}

		bool HasPending() const
		{
			for (auto Index = 0; Index < this->Ids.Num(); ++Index)
			{
				if (this->Values[Index].bDirty.load() == true)
				{
					return true;
				}
			}
			return false;
		}

		void Drain(TFunctionRef<void(const FVST3ParameterChange&)> Consumer)
		{
			for (auto Index = 0; Index < this->Ids.Num(); ++Index)
			{
				if (this->Values[Index].bDirty.exchange(false) == true)
				{
					const auto Bits = this->Values[Index].Bits.load();
					double Value = 0.0;
					FMemory::Memcpy(&Value, &Bits, sizeof(Value));
					Consumer({ this->Ids[Index], Value });
				}
			}
		}

	private:
		struct FValue
		{
			std::atomic<uint64> Bits
			{
				0
			};
			std::atomic<bool> bDirty
			{
				false
			};
		};

		TArray<uint32> Ids;
		TUniquePtr<FValue[]> Values;
	};

	/** このホストはブロック単位の値だけを扱うため、各キューは最終点を一つ保持する。 */
	class FParameterValueQueue final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Vst::IParamValueQueue>>
	{
	public:
		Steinberg::Vst::ParamID Id = 0;
		int32 ActiveIndex = INDEX_NONE;

		virtual Steinberg::Vst::ParamID PLUGIN_API getParameterId() override
		{
			return this->Id;
		}

		virtual Steinberg::int32 PLUGIN_API getPointCount() override
		{
			return this->bHasPoint == true ? 1 : 0;
		}

		virtual Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 Index, Steinberg::int32& Offset, Steinberg::Vst::ParamValue& Value) override
		{
			if (Index != 0 || this->bHasPoint == false)
			{
				return Steinberg::kInvalidArgument;
			}

			Offset = this->SampleOffset;
			Value = this->NormalizedValue;
			return Steinberg::kResultOk;
		}

		virtual Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 Offset, Steinberg::Vst::ParamValue Value, Steinberg::int32& Index) override
		{
			if (Offset < 0 || FMath::IsFinite(Value) == false || Value < 0.0 || Value > 1.0)
			{
				Index = -1;
				return Steinberg::kInvalidArgument;
			}

			this->SampleOffset = Offset;
			this->NormalizedValue = Value;
			this->bHasPoint = true;
			Index = 0;
			return Steinberg::kResultOk;
		}

		void Clear()
		{
			this->bHasPoint = false;
			this->ActiveIndex = INDEX_NONE;
		}

	private:
		int32 SampleOffset = 0;
		double NormalizedValue = 0.0;
		bool bHasPoint = false;
	};

	/** 列挙済みIDだけを受け付け、process中に配列を拡張しない。 */
	class FParameterChanges final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Vst::IParameterChanges>>
	{
	public:
		void Initialize(const TArray<uint32>& SortedIds)
		{
			this->Ids = SortedIds;
			this->Queues = MakeUnique<FParameterValueQueue[]>(this->Ids.Num());
			this->ActiveIndices.SetNumUninitialized(this->Ids.Num());

			for (auto Index = 0; Index < this->Ids.Num(); ++Index)
			{
				this->Queues[Index].Id = this->Ids[Index];
			}
		}

		virtual Steinberg::int32 PLUGIN_API getParameterCount() override
		{
			return this->Count;
		}

		virtual Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 Index) override
		{
			return Index >= 0 && Index < this->Count ? &this->Queues[this->ActiveIndices[Index]] : nullptr;
		}

		virtual Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& Id, Steinberg::int32& Index) override
		{
			const auto Slot = Algo::LowerBound(this->Ids, Id);
			if (this->Ids.IsValidIndex(Slot) == false || this->Ids[Slot] != Id)
			{
				Index = -1;
				return nullptr;
			}

			auto& Queue = this->Queues[Slot];
			if (Queue.ActiveIndex == INDEX_NONE)
			{
				Queue.ActiveIndex = this->Count;
				this->ActiveIndices[this->Count++] = Slot;
			}
			Index = Queue.ActiveIndex;
			return &Queue;
		}

		void Clear()
		{
			for (auto Index = 0; Index < this->Count; ++Index)
			{
				this->Queues[this->ActiveIndices[Index]].Clear();
			}
			this->Count = 0;
		}

	private:
		TArray<uint32> Ids;
		TUniquePtr<FParameterValueQueue[]> Queues;
		TArray<int32> ActiveIndices;
		int32 Count = 0;
	};
}
#endif

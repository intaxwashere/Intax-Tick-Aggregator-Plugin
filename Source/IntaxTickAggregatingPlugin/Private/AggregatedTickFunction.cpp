// Copyright © Eren Kaş
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted (subject to the limitations in the disclaimer
// below) provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
// 
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
// THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
// NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "AggregatedTickFunction.h"
#include "TickAggregatorInterface.h"

DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Total Tick Time"), STAT_TickAggregator_Tick, STATGROUP_TickAggregator);
DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Remove Objects"), STAT_TickAggregator_RemoveObjects, STATGROUP_TickAggregator);
DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Iterate Object Map"), STAT_TickAggregator_IterateObjectMap, STATGROUP_TickAggregator);
DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Unordered Ticks"), STAT_TickAggregator_UnorderedTicks, STATGROUP_TickAggregator);

bool FAggregatedTickFunction::AddNewObject(UObject* Object)
{
	if (IsValid(Object))
	{
		const UClass* Class = Object->GetClass();
		if (DoesObjectImplementTickFunction(Object))
		{
			FTickAggregatorObjectArray& NativeObjectArray = NativeAggregatedObjectTickElements.FindOrAdd(Class);
			return NativeObjectArray.Add(Object) != INDEX_NONE;
		}
		else
		{
			FTickAggregatorObjectArray& NativeObjectArray = NativeAggregatedObjectTickElements.FindOrAdd(Class);
			FTickAggregatorObjectArray& BlueprintObjectArray = BlueprintAggregatedObjectTickElements.FindOrAdd(Class);
			return NativeObjectArray.Add(Object) != INDEX_NONE && BlueprintObjectArray.Add(Object) != INDEX_NONE;
		}
		
	}
	return false;
}

bool FAggregatedTickFunction::RemoveObject(UObject* Object)
{
	if (IsValid(Object))
	{
		if (DoesObjectImplementTickFunction(Object))
		{
			return BlueprintObjectsToRemove.RemoveSwap(Object) != INDEX_NONE && NativeObjectsToRemove.RemoveSwap(Object) != INDEX_NONE;
		}
		else
		{
			//const UClass* Class = Object->GetClass();
			//FTickAggregatorObjectArray* ObjectArray = AggregatedObjectTickElements.Find(Class);
			return NativeObjectsToRemove.RemoveSwap(Object) != INDEX_NONE;
		}
	}
	return false;
}

bool FAggregatedTickFunction::AddNewUnorderedObject(UObject* Object)
{
	if (DoesObjectImplementTickFunction(Object))
	{
		return BlueprintUnorderedObjectTickElements.Add(Object) > INDEX_NONE && NativeUnorderedObjectTickElements.Add(Object) > INDEX_NONE;
	}
	else
	{
		return NativeUnorderedObjectTickElements.Add(Object) > INDEX_NONE;
	}
}

bool FAggregatedTickFunction::RemoveUnorderedObject(UObject* Object)
{
	if (DoesObjectImplementTickFunction(Object))
	{
		return BlueprintUnorderedObjectTickElements.Remove(Object) > 0 && NativeUnorderedObjectTickElements.Remove(Object) > 0;
	}
	else
	{
		return NativeUnorderedObjectTickElements.Remove(Object) > 0;
	}
		
}

bool FAggregatedTickFunction::RemoveObjectOnNextTick(UObject* Object)
{
	if (DoesObjectImplementTickFunction(Object))
	{
		return BlueprintObjectsToRemove.Add(Object) != INDEX_NONE && NativeObjectsToRemove.Add(Object) != INDEX_NONE;;
	}

	return NativeObjectsToRemove.Add(Object) != INDEX_NONE;
}

bool FAggregatedTickFunction::RemoveUnorderedObjectOnNextTick(UObject* Object)
{
	if (DoesObjectImplementTickFunction(Object))
	{
		return BlueprintObjectsToRemoveUnordered.Add(Object) != INDEX_NONE && NativeObjectsToRemove.Add(Object) != INDEX_NONE;;
	}

	return NativeObjectsToRemoveUnordered.Add(Object) != INDEX_NONE;
}

bool FAggregatedTickFunction::DoesObjectImplementTickFunction(UObject* Object) const
{
	const UClass* Class = Object->GetClass();
	const UFunction* Func =  Class->ClassGeneratedBy != nullptr ? Class->FindFunctionByName("BlueprintAggregatedTick", EIncludeSuperFlag::ExcludeSuper) : nullptr;
	return IsValid(Func) ? Func->GetOuter() == Class : false;
}

void FAggregatedTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
                                         const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_TickAggregator_Tick);

	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_RemoveObjects);
		
		for (UObject* Object : NativeObjectsToRemove)
		{
			AActor* Actor = Cast<AActor>(Object);
			if (IsValid(Actor)) 
			{
				Actor->Destroy();
			}
			else // if actor already destroyed itself, ignore.
			{
				Object->MarkAsGarbage();
			}

			FTickAggregatorObjectArray* ObjectArray = NativeAggregatedObjectTickElements.Find(Object->GetClass());
			ObjectArray->RemoveSwap(Object);
		}
		
		for (UObject* Object : BlueprintObjectsToRemove)
		{
			AActor* Actor = Cast<AActor>(Object);
			if (IsValid(Actor)) 
			{
				Actor->Destroy();
			}
			else // if actor already destroyed itself, ignore.
			{
				Object->MarkAsGarbage();
			}

			FTickAggregatorObjectArray* ObjectArray = NativeAggregatedObjectTickElements.Find(Object->GetClass());
			ObjectArray->RemoveSwap(Object);
		}

		for (UObject* Object : NativeObjectsToRemoveUnordered)
		{
			NativeUnorderedObjectTickElements.RemoveSwap(Object);
		}
		
		for (UObject* Object : BlueprintObjectsToRemoveUnordered)
		{
			BlueprintUnorderedObjectTickElements.RemoveSwap(Object);
		}
	}
	
	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_IterateObjectMap);
		
		for (const TTuple<TSoftClassPtr<UClass>, FTickAggregatorObjectArray>& ObjectMapPair : NativeAggregatedObjectTickElements)
		{
			const TArray<TWeakObjectPtr<UObject>>& ObjectArray = ObjectMapPair.Value.Get();
			for (TWeakObjectPtr<UObject> Object : ObjectArray)
			{
				if (Object.IsValid())
				{
					ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(Object);
					Interface->AggregatedTick(DeltaTime);
				}
			}
		}

		for (const TTuple<TSoftClassPtr<UClass>, FTickAggregatorObjectArray>& ObjectMapPair : BlueprintAggregatedObjectTickElements)
		{
			const TArray<TWeakObjectPtr<UObject>>& ObjectArray = ObjectMapPair.Value.Get();
			for (TWeakObjectPtr<UObject> Object : ObjectArray)
			{
				if (UObject* ObjectPtr = Object.Get())
				{
					ITickAggregatorInterface::Execute_BlueprintAggregatedTick(ObjectPtr, DeltaTime);
				}
			}
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_UnorderedTicks);
		
		for (TWeakObjectPtr<UObject> Object : NativeUnorderedObjectTickElements)
		{
			if (UObject* ObjectPtr = Object.Get())
			{
				ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(ObjectPtr);
				Interface->AggregatedTick(DeltaTime);
			}
		}
		
		for (TWeakObjectPtr<UObject> Object : BlueprintUnorderedObjectTickElements)
		{
			if (UObject* ObjectPtr = Object.Get())
			{
				ITickAggregatorInterface::Execute_BlueprintAggregatedTick(ObjectPtr, DeltaTime);
			}
		}
	}
	
}

FString FAggregatedTickFunction::DiagnosticMessage()
{
	return OwnerPrivate.IsValid() ? OwnerPrivate->GetName() : FString();
}

FName FAggregatedTickFunction::DiagnosticContext(bool bDetailed)
{
	return OwnerPrivate.IsValid() ? OwnerPrivate->GetFName() : FName();
}
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

#pragma once

#include "CoreMinimal.h"
#include "AggregatedTickFunction.generated.h"

/**
 * Wrapper for TArray to use it on UPROPERTY() TMap.
 */
USTRUCT()
struct FTickAggregatorObjectArray
{
	GENERATED_BODY()

public:
	const TArray<TWeakObjectPtr<UObject>>& Get() const { return Array; }
	int32 Add(UObject* Elem) { return Array.Add(Elem); }
	int32 Remove(UObject* Elem) { return Array.Remove(Elem); }
	int32 RemoveSwap(UObject* Elem) { return Array.RemoveSwap(Elem, false); }
	
private:
	UPROPERTY()
	TArray<TWeakObjectPtr<UObject>> Array;
};


USTRUCT()
struct FAggregatedTickFunction : public FTickFunction
{
	GENERATED_BODY()

	FAggregatedTickFunction()
	{
		bCanEverTick = true;
		bStartWithTickEnabled = true;
		bRunOnAnyThread = false;
	}
	
	/*FAggregatedTickElement(ETickingGroup InTickingGroup, ULevel* InLevel)
	{
		OwnerPrivate = nullptr;
		
		bCanEverTick = true;
		bStartWithTickEnabled = true;
		bRunOnAnyThread = false;
	}*/

	bool AddNewObject(UObject* Object);
	bool RemoveObject(UObject* Object);

	bool AddNewUnorderedObject(UObject* Object);
	bool RemoveUnorderedObject(UObject* Object);

	bool RemoveObjectOnNextTick(UObject* Object);
	bool RemoveUnorderedObjectOnNextTick(UObject* Object);

	bool DoesObjectImplementTickFunction(UObject* Object) const;

	int32 GetNum() const
	{
		int32 Num = 0;
		for (const TTuple<TSoftClassPtr<UClass>, FTickAggregatorObjectArray>& Elem : NativeAggregatedObjectTickElements)
		{
			Num += Elem.Value.Get().Num();
		}

		return Num;
	}
	
protected:
	// FTickFunction interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
	// End of FTickFunction interface

private:
	UPROPERTY(Transient)
	TMap<TSoftClassPtr<UClass>, FTickAggregatorObjectArray> NativeAggregatedObjectTickElements;

	UPROPERTY(Transient)
	TMap<TSoftClassPtr<UClass>, FTickAggregatorObjectArray> BlueprintAggregatedObjectTickElements;

	/** Unordered tick elements are only useful to reduce the cost of QueueTick of TickManager, and only
	 * should be used with actors / object that are singular or very few in the world. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> NativeUnorderedObjectTickElements;
	
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> BlueprintUnorderedObjectTickElements;
	
	UPROPERTY(Transient)
	TArray<UObject*> NativeObjectsToRemove;

	UPROPERTY(Transient)
	TArray<UObject*> NativeObjectsToRemoveUnordered;
	
	UPROPERTY(Transient)
	TArray<UObject*> BlueprintObjectsToRemove;

	UPROPERTY(Transient)
	TArray<UObject*> BlueprintObjectsToRemoveUnordered;

	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> OwnerPrivate;
	
};
// It is unsafe to copy FTickFunctions and any subclasses of FTickFunction should specify the type trait WithCopy = false
template<>
struct TStructOpsTypeTraits<FAggregatedTickFunction> : public TStructOpsTypeTraitsBase2<FAggregatedTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

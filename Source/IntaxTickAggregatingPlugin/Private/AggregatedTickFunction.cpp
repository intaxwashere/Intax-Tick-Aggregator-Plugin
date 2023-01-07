// Copyright INTAX Interactive, all rights reserved.

#include "AggregatedTickFunction.h"
#include "TickAggregatorInterface.h"

DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Remove Objects"), STAT_TickAggregator_RemoveObjects, STATGROUP_TickAggregator);

DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Total Tick Time"), STAT_TickAggregator_Tick, STATGROUP_TickAggregator);
DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Tick Native Functions"), STAT_TickAggregator_TickNativeFunctions, STATGROUP_TickAggregator);
DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Tick Blueprint Functions"), STAT_TickAggregator_TickBlueprintFunctions, STATGROUP_TickAggregator);

DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Tick Unordered Native Functions"), STAT_TickAggregator_TickUnorderedNativeFunctions, STATGROUP_TickAggregator);
DECLARE_CYCLE_STAT(TEXT("Tick Aggregator - Tick Unordered Blueprint Functions"), STAT_TickAggregator_TickUnorderedBlueprintFunctions, STATGROUP_TickAggregator);

// Note: TICK_AGGREGATOR_DO_CHECKS is only valid in editor.

void FAggregatedTickFunctionCollection::TickObjects(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_TickAggregator_Tick);

	// for each native array that is sorted by class type...
	for (const FTickAggregatorNativeObjectArray& NativeObjectArray : RegisteredNativeObjectsArray)
	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_TickNativeFunctions);

		// for each tick group that is sorted by identity...
		const TArray<FTickFunctionGroup>& TickFunctionGroupArray = NativeObjectArray.Get();
		for (const FTickFunctionGroup& TickFunctionGroup : TickFunctionGroupArray)
		{
			const TArray<FAggregatedTickDelegate>& TickFunctionArray = TickFunctionGroup.Get();

#if TICK_AGGREGATOR_DO_CHECKS
			if (!ensureMsgf(!TickFunctionArray.IsEmpty(), TEXT("Given TickFunctionArray was empty, it should have been removed before loop executed.")))
			{
				continue;
			}
#endif

			// get tick function delegates and invoke them.
			for (const FAggregatedTickDelegate& TickFunctionPtr : TickFunctionArray)
			{

#if TICK_AGGREGATOR_DO_CHECKS
					if (!ensureAlwaysMsgf(TickFunctionPtr.IsBound(), TEXT("TickFunctionPtr was not bound to anything?!")))
					{
						continue;
					}
#endif

				TickFunctionPtr.Execute(DeltaTime);
				//TickFunctionPtr(DeltaTime);
			}
		}

	}

	// for each blueprint object array that is sorted by class type...
	for (const FTickAggregatedBlueprintObjectArray& BlueprintObjectArray : RegisteredBlueprintObjectsArray)
	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_TickBlueprintFunctions);

		// take the pair of UObject & UFunction...
		for (auto [WeakObjectPtr, Function] : BlueprintObjectArray.Get())
		{
			const UObject* Object = WeakObjectPtr.Get();

#if TICK_AGGREGATOR_DO_CHECKS
				if (!ensureMsgf(Object != nullptr, TEXT("Given object to tick aggregated was invalid!")))
				{
					continue;
				}

				if (!ensureMsgf(Function != nullptr, TEXT("Given function to tick aggregated was invalid!")))
				{
					continue;
				}
#endif

			// create a fake struct and pass it to BP VM, which will look for a float variable
			// inside of it and pass it to called BP function. Jeez, BP VM is so strange.
			Intax::TA::TickAggregatorDeltaSecondsParam Params(DeltaTime);
			WeakObjectPtr->ProcessEvent(Function, &Params);
		}
	}
}

void FAggregatedTickFunctionCollection::RemoveAndDestroyRequiredObjects()
{
	SCOPE_CYCLE_COUNTER(STAT_TickAggregator_RemoveObjects);
	
	for (int32 i = NativeFunctionsPendingRemove.Num() - 1; i >= 0; --i)
	{
		FTickAggregatorFunctionHandle& Handle = NativeFunctionsPendingRemove[i];
		
#if TICK_AGGREGATOR_DO_CHECKS
		if (ensureMsgf(!Handle.IsValid(), TEXT("Given function handle in FunctionsPendingRemove was invalid!")))
		{
			continue;
		}
#endif

		// @TODO this can be optimized even more. We shouldnt do lookup by predicate at each loop, but maybe we can build another data structure to do faster lookups.

		const TSubclassOf<UObject> Class = Handle.GetClassType();
		const auto Predicate = [Class](const FTickAggregatorNativeObjectArray& Array) { return Array.GetClassType() == Class; };
		FTickAggregatorNativeObjectArray* FoundObjectArray = RegisteredNativeObjectsArray.FindByPredicate(Predicate);

#if TICK_AGGREGATOR_DO_CHECKS
		if (ensureMsgf(!FoundObjectArray, TEXT("Could not find object array associated with the given class for native object")))
		{
			continue;
		}
#endif

		FoundObjectArray->RemoveTickFunction(Handle.GetIdentity(), Handle.GetTickFunction());
		if (FoundObjectArray->TickGroupArray.Num() == 0)
		{
			RegisteredNativeObjectsArray.Remove(*FoundObjectArray);
		}
	}

	for (UObject* Object : BlueprintObjectsPendingRemove)
	{
#if TICK_AGGREGATOR_DO_CHECKS
		if (ensureMsgf(Object != nullptr, TEXT("Given UObject in BlueprintObjectsPendingRemove was invalid!")))
		{
			continue;
		}
#endif

		UClass* Class = Object->GetClass();
		const auto Predicate = [Class](const FTickAggregatedBlueprintObjectArray& Array) { return Array.GetClassType() == Class; };
		FTickAggregatedBlueprintObjectArray* FoundObjectArray = RegisteredBlueprintObjectsArray.FindByPredicate(Predicate);
#if TICK_AGGREGATOR_DO_CHECKS
		if (ensureMsgf(FoundObjectArray != nullptr, TEXT("Could not find object array associated with the given class for BP object")))
		{
			continue;
		}
#endif

		UFunction* Function = Object->FindFunctionChecked(Intax::TA::BlueprintTickFunctionName);
		FoundObjectArray->RemoveSwap(Object, Function);
		if (FoundObjectArray->Get().Num() == 0)
		{
			RegisteredBlueprintObjectsArray.Remove(*FoundObjectArray);
		}
	}
}

FTickAggregatorFunctionHandle FAggregatedTickFunctionCollection::AddNewNativeFunction(const UObject* Object, const FAggregatedTickDelegate& Function, const FName TickFunctionGroupName)
{
	using namespace Intax::TA;

	UClass* Class = Object->GetClass();
	if (!ensure(Class))
	{
		return MakeInvalidFunctionHandle();
	}

	// try to find existing object array with given object's class type.
	auto Predicate = [Class](const FTickAggregatorNativeObjectArray& ObjectArray) { return ObjectArray.IsA(Class); };
	FTickAggregatorNativeObjectArray* FoundObjectArray = RegisteredNativeObjectsArray.FindByPredicate(Predicate);
	if (FoundObjectArray)
	{
		const int32 Index = FoundObjectArray->AddNewTickFunction(TickFunctionGroupName, Function);
		if (Index != INDEX_NONE)
		{
			return MakeFunctionHandle(Index, AssociatedTickCategory, AssociatedTickingGroup, Class, TickFunctionGroupName);
		}

		return MakeInvalidFunctionHandle();
	}
	else // if there isnt one existing for given object type, create a new one and add object's function to it.
	{
		const int32 Index = BuildNewObjectArrayFor<FTickAggregatorNativeObjectArray>(Class).AddNewTickFunction(TickFunctionGroupName, Function);
		if (Index != INDEX_NONE)
		{
			return MakeFunctionHandle(Index, AssociatedTickCategory, AssociatedTickingGroup, Class, TickFunctionGroupName);
		}

		return MakeInvalidFunctionHandle();
	}
}

bool FAggregatedTickFunctionCollection::AddNewRemoveRequest(const FTickAggregatorFunctionHandle& InHandle)
{
	return NativeFunctionsPendingRemove.AddUnique(InHandle) > INDEX_NONE;
}

bool FAggregatedTickFunctionCollection::AddNewBlueprintFunction(UObject* Object)
{
	UClass* Class = Object->GetClass();
	if (!ensure(Class))
	{
		return false;
	}

	if (!Intax::TA::IsBlueprintObject(Object))
	{
		return false;
	}

	UFunction* Function = Class->FindFunctionByName(Intax::TA::BlueprintTickFunctionName);
	if (!ensure(Function))
	{
		return false;
	}

	auto Predicate = [Class](const FTickAggregatedBlueprintObjectArray& ObjectArray) { return ObjectArray.IsA(Class); };
	FTickAggregatedBlueprintObjectArray* FoundObjectArray = RegisteredBlueprintObjectsArray.FindByPredicate(Predicate);
	if (FoundObjectArray)
	{
		return FoundObjectArray->AddUnique(Object, Function) > INDEX_NONE;
	}
	else
	{
		return BuildNewObjectArrayFor<FTickAggregatedBlueprintObjectArray>(Class).Add(Object, Function) > INDEX_NONE;
	}

	return false;
}

#if WITH_EDITOR
void FAggregatedTickFunctionCollection::DumpTicks(const FString& CategoryName)
{
	if (RegisteredNativeObjectsArray.IsEmpty())
	{
		TA_LOG(Log, "---");
		TA_LOG(Log, "%s is empty.", *CategoryName);
		TA_LOG(Log, "---");
	}
	else
	{
		TA_LOG(Log, "---");
		TA_LOG(Log, "%s Tick Functions:", *CategoryName);
		for (const FTickAggregatorNativeObjectArray& NativeObjects : RegisteredNativeObjectsArray)
		{
			const TArray<FTickFunctionGroup>& Array = NativeObjects.Get();
			for (const FTickFunctionGroup& TickFunctionGroup : Array)
			{
				const TArray<FAggregatedTickDelegate>& TickFunctionArray = TickFunctionGroup.Get();
				for (const FAggregatedTickDelegate& FunctionArray : TickFunctionArray)
				{
					TA_LOG(Log, "OBJECT: %s - DEFINITION: %s", *FunctionArray.GetUObject()->GetName(), *TickFunctionGroup.GetDefinition().ToString());
				}
			}
		}
		TA_LOG(Log, "---");
	}
}
#endif

int32 FTickAggregatorNativeObjectArray::AddNewTickFunction(const FName Identity, const FAggregatedTickDelegate& FunctionPtr)
{
	if ((Identity == NAME_None || !FunctionPtr.IsBound()))
	{
		return INDEX_NONE;
	}

	FTickFunctionGroup* FoundTickGroup = FindTickGroupByIdentity(Identity);
	if (FoundTickGroup)
	{
		return FoundTickGroup->Add(FunctionPtr);
	}
	else
	{
		return TickGroupArray.Emplace_GetRef(Identity).Add(FunctionPtr);
	}
}

void FTickAggregatorNativeObjectArray::RemoveTickFunction(const FName Identity, FAggregatedTickDelegate FunctionPtr)
{
	if (!ensure(Identity == NAME_None) || !ensure(FunctionPtr.IsBound()))
	{
		return;
	}

	FTickFunctionGroup* FoundTickGroup = FindTickGroupByIdentity(Identity);
	if (ensure(FoundTickGroup))
	{
		FoundTickGroup->RemoveSwap(FunctionPtr);
	}
}

FTickFunctionGroup* FTickAggregatorNativeObjectArray::FindTickGroupByIdentity(const FName Identity)
{
	auto Predicate = [Identity](const FTickFunctionGroup& TickFunctionGroup) { return TickFunctionGroup.GetDefinition() == Identity; };
	FTickFunctionGroup* FoundTickGroup = TickGroupArray.FindByPredicate(Predicate);
	return FoundTickGroup;
}

void FAggregatedTickFunctionCollection::Execute(float DeltaTime)
{
	// remove required objects before ticking them.
	RemoveAndDestroyRequiredObjects();

	// tick the objects.
	TickObjects(DeltaTime);
}

FTickAggregatorFunctionHandle FAggregatedTickFunction::RegisterNativeFunction(const UObject* Object, const FAggregatedTickDelegate& Function, ETickAggregatorTickCategory::Type Category, const FName TickFunctionGroupName)
{
	using namespace Intax::TA;

	if (!Function.IsBound() || !Object)
	{
		return MakeInvalidFunctionHandle();
	}

	switch (Category)
	{
	case ETickAggregatorTickCategory::TC_UNORDERED: return RegisterUnorderedNativeFunction(Object, Function, Category);
	case ETickAggregatorTickCategory::TC_ALPHA:     return Alpha.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_BRAVO:     return Bravo.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_CHARLIE:   return Charlie.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_ECHO:      return Echo.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_FOXTROT:   return Foxtrot.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_GOLF:      return Golf.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_HOTEL:     return Hotel.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	case ETickAggregatorTickCategory::TC_INDIA:     return India.AddNewNativeFunction(Object, Function, TickFunctionGroupName);
	default:
		checkNoEntry();
		return MakeInvalidFunctionHandle();
	}

}

bool FAggregatedTickFunction::RemoveNativeFunction(const FTickAggregatorFunctionHandle& InHandle)
{
	const ETickAggregatorTickCategory::Type Category = InHandle.GetTickCategory();

	switch (Category)
	{
	case ETickAggregatorTickCategory::TC_ALPHA:   return Alpha.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_BRAVO:   return Bravo.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_CHARLIE: return Charlie.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_ECHO:    return Echo.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_FOXTROT: return Foxtrot.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_GOLF:    return Golf.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_HOTEL:   return Hotel.AddNewRemoveRequest(InHandle);
	case ETickAggregatorTickCategory::TC_INDIA:   return India.AddNewRemoveRequest(InHandle);
	default:
		checkNoEntry();
		return false;
	}
}

bool FAggregatedTickFunction::RegisterBlueprintFunction(UObject* Object, ETickAggregatorTickCategory::Type Category)
{
	if (!Object)
	{
		return false;
	}

	switch (Category)
	{
	case ETickAggregatorTickCategory::TC_UNORDERED: return RegisterUnorderedBlueprintFunction(Object, Category);
	case ETickAggregatorTickCategory::TC_ALPHA: return Alpha.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_BRAVO: return Bravo.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_CHARLIE: return Charlie.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_ECHO: return Echo.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_FOXTROT: return Foxtrot.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_GOLF: return Golf.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_HOTEL: return Hotel.AddNewBlueprintFunction(Object);
	case ETickAggregatorTickCategory::TC_INDIA: return India.AddNewBlueprintFunction(Object);
	default:
		return false;
		checkNoEntry();
	}
}

void FAggregatedTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
                                          const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_TickAggregator_Tick);

	// Tick legacy
	Legacy_Tick(DeltaTime);

	// Execute ticks in order.

	Alpha.Execute(DeltaTime);
	Bravo.Execute(DeltaTime);
	Charlie.Execute(DeltaTime);
	Delta.Execute(DeltaTime);

	Echo.Execute(DeltaTime);

	// we tick unordered objects after echo
	RemovePendingUnorderedTickFunctions();
	ExecuteUnorderedTickFunctions(DeltaTime);

	Foxtrot.Execute(DeltaTime);
	Golf.Execute(DeltaTime);
	Hotel.Execute(DeltaTime);
	India.Execute(DeltaTime);
}

FString FAggregatedTickFunction::DiagnosticMessage()
{
	return FString();
}

FName FAggregatedTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName();
}

void FAggregatedTickFunction::ExecuteUnorderedTickFunctions(float DeltaTime) const
{
	SCOPE_CYCLE_COUNTER(STAT_TickAggregator_Tick);

	for (const FTickFunctionGroup& TickFunctionGroup : NativeUnorderedTickFunctions.Get())
	{
		for (const FAggregatedTickDelegate& FunctionDelegate : TickFunctionGroup.Get())
		{
			SCOPE_CYCLE_COUNTER(STAT_TickAggregator_TickNativeFunctions);
			SCOPE_CYCLE_COUNTER(STAT_TickAggregator_TickUnorderedNativeFunctions);

#if TICK_AGGREGATOR_DO_CHECKS
			if (!ensureMsgf(FunctionDelegate.IsBound(), TEXT("TickFunctionPtr was invalid!")))
			{
				continue;
			}
#endif

			FunctionDelegate.Execute(DeltaTime);
		}
	}

	for (const TPair<TWeakObjectPtr<UObject>, UFunction*>& Pair : BlueprintUnorderedTickFunctions.Get())
	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_TickUnorderedBlueprintFunctions);

		UObject* Object = Pair.Key.Get();
		UFunction* Function = Pair.Value;

#if TICK_AGGREGATOR_DO_CHECKS
		if (!ensureMsgf(Object, TEXT("Given object to tick aggregated was invalid!")))
		{
			continue;
		}

		if (!ensureMsgf(Function, TEXT("Given function to tick aggregated was invalid!")))
		{
			continue;
		}
#endif

		Intax::TA::TickAggregatorDeltaSecondsParam Params(DeltaTime);
		Object->ProcessEvent(Function, &Params);
	}
}

void FAggregatedTickFunction::RemovePendingUnorderedTickFunctions()
{
	for (const FTickAggregatorFunctionHandle& Handle : NativeUnorderedTickFunctionsPendingRemove)
	{
#if TICK_AGGREGATOR_DO_CHECKS
		if (!ensureMsgf(Handle.IsValid(), TEXT("Given function handle in FunctionsPendingRemove was invalid!")))
		{
			continue;
		}
#endif

		// @TODO this can be optimized even more. We shouldnt do lookup by predicate at each loop, but maybe we can build another data structure to do faster lookups.

		NativeUnorderedTickFunctions.RemoveTickFunction(Handle.GetIdentity(), Handle.GetTickFunction());
	}

	for (UObject* Object : BlueprintUnorderedObjectsPendingRemove)
	{
#if TICK_AGGREGATOR_DO_CHECKS
		if (!ensureMsgf(Object != nullptr, TEXT("Given UObject in BlueprintObjectsPendingRemove was invalid!")))
		{
			continue;
		}
#endif
		UFunction* Function = Object->FindFunctionChecked(Intax::TA::BlueprintTickFunctionName);
		BlueprintUnorderedTickFunctions.RemoveSwap(Object, Function);
	}
}

FTickAggregatorFunctionHandle FAggregatedTickFunction::RegisterUnorderedNativeFunction(const UObject* Object, FAggregatedTickDelegate Function, ETickAggregatorTickCategory::Type Category)
{
	using namespace Intax::TA;

	UClass* Class = Object->GetClass();
	if (!ensure(Class))
	{
		return MakeInvalidFunctionHandle();
	}

	const int32 Index = NativeUnorderedTickFunctions.AddNewTickFunction(DefaultTickFunctionCategory, Function);
	if (Index != INDEX_NONE)
	{
		return MakeFunctionHandle(Index, ETickAggregatorTickCategory::TC_UNORDERED, AssociatedTickGroup, Class, InvalidTickFunctionCategory);
	}

	return MakeInvalidFunctionHandle();
}

bool FAggregatedTickFunction::RemoveUnorderedNativeFunction(const FTickAggregatorFunctionHandle& FunctionHandle)
{
	return NativeUnorderedTickFunctionsPendingRemove.Add(FunctionHandle) > INDEX_NONE;
}

bool FAggregatedTickFunction::RegisterUnorderedBlueprintFunction(UObject* Object, ETickAggregatorTickCategory::Type Category)
{
	using namespace Intax::TA;

	UClass* Class = Object->GetClass();
	if (!ensure(Class))
	{
		return false;
	}

	const int32 Index = BlueprintUnorderedObjectsPendingRemove.AddUnique(Object);
	return Index != INDEX_NONE;
}

/////////////// LEGACY SUPPORT

bool FAggregatedTickFunction::Legacy_AddNewObject(UObject* Object)
{
	if (IsValid(Object))
	{
		const UClass* Class = Object->GetClass();
		if (Intax::TA::DoesObjectImplementBlueprintTickFunction(Object))
		{
			if (Intax::TA::DoesBlueprintObjectHaveValidNativeClass(Object))
			{
				FTickAggregatorObjectArray& NativeObjectArray = Legacy_NativeAggregatedObjectTickElements.FindOrAdd(Class);
				FTickAggregatorObjectArray& BlueprintObjectArray = Legacy_BlueprintAggregatedObjectTickElements.FindOrAdd(Class);
				return NativeObjectArray.AddUnique(Object) != INDEX_NONE && BlueprintObjectArray.AddUnique(Object) != INDEX_NONE;
			}
			else
			{
				FTickAggregatorObjectArray& BlueprintObjectArray = Legacy_BlueprintAggregatedObjectTickElements.FindOrAdd(Class);
				return BlueprintObjectArray.AddUnique(Object) != INDEX_NONE;
			}
		}
		else
		{
			FTickAggregatorObjectArray& NativeObjectArray = Legacy_NativeAggregatedObjectTickElements.FindOrAdd(Class);
			return NativeObjectArray.AddUnique(Object) != INDEX_NONE;
		}

		checkNoEntry();
	}

	return false;
}

bool FAggregatedTickFunction::Legacy_RemoveObject(UObject* Object)
{
	if (IsValid(Object))
	{
		if (Intax::TA::DoesObjectImplementBlueprintTickFunction(Object))
		{
			if (Intax::TA::DoesBlueprintObjectHaveValidNativeClass(Object))
			{
				return Legacy_BlueprintObjectsToRemove.RemoveSwap(Object) != INDEX_NONE && Legacy_NativeObjectsToRemove.RemoveSwap(Object) != INDEX_NONE;
			}
			else
			{
				return Legacy_BlueprintObjectsToRemove.RemoveSwap(Object) != INDEX_NONE;
			}
		}
		else
		{
			//const UClass* Class = Object->GetClass();
			//FTickAggregatorObjectArray* ObjectArray = AggregatedObjectTickElements.Find(Class);
			return Legacy_NativeObjectsToRemove.RemoveSwap(Object) != INDEX_NONE;
		}
	}
	return false;
}

bool FAggregatedTickFunction::Legacy_AddNewUnorderedObject(UObject* Object)
{
	if (Intax::TA::DoesObjectImplementBlueprintTickFunction(Object))
	{
		if (Intax::TA::DoesBlueprintObjectHaveValidNativeClass(Object))
		{
			return Legacy_BlueprintUnorderedObjectTickElements.Add(Object) > INDEX_NONE && Legacy_NativeUnorderedObjectTickElements.Add(Object) > INDEX_NONE;
		}
		else
		{
			return Legacy_BlueprintUnorderedObjectTickElements.Add(Object) > INDEX_NONE;
		}
	}
	else
	{
		return Legacy_NativeUnorderedObjectTickElements.Add(Object) > INDEX_NONE;
	}
}

bool FAggregatedTickFunction::Legacy_RemoveUnorderedObject(UObject* Object)
{
	if (Intax::TA::DoesObjectImplementBlueprintTickFunction(Object))
	{
		if (Intax::TA::DoesBlueprintObjectHaveValidNativeClass(Object))
		{
			return Legacy_BlueprintUnorderedObjectTickElements.Remove(Object) > 0 && Legacy_NativeUnorderedObjectTickElements.Remove(Object) > 0;
		}
		else
		{
			return Legacy_BlueprintUnorderedObjectTickElements.Remove(Object) > 0;
		}
	}
	else
	{
		return Legacy_NativeUnorderedObjectTickElements.Remove(Object) > 0;
	}

}

bool FAggregatedTickFunction::Legacy_RemoveObjectOnNextTick(UObject* Object)
{
	if (Intax::TA::DoesObjectImplementBlueprintTickFunction(Object))
	{
		if (Intax::TA::DoesBlueprintObjectHaveValidNativeClass(Object))
		{
			return Legacy_BlueprintObjectsToRemove.Add(Object) != INDEX_NONE && Legacy_NativeObjectsToRemove.Add(Object) != INDEX_NONE;
		}
		else
		{
			return Legacy_BlueprintObjectsToRemove.Add(Object) > 0;
		}
	}

	return Legacy_NativeObjectsToRemove.Add(Object) != INDEX_NONE;
}

bool FAggregatedTickFunction::Legacy_RemoveUnorderedObjectOnNextTick(UObject* Object)
{
	if (Intax::TA::DoesObjectImplementBlueprintTickFunction(Object))
	{
		if (Intax::TA::DoesBlueprintObjectHaveValidNativeClass(Object))
		{
			return Legacy_BlueprintObjectsToRemoveUnordered.Add(Object) != INDEX_NONE && Legacy_NativeObjectsToRemove.Add(Object) != INDEX_NONE;
		}
		else
		{
			return Legacy_BlueprintObjectsToRemoveUnordered.Add(Object) != INDEX_NONE;
		}
	}

	return Legacy_NativeObjectsToRemoveUnordered.Add(Object) != INDEX_NONE;
}

void FAggregatedTickFunction::Legacy_Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_TickAggregator_Tick);

	{
		SCOPE_CYCLE_COUNTER(STAT_TickAggregator_RemoveObjects);

		for (UObject* Object : Legacy_NativeObjectsToRemove)
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

			FTickAggregatorObjectArray* ObjectArray = Legacy_NativeAggregatedObjectTickElements.Find(Object->GetClass());
			ObjectArray->RemoveSwap(Object);
		}
		Legacy_NativeObjectsToRemove.Reset();

		for (UObject* Object : Legacy_BlueprintObjectsToRemove)
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

			FTickAggregatorObjectArray* ObjectArray = Legacy_NativeAggregatedObjectTickElements.Find(Object->GetClass());
			ObjectArray->RemoveSwap(Object);
		}
		Legacy_BlueprintObjectsToRemove.Reset();

		for (UObject* Object : Legacy_NativeObjectsToRemoveUnordered)
		{
			Legacy_NativeUnorderedObjectTickElements.RemoveSwap(Object);
		}
		Legacy_NativeObjectsToRemoveUnordered.Reset();

		for (UObject* Object : Legacy_BlueprintObjectsToRemoveUnordered)
		{
			Legacy_BlueprintUnorderedObjectTickElements.RemoveSwap(Object);
		}
		Legacy_BlueprintObjectsToRemoveUnordered.Reset();
	}

	{
		{
			for (const TTuple<TSoftClassPtr<UClass>, FTickAggregatorObjectArray>& ObjectMapPair : Legacy_NativeAggregatedObjectTickElements)
			{
				const TArray<TWeakObjectPtr<UObject>>& ObjectArray = ObjectMapPair.Value.Get();
				for (TWeakObjectPtr<UObject> WeakObject : ObjectArray)
				{
					UObject* Object = WeakObject.Get();
					if (IsValid(Object))
					{
						ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(Object);
						Interface->AggregatedTick(DeltaTime);
					}
				}
			}

			for (const TTuple<TSoftClassPtr<UClass>, FTickAggregatorObjectArray>& ObjectMapPair : Legacy_BlueprintAggregatedObjectTickElements)
			{
				const TArray<TWeakObjectPtr<UObject>>& ObjectArray = ObjectMapPair.Value.Get();
				for (TWeakObjectPtr<UObject> WeakObject : ObjectArray)
				{
					UObject* Object = WeakObject.Get();
					if (IsValid(Object))
					{
						ITickAggregatorInterface::Execute_BlueprintAggregatedTick(Object, DeltaTime);
					}
				}
			}
		}

		{

			for (TWeakObjectPtr<UObject> Object : Legacy_NativeUnorderedObjectTickElements)
			{
				if (UObject* ObjectPtr = Object.Get())
				{
					ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(ObjectPtr);
					Interface->AggregatedTick(DeltaTime);
				}
			}

			for (TWeakObjectPtr<UObject> Object : Legacy_BlueprintUnorderedObjectTickElements)
			{
				if (UObject* ObjectPtr = Object.Get())
				{
					ITickAggregatorInterface::Execute_BlueprintAggregatedTick(ObjectPtr, DeltaTime);
				}
			}
		}
	}
}

#if !UE_BUILD_SHIPPING
void FAggregatedTickFunction::DumpTicks()
{
	TA_LOG(Log, "Tick Aggregator %s Dump Ticks Begin:", *UEnum::GetValueAsString(AssociatedTickGroup));
	Alpha.DumpTicks("Alpha");
	Bravo.DumpTicks("Bravo");
	Charlie.DumpTicks("Charlie");
	Delta.DumpTicks("Delta");
	Echo.DumpTicks("Echo");
	Foxtrot.DumpTicks("Foxtrot");
	Golf.DumpTicks("Golf");
	Hotel.DumpTicks("Hotel");
	India.DumpTicks("India");
}
#endif
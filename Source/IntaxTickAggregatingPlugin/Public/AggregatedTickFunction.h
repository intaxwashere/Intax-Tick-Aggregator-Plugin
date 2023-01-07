// Copyright INTAX Interactive, all rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "TickAggregatorTypes.h"

// @todo remove
struct FTickAggregatorObjectArray
{
	// FORCEINLINE is fine for this context.

	FORCEINLINE const TArray<TWeakObjectPtr<UObject>>& Get() const { return Array; }
	FORCEINLINE int32 Add(UObject* Elem) { return Array.Add(Elem); }
	FORCEINLINE int32 AddUnique(UObject* Elem) { return Array.AddUnique(Elem); }
	FORCEINLINE int32 Remove(UObject* Elem) { return Array.Remove(Elem); }
	FORCEINLINE int32 RemoveSwap(UObject* Elem) { return Array.RemoveSwap(Elem, false); }
	FORCEINLINE void RemoveAtSwap(const int32 Index) { return Array.RemoveAtSwap(Index); }
	
private:
	TArray<TWeakObjectPtr<UObject>> Array;
};

/**
 * Sequence of tick function pointers.
 * This struct stores an array of function pointers per "group" for each class type. So data structure looks like this:
 *
 * [FAggregatedTickFunctionCollection] -- (Holds array of FTickAggregatorNativeObjectArray per registered UObject)
 *		[FTickAggregatorNativeObjectArray] -- (Holds array of FTickFunctionGroup per tick function group)
 *			[FTickFunctionGroup (Identity: "CollisionCheck")]
 *				[Array of function pointers that updates and checks collision state of the object]
 *			[FTickFunctionGroup (Identity: "UpdateMovement")]
 *				[Array of function pointers that updates the movement after collision check]
 *			[FTickFunctionGroup (Identity: "UpdateNiagaraParams")]
 *				[Array of function pointers that updates parameters of a niagara component]
 *
 * this way we're able to split our code into smaller chunks and help CPU to fit the instructions into faster cache levels.
 */
struct FTickFunctionGroup
{
	FTickFunctionGroup() = delete;
	FTickFunctionGroup(const FName InIdentity) : Definition(InIdentity) {}

	// FORCEINLINE is fine for this context.

	FORCEINLINE const TArray<FAggregatedTickDelegate>& Get() const
	{
		return TickFunctionDelegates;
	}

	FORCEINLINE int32 Add(const FAggregatedTickDelegate& Elem)
	{
		return TickFunctionDelegates.Add(Elem);
	}

	FORCEINLINE int32 GetIndexOfByKey(const FAggregatedTickDelegate& Elem) const
	{
		const FDelegateHandle ElemHandle = Elem.GetHandle();
		const auto Predicate = [ElemHandle](const FAggregatedTickDelegate& Other)->bool { return Other.GetHandle() == ElemHandle; };
		const int32 Index = TickFunctionDelegates.IndexOfByPredicate(Predicate);
		return Index;
	}

	FORCEINLINE int32 AddUnique(const FAggregatedTickDelegate& Elem)
	{
		const int32 Index = GetIndexOfByKey(Elem);
		return Index == INDEX_NONE ? Add(Elem) : INDEX_NONE;
	}

	FORCEINLINE int32 Remove(const FAggregatedTickDelegate& Elem)
	{
		const int32 Index = GetIndexOfByKey(Elem);
		if (Index != INDEX_NONE && TickFunctionDelegates.IsValidIndex(Index))
		{
			TickFunctionDelegates.RemoveAt(Index);
			return 1;
		}

		return 0;
	}
	FORCEINLINE int32 RemoveSwap(const FAggregatedTickDelegate Elem)
	{
		const int32 Index = GetIndexOfByKey(Elem);
		if (Index != INDEX_NONE && TickFunctionDelegates.IsValidIndex(Index))
		{
			TickFunctionDelegates.RemoveAtSwap(Index);
			return 1;
		}

		return 0;
	}

	FORCEINLINE FName GetDefinition() const
	{
		return Definition;
	}

	// @todo
	void Tick(float DeltaTime)
	{

	}

protected:
	/**
	 * Delegates that hold a pointer to same functions
	 * Delegates get optimized into single function pointer call on shipping build, so it's safe to use them.
	 */
	TArray<FAggregatedTickDelegate> TickFunctionDelegates;

	/** User defined identity of this tick function array. i.e. name of the tick group. */
	FName Definition;
};

struct FTickFunctionGroupInterval : public FTickFunctionGroup
{
	void Tick(float DeltaTime)
	{
		SpentTime += DeltaTime;
		if (SpentTime >= Interval)
		{
			SpentTime = 0.f;
			for (const TDelegate<void(float)>& Function : TickFunctionDelegates)
			{
				Function.Execute(DeltaTime);
			}
		}
	}
private:
	double SpentTime;
	double Interval;
};

struct FTickFunctionGroupRoundRobin : public FTickFunctionGroup
{

	// CountOfTicks / frame
	// while reach the frame?
	// then delay?

	// problems: batch swap indexes
	// receive new prio from function owners?

	void placeholder()
	{
		const TArray<TPair<int32, double>> PrioList;

		// break up into prios

		// indexes hold the prio data
		// distribute elements by their indexes by using movetemp:
		// LowPrio[i] = MoveTemp(FuncArray[Index])
	}

	void Tick(float DeltaTime)
	{
		
	}
	
	struct FRoundRobinIndexRange { int32 X, Y; };
	TArray<FRoundRobinIndexRange> RoundRobinDistribution;
};

struct FTickFunctionGroupTimeSliced : public FTickFunctionGroup
{
	/**
	 * @brief Helper function to tick objects with a time-budget by range of indexes
	 * @param InIndexBegin Determines at which index of tick functions we start with
	 * @param InIndexEnd Determines at which index of tick functions we should exit the while loop
	 * @param InTimeBudget Max time we can take to tick functions
	 * @param InDeltaTime Tick delta
	 * @param OutTimeLeft Time left after we finished while loop
	 * @param OutIndex Reached index after we finished while loop
	 */
	void TickRange(const int32 InIndexBegin, const int32 InIndexEnd, const float InTimeBudget, float InDeltaTime, float& OutTimeLeft, int32& OutIndex)
	{
		int32 Index = InIndexBegin;
		float TimeLeft = InTimeBudget;

		ensure(Index != INDEX_NONE);
		ensure(Index < TickFunctionDelegates.Num());
		ensure(TimeLeft > 0.0);
		
		while (TimeLeft > 0.0 && Index <= InIndexEnd)
		{
			// time we started this step
			const double StepStartTime = FPlatformTime::Seconds();

			// access the tick function and execute it
			FAggregatedTickDelegate& Function = TickFunctionDelegates[Index];
			Function.Execute(InDeltaTime);

			// time spent to execute the tick function
			const double StepProcessingTime = FPlatformTime::Seconds() - StepStartTime;

			// decrement time spent to execute the tick function from time budget
			TimeLeft -= StepProcessingTime;
			// increment the index
			Index++;
		}

		OutTimeLeft = TimeLeft;
		OutIndex = Index;
	}
	
	void Tick(float DeltaTime)
	{
		// total number of tickable functions
		const int32 FunctionCount = TickFunctionDelegates.Num();

		const int32 MaxIndex = FunctionCount - 1;

		// cache at which index we began this frame 
		const int32 PreviousIndexBegin = IndexBegin;
		
		float TimeLeft;
		int32 Index;
		TickRange(IndexBegin, MaxIndex, TimeLimit, DeltaTime, TimeLeft, Index);

		// if we haven't consumed all of our time budget but we encountered of the function index to execute..
		if (TimeLeft > 0.0 && Index < FunctionCount)
		{
			// check if our begin index was zero or not - if it wasnt zero, that means previous frame we skipped
			// this frame's updates, so we can roll index back and update until reach the PreviousIndexBegin 
			if (PreviousIndexBegin != 0)
			{
				float SecondTimeLeft;
				int32 SecondIndex;
				TickRange(0, PreviousIndexBegin, DeltaTime, TimeLeft, SecondTimeLeft, SecondIndex);
				// at this point we can not ever tick the range again - we either reached the max function count or consumed our budged
				// if we consumed our budget:
				if (SecondIndex < MaxIndex)
				{
					// save the index we managed to reach in second iteration
					IndexBegin = SecondIndex;
				}
				else // if we reached the max function index:
				{
					// roll back to first index
					IndexBegin = 0;
				}
			}
		}
		// if we consumed all of our time budget but couldnt tick all possible functions, set next frame's IndexBegin
		// to currently reached index, so next frame will continue from where we left.
		else if (TimeLeft <= 0.0 && Index < MaxIndex)
		{
			IndexBegin = Index;
		}
		// if we both consumed our time budget and managed to tick all elements, then yay... just set IndexBegin
		// to zero so we will tick elements from start next frame.
		else if (TimeLeft <= 0.0 && Index >= FunctionCount)
		{
			IndexBegin = 0;
		}
		else
		{
			// we probably covered every situation already - code shouldnt reach to this place
			checkNoEntry();
		}
	}
	
private:
	double TimeLimit = 0.0;
	int32 IndexBegin = 0;
};

/**
 * Data struct that is holding an array of TickFunctionGroup's for a specific class type. 
 */
struct FTickAggregatorNativeObjectArray
{
	friend struct FAggregatedTickFunctionCollection;

	FTickAggregatorNativeObjectArray() {}
	FTickAggregatorNativeObjectArray(TSubclassOf<UObject> InClassType) : ClassType(InClassType) {}

	int32 AddNewTickFunction(const FName Identity, const FAggregatedTickDelegate& FunctionPtr);
	void RemoveTickFunction(const FName Identity, FAggregatedTickDelegate FunctionPtr);

	// FORCEINLINE is fine for this context.

	FORCEINLINE const TArray<FTickFunctionGroup>& Get() const { return TickGroupArray; }

	FORCEINLINE TSubclassOf<UObject> GetClassType() const { return ClassType; }
	FORCEINLINE bool IsA(const TSubclassOf<UObject> Class) const { return Class == ClassType; }

	friend bool operator==(const FTickAggregatorNativeObjectArray& Lhs, const FTickAggregatorNativeObjectArray& RHS)
	{
		return Lhs.ClassType == RHS.ClassType && Lhs.TickGroupArray.Num() == RHS.TickGroupArray.Num();
	}

	friend bool operator!=(const FTickAggregatorNativeObjectArray& Lhs, const FTickAggregatorNativeObjectArray& RHS)
	{
		return !(Lhs == RHS);
	}

protected:
	FTickFunctionGroup* FindTickGroupByIdentity(const FName Identity);

	TArray<FTickFunctionGroup> TickGroupArray;
	TSubclassOf<UObject> ClassType;
};

struct FTickFunctionNativeRoundRobinGroup : public FTickAggregatorNativeObjectArray
{
	FTickFunctionNativeRoundRobinGroup(TSubclassOf<UObject> InClassType, const uint64 FrameDelay)
		: FTickAggregatorNativeObjectArray(InClassType), FrameDelay(FrameDelay), CurrentFrameIndex(0), CurrentGroupIndex(0), CurrentDelta(0)
	{
	}

	void Update(float DeltaSeconds)
	{
		check(CurrentFrameIndex == 0 || (CurrentFrameIndex > 0 || CurrentFrameIndex <= FrameDelay));

		// update this frame
		CurrentFrameIndex++;
		CurrentDelta =+ DeltaSeconds;
		if (UNLIKELY(++CurrentFrameIndex == FrameDelay))
		{
			check(TickGroupArray.IsValidIndex(CurrentGroupIndex));

			// tick this group
			FTickFunctionGroup& CurrentTickFunctionGroup = TickGroupArray[CurrentGroupIndex];
			CurrentTickFunctionGroup.Tick(CurrentDelta);

			// reset the variables
			CurrentFrameIndex = 0;
			CurrentGroupIndex = (CurrentGroupIndex + 1) % TickGroupArray.Num();
			CurrentDelta = 0.f;
		}
	}

private:
	uint64 FrameDelay;
	uint64 CurrentFrameIndex;
	int32 CurrentGroupIndex;
	float CurrentDelta;
};

/**
 * Data struct that is holding an array of a pair that contains blueprint UObject and it's tick UFunction,
 * which points to AggregatedTick function implemented in interface.
 */
struct FTickAggregatedBlueprintObjectArray
{
	friend struct FAggregatedTickFunctionCollection;

	FTickAggregatedBlueprintObjectArray() {}
	FTickAggregatedBlueprintObjectArray(TSubclassOf<UObject> InClassType) : ClassType(InClassType) {}

	// FORCEINLINE is fine for this context.

	FORCEINLINE const TArray<TPair<TWeakObjectPtr<UObject>, UFunction*>>& Get() const { return Array; }
	FORCEINLINE int32 Add(UObject* Object, UFunction* Function) { return Array.Add({Object, Function}); }
	FORCEINLINE int32 AddUnique(UObject* Object, UFunction* Function) { return Array.AddUnique({ Object, Function }); }
	FORCEINLINE int32 Remove(UObject* Object, UFunction* Function) { return Array.Remove({ Object, Function }); }
	FORCEINLINE int32 RemoveSwap(UObject* Object, UFunction* Function) { return Array.RemoveSwap({ Object, Function }, false); }
	FORCEINLINE void RemoveAtSwap(const int32 Index) { return Array.RemoveAtSwap(Index); }

	FORCEINLINE TSubclassOf<UObject> GetClassType() const { return ClassType; }
	FORCEINLINE bool IsA(const TSubclassOf<UObject> Class) const { return Class == ClassType; }

	friend bool operator==(const FTickAggregatedBlueprintObjectArray& Lhs, const FTickAggregatedBlueprintObjectArray& RHS)
	{
		return Lhs.ClassType == RHS.ClassType && Lhs.Array.Num() == RHS.Array.Num();
	}

	friend bool operator!=(const FTickAggregatedBlueprintObjectArray& Lhs, const FTickAggregatedBlueprintObjectArray& RHS)
	{
		return !(Lhs == RHS);
	}

private:

	/**
	 * We store a pair which contains owner object and the BP function. We could directly call Tick function via interface
	 * but IInterface::Execute_BlueprintTick() actually goes through a TMap lookup to find and invoke the BP functions. Thats why we
	 * do that lookup during RegisterBlueprintFunction() and store the pointer to UFunction in this pair. So we get rid of the TMap lookup overhead
	 * per function call in tick aggregator.
	 * Also UFunction should be guaranteed to be valid until UObject pointer is valid, so we dont need multiple weak object pointers.
	 * TWeakObjectPtr's ".Get()" function does an array lookup (which is only ~3 instructions in theory but still..) and we need to avoid it
	 * (hey i made this plugin to microoptimize this process to moon so you dont have to!)
	 */
	TArray<TPair<TWeakObjectPtr<UObject>, UFunction*>> Array;
	TSubclassOf<UObject> ClassType;

};

/**
 * Tick function collection is a struct that holds an array of FTickAggregatedBlueprintObjectArray per class type
 */
struct FAggregatedTickFunctionCollection final
{
	FAggregatedTickFunctionCollection() = delete;
	FAggregatedTickFunctionCollection(ETickAggregatorTickCategory::Type InTickCategory, ETickingGroup InTickGroup)
									  : AssociatedTickCategory(InTickCategory), AssociatedTickingGroup(InTickGroup) {}

	/**
	 * Each tick function collections removes pending tick functions first, then ticks the objects.
	 */
	void Execute(float DeltaTime);

	FTickAggregatorFunctionHandle AddNewNativeFunction(const UObject* Object, const FAggregatedTickDelegate& Function, const FName TickFunctionGroupName);
	bool AddNewRemoveRequest(const FTickAggregatorFunctionHandle& InHandle);

	bool AddNewBlueprintFunction(UObject* Object);

#if !UE_BUILD_SHIPPING
	void DumpTicks(const FString& CategoryName);
#endif

private:

	/**
	 * Build a new object array for given class type. Both used for BP objects and native objects.
	 * T determines the type of the object array, whether it will be a BP one or native one.
	 */
	template<typename T>
	T& BuildNewObjectArrayFor(TSubclassOf<UObject> Class)
	{
		static_assert(!std::is_pointer_v<T>);
		static_assert(std::is_base_of_v<T, FTickAggregatorNativeObjectArray> || std::is_base_of_v<T, FTickAggregatedBlueprintObjectArray>);

		T NewObjectArray(Class);
		if constexpr (std::is_base_of_v<T, FTickAggregatorNativeObjectArray>)
		{
			int32 Index = RegisteredNativeObjectsArray.Add(NewObjectArray);
			return RegisteredNativeObjectsArray[Index];
		}
		else // if constexpr (std::is_base_of_v<T, FTickAggregatedBlueprintObjectArray>)
		{
			int32 Index = RegisteredBlueprintObjectsArray.Add(NewObjectArray);
			return RegisteredBlueprintObjectsArray[Index];
		}
	}

	void TickObjects(float DeltaTime);
	void RemoveAndDestroyRequiredObjects();

	TArray<FTickAggregatorNativeObjectArray> RegisteredNativeObjectsArray;
	TArray<FTickAggregatorFunctionHandle> NativeFunctionsPendingRemove;

	TArray<FTickAggregatedBlueprintObjectArray> RegisteredBlueprintObjectsArray;
	TArray<UObject*> BlueprintObjectsPendingRemove;

	/** The tick category that this aggregated tick function is associated with. */
	ETickAggregatorTickCategory::Type AssociatedTickCategory = ETickAggregatorTickCategory::TC_MAX;

	ETickingGroup AssociatedTickingGroup = TG_MAX;
};

struct FAggregatedTickFunction : public FTickFunction
{
	friend class UTickAggregatorWorldSubsystem;

	FAggregatedTickFunction() = delete;

	FAggregatedTickFunction(const ETickingGroup InTickingGroup) :
	Alpha(ETickAggregatorTickCategory::TC_ALPHA, InTickingGroup),
	Bravo(ETickAggregatorTickCategory::TC_BRAVO, InTickingGroup),
	Charlie(ETickAggregatorTickCategory::TC_CHARLIE, InTickingGroup),
	Delta(ETickAggregatorTickCategory::TC_DELTA, InTickingGroup),
	Echo(ETickAggregatorTickCategory::TC_ECHO, InTickingGroup),
	Foxtrot(ETickAggregatorTickCategory::TC_FOXTROT, InTickingGroup),
	Golf(ETickAggregatorTickCategory::TC_GOLF, InTickingGroup),
	Hotel(ETickAggregatorTickCategory::TC_HOTEL, InTickingGroup),
	India(ETickAggregatorTickCategory::TC_INDIA, InTickingGroup),
	NativeUnorderedTickFunctions(),
	BlueprintUnorderedTickFunctions()
	{
		check(AssociatedTickGroup != TG_MAX);
		AssociatedTickGroup   = InTickingGroup;
		bCanEverTick          = true;
		bStartWithTickEnabled = true;
		bRunOnAnyThread       = false;
	}

	FTickAggregatorFunctionHandle RegisterNativeFunction(const UObject* Object, const FAggregatedTickDelegate& Function, ETickAggregatorTickCategory::Type Category, const FName TickFunctionGroupName);
	bool RemoveNativeFunction(const FTickAggregatorFunctionHandle& InHandle);

	bool RegisterBlueprintFunction(UObject* Object, ETickAggregatorTickCategory::Type Category);

	FTickAggregatorFunctionHandle RegisterUnorderedNativeFunction(const UObject* Object, FAggregatedTickDelegate Function, ETickAggregatorTickCategory::Type Category);
	bool RemoveUnorderedNativeFunction(const FTickAggregatorFunctionHandle& FunctionHandle);

	bool RegisterUnorderedBlueprintFunction(UObject* Object, ETickAggregatorTickCategory::Type Category);

	// Legacy support

	bool Legacy_AddNewObject(UObject* Object);
	bool Legacy_RemoveObject(UObject* Object);

	bool Legacy_AddNewUnorderedObject(UObject* Object);
	bool Legacy_RemoveUnorderedObject(UObject* Object);

	bool Legacy_RemoveObjectOnNextTick(UObject* Object);
	bool Legacy_RemoveUnorderedObjectOnNextTick(UObject* Object);

	void Legacy_Tick(float DeltaTime);

#if !UE_BUILD_SHIPPING
	/** Editor only function that prints every registered tick with required information to output log. */
	void DumpTicks();
#endif

protected:
	// FTickFunction interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
	// End of FTickFunction interface

	void ExecuteUnorderedTickFunctions(float DeltaTime) const;
	void RemovePendingUnorderedTickFunctions();

private:

	FAggregatedTickFunctionCollection Alpha;
	FAggregatedTickFunctionCollection Bravo;
	FAggregatedTickFunctionCollection Charlie;
	FAggregatedTickFunctionCollection Delta;
	FAggregatedTickFunctionCollection Echo;
	FAggregatedTickFunctionCollection Foxtrot;
	FAggregatedTickFunctionCollection Golf;
	FAggregatedTickFunctionCollection Hotel;
	FAggregatedTickFunctionCollection India;

	FTickAggregatorNativeObjectArray NativeUnorderedTickFunctions;
	TArray<FTickAggregatorFunctionHandle> NativeUnorderedTickFunctionsPendingRemove;

	FTickAggregatedBlueprintObjectArray BlueprintUnorderedTickFunctions;
	TArray<UObject*> BlueprintUnorderedObjectsPendingRemove;

	ETickingGroup AssociatedTickGroup;

	// Legacy support

	UPROPERTY(Transient)
	TMap<TSoftClassPtr<UClass>, FTickAggregatorObjectArray> Legacy_NativeAggregatedObjectTickElements;

	UPROPERTY(Transient)
	TMap<TSoftClassPtr<UClass>, FTickAggregatorObjectArray> Legacy_BlueprintAggregatedObjectTickElements;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> Legacy_NativeUnorderedObjectTickElements;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> Legacy_BlueprintUnorderedObjectTickElements;

	UPROPERTY(Transient)
	TArray<UObject*> Legacy_NativeObjectsToRemove;

	UPROPERTY(Transient)
	TArray<UObject*> Legacy_NativeObjectsToRemoveUnordered;

	UPROPERTY(Transient)
	TArray<UObject*> Legacy_BlueprintObjectsToRemove;

	UPROPERTY(Transient)
	TArray<UObject*> Legacy_BlueprintObjectsToRemoveUnordered;
	
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

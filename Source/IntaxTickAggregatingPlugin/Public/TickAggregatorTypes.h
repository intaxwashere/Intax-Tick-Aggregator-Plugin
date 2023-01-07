// Copyright INTAX Interactive, all rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EngineMinimal.h"
#include "TickAggregatorTypes.generated.h"

class UTickAggregatorWorldSubsystem;

/* Alias for tick function that is equal to "void Tick(float DeltaSeconds)" -- which is equal to DECLARE_DELEGATE_OneParam(float) */
using FAggregatedTickDelegate = TDelegate<void(float)>;

// hi macro haters.. have fun!

DECLARE_LOG_CATEGORY_CLASS(LogTickAggregator, Log, All);

#define TA_LOG(Verbosity, Format, ...) UE_LOG(LogTickAggregator, Verbosity, TEXT(Format), ##__VA_ARGS__)

DECLARE_STATS_GROUP(TEXT("Tick Aggregator"), STATGROUP_TickAggregator, STATCAT_Advanced);

#define TA_REGISTER_TICK(Handle, Object, Func, TickingGroup, Category, FuncGroup) \
	{ \
		if (UTickAggregatorWorldSubsystem* TA = GetWorld() ? GetWorld()->GetSubsystem<UTickAggregatorWorldSubsystem>() : nullptr) \
		{ \
			FAggregatedTickDelegate Delegate; \
			Delegate.BindUObject(Object, &ThisClass::Func); \
			Handle = TA->RegisterNativeObject(Object, Delegate, TickingGroup, ETickAggregatorTickCategory::Category, FuncGroup); \
		} \
		else \
		{ \
			TA_LOG(Warning, "TA_REGISTER_TICK macro's context executed earlier than world subsystems are initialized."); \
		} \
	} \

#define TA_REGISTER_TICK_TIMESLICED(Handle, Object, Func, TickingGroup, Category, FuncGroup, Interval) \
	{ \
		if (UTickAggregatorWorldSubsystem* TA = GetWorld() ? GetWorld()->GetSubsystem<UTickAggregatorWorldSubsystem>() : nullptr) \
		{ \
			FAggregatedTickDelegate Delegate; \
			Delegate.BindUObject(Object, &ThisClass::Func); \
			Handle = TA->RegisterNativeObject(Object, Delegate, TickingGroup, ETickAggregatorTickCategory::Category, FuncGroup); \
		} \
		else \
		{ \
			TA_LOG(Warning, "TA_REGISTER_TICK macro's context executed earlier than world subsystems are initialized."); \
		} \
	} \

#define TA_REGISTER_TICK_ROUNDROBIN(Handle, Object, Func, TickingGroup, Category, FuncGroup, Interval) \
	{ \
		if (UTickAggregatorWorldSubsystem* TA = GetWorld() ? GetWorld()->GetSubsystem<UTickAggregatorWorldSubsystem>() : nullptr) \
		{ \
			FAggregatedTickDelegate Delegate; \
			Delegate.BindUObject(Object, &ThisClass::Func); \
			Handle = TA->RegisterNativeObject(Object, Delegate, TickingGroup, ETickAggregatorTickCategory::Category, FuncGroup); \
		} \
		else \
		{ \
			TA_LOG(Warning, "TA_REGISTER_TICK macro's context executed earlier than world subsystems are initialized."); \
		} \
	} \

#define TA_REMOVE_TICK(Handle) \
	{ \
		if (UTickAggregatorWorldSubsystem* TA = GetWorld() ? GetWorld()->GetSubsystem<UTickAggregatorWorldSubsystem>() : nullptr) \
		{ \
			TA->RemoveNativeObject(Handle); \
		} \
		else \
		{ \
			TA_LOG(Warning, "TA_REMOVE_TICK macro's context executed earlier than world subsystems are initialized."); \
		} \
	} \

#define TA_REGISTER_SCRIPT_TICK(Object, TickCategory, TickingGroup) \
	{ \
		if (UTickAggregatorWorldSubsystem* TA = GetWorld() ? GetWorld()->GetSubsystem<UTickAggregatorWorldSubsystem>() : nullptr) \
		{ \
			TA->RegisterBlueprintObject(Object, Delegate, TickingGroup, ETickAggregatorTickCategory::Category); \
		} \
		else \
		{ \
			TA_LOG(Warning, "TA_REGISTER_TICK macro's context executed earlier than world subsystems are initialized."); \
		} \
	} \


/**
 * Tick order for each FAggregatedTickFunction.
 * The idea of tick categories is to help users organize the order of the execution of tick functions.
 */
UENUM(BlueprintType, DisplayName = "Tick Order")
namespace ETickAggregatorTickCategory
{
	enum Type
	{
		/* Unordered objects are being ticked right after TC_ECHO in a random order, which means unlike in other categories, objects wont be
		 * sorted by their class types to execute same instructions over and over to help CPU to improve caching efficiency.
		 *
		 * Unordered category is mostly useful for singleton classes and their use case is mostly to avoid default tick manager overhead.
		 * Even though it wont provide magical performance increase, it's still helpful and gives a little bit of push to open space
		 * for other things since default tick manager goes through tons of different steps to execute tick tasks.
		 * So essentially using TC_UNORDERED will reduce amounts of registered FTickFunctions in your projects and gives a slight improvement. */
		TC_UNORDERED,


		TC_ALPHA, /** <-- ticks first. */
		TC_BRAVO,
		TC_CHARLIE,
		TC_DELTA,
		TC_ECHO, /** <-- ECHO is pretty much a great category for any regular stuff. */
		TC_FOXTROT, 
		TC_GOLF, 
		TC_HOTEL,
		TC_INDIA, /** <-- ticks last */

		/* Represents "invalid category". Do not select this as a tick category. */
		TC_MAX
	};
}

namespace ETickAggregatorTickCategory
{
	/**
	* Aliases for tick categories:
	*/

	/** Earliest tick group. Gameplay code related code shouldnt usually be in here. */
	static constexpr Type EARLIEST = TC_ALPHA;
	/** Latest tick group. This category preferably should be used after everyting related gameplay is done to post-process things */
	static constexpr Type LATEST = TC_INDIA;
	/** Default tick category. */
	static constexpr Type DEFAULT = TC_ECHO;

	/*
	 * You can add aliases to tick categories like this by extending the namespace in your project module:
	 * static constexpr Type MOVEMENT = TC_DELTA; // the category where we run movement code in components etc
	 * static constexpr Type PARTICLES = TC_CHARLIE; // the category where to update particle system properties after movement is done etc
	 * static constexpr Type ANIMATION = TC_CHARLIE; // the category where we update animation instances after characters moved and updated their state etc
	 * static constexpr Type ASYNC_BEGIN = TC_ALPHA; // the category where we fire all async tasks before doing any gameplay logic etc
	 *
	 * so you can use ETickAggregatorTickCategory::PARTICLES or ETickAggregatorTickCategory::ANIMATION to register tick functions to TC_CHARLIE
	 * just some examples. Basically it's just a cool way to assign aliases to tick categories.
	 */
}

/*
 * Function handles are similar to timer handles, they hold the information of where is the tick function
 * being stored in the FTickAggregatorNativeObjectArray. User should save this in the owner object of the given
 * tick function to manage the state of the registered tick function via tick aggregator subsystem.
 */
struct FTickAggregatorFunctionHandle
{
	FTickAggregatorFunctionHandle() {}
	FTickAggregatorFunctionHandle(const int32 InIndex, const ETickAggregatorTickCategory::Type InTickCategory, TEnumAsByte<ETickingGroup> InTickingGroup, TSubclassOf<UObject> InClassType, const class FName InIdentity)
		: ClassType(InClassType), Definition(InIdentity), Index(InIndex), TickCategory(InTickCategory), TickingGroup(InTickingGroup)
	{
	}

	FORCEINLINE bool IsValid() const { return Index != INDEX_NONE && TickCategory != TC_MAX && TickingGroup != TG_MAX && ClassType != nullptr; }

	FORCEINLINE FAggregatedTickDelegate GetTickFunction() const { return TickFunction; }
	FORCEINLINE FName GetIdentity() const { return Definition; }
	FORCEINLINE int32 GetIndex() const { return Index; }
	FORCEINLINE ETickAggregatorTickCategory::Type GetTickCategory() const { return TickCategory; }
	FORCEINLINE TEnumAsByte<ETickingGroup> GetTickingGroup() const { return TickingGroup; }
	FORCEINLINE TSubclassOf<UObject> GetClassType() const { return ClassType; }

	bool operator==(const FTickAggregatorFunctionHandle& Other) const
	{
		return Other.Index == Index
			&& Other.TickingGroup == TickingGroup
			&& Other.TickCategory == TickCategory
			&& Other.ClassType == ClassType
			&& Other.Definition == Definition
			&& Other.TickFunction.GetHandle() == TickFunction.GetHandle();
	}

private:

	FAggregatedTickDelegate TickFunction;
	TSubclassOf<UObject> ClassType = nullptr;
	FName Definition = NAME_None;
	int32 Index = INDEX_NONE;
	ETickAggregatorTickCategory::Type TickCategory = ETickAggregatorTickCategory::TC_MAX;
	TEnumAsByte<ETickingGroup> TickingGroup = TG_MAX;
};

namespace Intax
{
	namespace TA // Tick Aggregator
	{
		namespace Private
		{
			static TWeakObjectPtr<UWorld> CachedGameWorld;

			/** Reference to UTickAggregatorWorldSubsystem which spawned by UWorld and sets this reference on PostInitialize() */
			static UTickAggregatorWorldSubsystem* SubsystemInstance;
		}
		
		using FTickAggregatorEvent = TMulticastDelegate<void(UTickAggregatorWorldSubsystem*)>;
		/** Bind to this delegate if you have a system relies on tick aggregator's initialization phase and you need a callback when it's initted. */
		static FTickAggregatorEvent OnTickAggregatorInitialized;
		/** Called when tick aggregator is deinitialized. */
		static FTickAggregatorEvent OnTickAggregatorDeinitialized;

		static const char* BlueprintTickFunctionName = "BlueprintAggregatedTick";

		static const char* DefaultTickFunctionCategory = "Default";
		static const char* InvalidTickFunctionCategory = "NONE";

		/* Blueprint VM takes a "void*" (anonymous data) to invoke Blueprint functions with parameters. Since we know our
		 * Blueprint tick function only takes a float variable as DeltaSeconds, we just need to have a struct that ProcessEvent()
		 * function can access a float variable inside of it.
		 * @todo maybe move this to header file of FAggregatedTickFunction? */
		struct TickAggregatorDeltaSecondsParam
		{
			TickAggregatorDeltaSecondsParam() = delete;
			TickAggregatorDeltaSecondsParam(float InDeltaTime) : DeltaTime(InDeltaTime) {}
			float DeltaTime;
		};

		static bool IsBlueprintObject(const UObject* Object)
		{
			if (!Object)
			{
				return false;
			}

			const UClass* Class = Object->GetClass();
			return Cast<UBlueprintGeneratedClass>(Class) != nullptr;
		}

		/** We check if given blueprint object has any BlueprintAggregatedTick in it's graph. */
		static bool DoesObjectImplementBlueprintTickFunction(const UObject* Object)
		{
			if (!Object)
			{
				return false;
			}

			const UClass* Class = Object->GetClass();
			const UFunction* Func = IsBlueprintObject(Object) ? Class->FindFunctionByName(BlueprintTickFunctionName, EIncludeSuperFlag::ExcludeSuper) : nullptr;
			return IsValid(Func) ? Func->GetOuter() == Class : false;
		}

		/** We check if given blueprint object is derived from a native class that implements ITickAggregatorInterface. */
		static bool DoesBlueprintObjectHaveValidNativeClass(const UObject* Object)
		{
			if (!Object)
			{
				return false;
			}

			UClass* Class = Object->GetClass();
			const bool bIsBlueprint = Cast<UBlueprintGeneratedClass>(Class) != nullptr;
			if (!bIsBlueprint)
			{
				return false;
			}

			// walk over reflection hierarchy to find native super class.
			bool bExitWhile = false;
			UClass* LastSuperClass = Class;
			while (!bExitWhile)
			{
				if (!LastSuperClass)
				{
					bExitWhile = true;
					continue;
				}

				LastSuperClass = LastSuperClass->GetSuperClass();
				if (LastSuperClass && LastSuperClass->IsNative())
				{
					bExitWhile = true;
				}
			}

			return LastSuperClass != nullptr;
		}

		static FTickAggregatorFunctionHandle MakeFunctionHandle(const int32 InIndex, ETickAggregatorTickCategory::Type InTickCategory, ETickingGroup InTickingGroup, TSubclassOf<UObject> InClassType, const FName InIdentity)
		{
			return FTickAggregatorFunctionHandle(InIndex, InTickCategory, InTickingGroup, InClassType, InIdentity);
		}

		static FTickAggregatorFunctionHandle MakeInvalidFunctionHandle()
		{
			return FTickAggregatorFunctionHandle(-1, ETickAggregatorTickCategory::TC_MAX, TG_MAX, nullptr, NAME_None);
		}
	}
}



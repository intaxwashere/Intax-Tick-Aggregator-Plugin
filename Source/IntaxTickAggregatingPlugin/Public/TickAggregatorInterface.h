// Copyright INTAX Interactive, all rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "TickAggregatorWorldSubsystem.h"
#include "UObject/Interface.h"
#include "TickAggregatorInterface.generated.h"

// disables all component's actor's own tick function that inherit ITickAggregatorInterface
// @todo probably GetComponents() is not valid on ctor..

#define SETUP_AGGREGATED_TICK_CTOR() \
	this->PrimaryActorTick.bCanEverTick = false; \
	for (UActorComponent* Component : GetComponents()) \
	{ \
		if (IsValid(Component) && Component->Implements<UTickAggregatorInterface>()) \
		{ \
			Component->PrimaryComponentTick.bCanEverTick = false; \
		} \
	} \

UINTERFACE()
class INTAXTICKAGGREGATINGPLUGIN_API UTickAggregatorInterface : public UInterface
{
	GENERATED_BODY()
};

class UTickAggregatorWorldSubsystem;

/**
 * Tick aggregator interface that lets users tweak how owner object's tick settings for tick aggregator
 * and provides a virtual function to implement tick code inside of it, that tick aggregator calls.
 */
class INTAXTICKAGGREGATINGPLUGIN_API ITickAggregatorInterface
{
	GENERATED_BODY()

	/** Gotta let tick aggregator access private members. */
	friend UTickAggregatorWorldSubsystem;
	
public:

	/** Override this and write your tick code inside of this. */
	
	virtual void AggregatedTick(float DeltaTime) {  }

	/** Blueprint tick that is called in a separate loop than native tick functions. So unlike Unreal Engine's default implementation,
	 * child BP class' this tick function won't directly be called after it's owner's native tick function. */
	
	UFUNCTION(BlueprintImplementableEvent, DisplayName="Aggregated Tick")
	void BlueprintAggregatedTick(float DeltaTime);

	/** Should actor automatically registered to tick aggregator on spawn or if it's placed to level? */
	
	UFUNCTION(BlueprintNativeEvent)
	bool ShouldAutomaticallyRegisterActor() const;
	virtual bool ShouldAutomaticallyRegisterActor_Implementation() const { return false; }

	/** Should component be registered automatically after it's actor? */
	
	UFUNCTION(BlueprintNativeEvent)
	bool ShouldAutomaticallyRegisterComponent(UActorComponent* Component) const;
	virtual bool ShouldAutomaticallyRegisterComponent_Implementation(UActorComponent* Component) const { return true; }

	/** Return true if owner object does not care about instruction order. This is only useful if you have less than a few
	 * instances of the owner in the world. Ticking them without order by bypassing tick manager gives a slight optimization
	 * on the long run as the count of objects registered to unordered tick array increases since QueueTick function's cost is reducing. */
	
	UFUNCTION(BlueprintNativeEvent)
	bool ShouldTickAsUnordered() const;
	virtual bool ShouldTickAsUnordered_Implementation() const { return false; }

	/** Provide which tick group owner object should tick at. */
	
	UFUNCTION(BlueprintNativeEvent)
	ETickingGroup GetTickingGroup() const; //{ return TG_PostPhysics; }
	virtual ETickingGroup GetTickingGroup_Implementation() const { return TG_PostPhysics; }

	/** This function lets you override given component's ticking group (if owner is an actor). Return TG_MAX to ignore. */
	
	UFUNCTION(BlueprintNativeEvent)
	ETickingGroup OverrideTickingGroupForComponent(UActorComponent* ActorComponent) const; //{ return TG_MAX; }
	virtual ETickingGroup OverrideTickingGroupForComponent_Implementation(UActorComponent* ActorComponent) const { return TG_MAX; }

	/** Helper function to register object. */
	
	static void RegisterToAggregatedTick(UObject* This);
	/** Helper function to remove object. */
	
	static void RemoveFromAggregatedTick(UObject* This);

	/** Helper function to remove the object from system when it's destroyed. */
	
	static void NotifyObjectDestroyed(UWorld* World, UObject* Context);

	/** Destroy actor during tick function. Aggregator will save the object into an array and remove it on it's own tick function. */
	
	static void DestroyDuringTick(UWorld* World, UObject* Context);

private:
	/**
	 * EXPERIMENTAL - NOT UTILIZED YET
	 * Owner's index in the aggregated tick function's array.
	 */
	
	int32 AggregatedElementArrayIndex = INDEX_NONE;
	
};
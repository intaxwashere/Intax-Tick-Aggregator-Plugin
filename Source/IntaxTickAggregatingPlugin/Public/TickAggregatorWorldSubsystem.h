// Copyright INTAX Interactive, all rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "AggregatedTickFunction.h"
#include "TickAggregatorWorldSubsystem.generated.h"

class ITickAggregatorInterface;

/**
 * Tick aggregator subsystem that manages the FTickFunction's and handles registration/removal of objects to them.
 */
UCLASS(Config="TickAggregator")
class INTAXTICKAGGREGATINGPLUGIN_API UTickAggregatorWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	UTickAggregatorWorldSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void PostInitialize() override;
	virtual void Deinitialize() override;

	virtual void StartTickAggregator();
	
	bool HasAnyDerivedClasses() const;
	
	FTickAggregatorFunctionHandle RegisterNativeObject(const UObject* Object, const FAggregatedTickDelegate& Function, const ETickingGroup TickingGroup, ETickAggregatorTickCategory::Type Category, const FName TickFunctionGroup);
	bool RemoveNativeObject(const FTickAggregatorFunctionHandle& InHandle);

	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	bool RegisterBlueprintObject(UObject* Object, const ETickAggregatorTickCategory::Type TickCategory = ETickAggregatorTickCategory::TC_ECHO, const ETickingGroup TickingGroup = TG_PostPhysics);

	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	bool RemoveBlueprintObject(UObject* Object, const ETickAggregatorTickCategory::Type TickCategory, const ETickingGroup TickingGroup);

	/**
	 * Register object to the tick function with it's desired tick group.
	 * - Registration happens next frame.
	 * - If object is component, owner actor's relevant functions in ITickAggregatorInterface will be called to manage
	 * override some settings in component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void RegisterObject(UObject* Object);
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void RemoveObject(UObject* Object);

	/**
	 * Register actor AND IT'S ALL COMPONENTS IMPLEMENTS THE ITickAggregator INTERFACE.
	 * Actor will be removed from system automatically if it gets destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category="Tick Aggregator")
	void RegisterActor(AActor* SpawnedActor);
	void RemoveActor(AActor* Actor);

	/**
	 * Register an object without caring about instruction order in CPU. This is only useful to reduce the cost of
	 * QueueTicks function in TickManager, and provides advantage when number of unordered ticking objects is high in the world.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void RegisterUnorderedObject(UObject* Object);
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void RemoveUnorderedObject(UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void RegisterUnorderedActor(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void RemoveUnorderedActor(AActor* Actor);

	/** Remove the object during tick function.
	 * User MUST "return;" from their tick function after calling this.
	 * This function must be called to destroy any object inside of it's tick function. Do not call RemoveObject directly inside of AggregatedTick() */
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void NotifyRemoveRequestDuringTick(UObject* Object);
	
	/** Remove the object during tick function.
	 * User MUST "return;" from their tick function after calling this.
	 * This function must be called to destroy any object inside of it's tick function. Do not call RemoveObject directly inside of AggregatedTick() */
	UFUNCTION(BlueprintCallable, Category = "Tick Aggregator")
	void NotifyRemoveRequestDuringTickUnordered(UObject* Object);
	
	/** Called when UWorld spawns any actor. We check if spawned actor implements tick aggregator interface here to
	 * register it to subsystem. */
	UFUNCTION()
	virtual void OnActorSpawned(AActor* SpawnedActor);

	UFUNCTION()
	virtual void OnRegisteredActorDestroyed(AActor* DestroyedActor);
	
	/** Called when one of the registered actors get destroyed to remove from the ObjectMap. Uobjects has to
	 * manage theirselves on their own since we are only able to receive callbacks from actors when they are destroyed. */
	virtual void OnRegisteredObjectDestroyed(UObject* DestroyedObject);

	UFUNCTION()
	virtual void OnLevelAddedToWorld(ULevel* Level, UWorld* World);

	UFUNCTION()
	virtual void OnLevelRemovedFromWorld(ULevel* Level, UWorld* World);

	FAggregatedTickFunction* GetTickFunctionByObject(UObject* Object);
	
	FAggregatedTickFunction* GetTickFunctionByEnum(ETickingGroup TickingGroup);

	UFUNCTION(BlueprintCallable, Exec)
	void PrintAggregatedTickSubscriberCount();

	UFUNCTION(BlueprintCallable, Exec)
	void TickAggregatorDumpRegisteredObjects();

	TMap<TSoftClassPtr<UClass>, FTickAggregatorObjectArray> AggregatedObjectTickElements;

	/** Whether aggregator should iterate over all placed actors placed on the map, or should let user handle this logic? */
	UPROPERTY(Config)
	bool bAutomaticallyRegisterAllPlacedActorsOnLevel;

	FOnActorSpawned::FDelegate OnActorSpawnedHandle;

	/** Any item that needs to be executed before physics simulation starts. */
	FAggregatedTickFunction TickFunction_PrePhysics = FAggregatedTickFunction(TG_PrePhysics);

	/** Special tick group that starts physics simulation. */							
	FAggregatedTickFunction TickFunction_StartPhysics = FAggregatedTickFunction(TG_StartPhysics);

	/** Any item that can be run in parallel with our physics simulation work. */
	FAggregatedTickFunction TickFunction_DuringPhysics = FAggregatedTickFunction(TG_DuringPhysics);

	/** Special tick group that ends physics simulation. */
	FAggregatedTickFunction TickFunction_EndPhysics = FAggregatedTickFunction(TG_EndPhysics);

	/** Any item that needs rigid body and cloth simulation to be complete before being executed. */
	FAggregatedTickFunction TickFunction_PostPhysics = FAggregatedTickFunction(TG_PostPhysics);

	/** Any item that needs the update work to be done before being ticked. */
	FAggregatedTickFunction TickFunction_PostUpdateWork = FAggregatedTickFunction(TG_PostUpdateWork);

	/** Catchall for anything demoted to the end. */
	FAggregatedTickFunction TickFunction_LastDemotable = FAggregatedTickFunction(TG_LastDemotable);
	
};

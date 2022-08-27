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
#include "AggregatedTickFunction.h"
#include "TickAggregatorWorldSubsystem.generated.h"

class ITickAggregatorInterface;
DECLARE_STATS_GROUP(TEXT("Tick Aggregator"), STATGROUP_TickAggregator, STATCAT_Advanced);

/**
 * 
 */
UCLASS(Config="TickAggregator")
class INTAXTICKAGGREGATINGPLUGIN_API UTickAggregatorWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	UTickAggregatorWorldSubsystem();

	virtual void PostInitialize() override;

	/**
	 * Register object to the tick function with it's desired tick group.
	 * - Registration happens next frame.
	 * - If object is component, owner actor's relevant functions in ITickAggregatorInterface will be called to manage
	 * override some settings in component. */
	void RegisterObject(UObject* Object);
	void RemoveObject(UObject* Object);

	/**
	 * Register actor AND IT'S ALL COMPONENTS IMPLEMENTS THE ITickAggregator INTERFACE.
	 * Actor will be removed from system automatically if it gets destroyed. */
	void RegisterActor(AActor* SpawnedActor);
	void RemoveActor(AActor* Actor);

	/**
	 * Register an object without caring about instruction order in CPU. This is only useful to reduce the cost of
	 * QueueTicks function in TickManager, and provides advantage when number of unordered ticking objects is high in the world.
	 */
	void RegisterUnorderedObject(UObject* Object);
	void RemoveUnorderedObject(UObject* Object);

	void RegisterUnorderedActor(AActor* Actor);
	void RemoveUnorderedActor(AActor* Actor);

	/** Remove the object during tick function.
	 * User MUST "return;" from their tick function after calling this.
	 * This function must be called to destroy any object inside of it's tick function. Do not call RemoveObject directly inside of AggregatedTick() */
	void NotifyRemoveRequestDuringTick(UObject* Object);
	
	/** Remove the object during tick function.
	 * User MUST "return;" from their tick function after calling this.
	 * This function must be called to destroy any object inside of it's tick function. Do not call RemoveObject directly inside of AggregatedTick() */
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

	UPROPERTY()
	TMap<TSoftClassPtr<UClass>, FTickAggregatorObjectArray> AggregatedObjectTickElements;

	/** Whether aggregator should iterate over all placed actors placed on the map, or should let user handle this logic? */
	UPROPERTY(Config)
	bool bAutomaticallyRegisterAllPlacedActorsOnLevel;

	FOnActorSpawned::FDelegate OnActorSpawnedHandle;

	/** Any item that needs to be executed before physics simulation starts. */
	FAggregatedTickFunction TickFunction_PrePhysics;

	/** Special tick group that starts physics simulation. */							
	FAggregatedTickFunction TickFunction_StartPhysics;

	/** Any item that can be run in parallel with our physics simulation work. */
	FAggregatedTickFunction TickFunction_DuringPhysics;

	/** Special tick group that ends physics simulation. */
	FAggregatedTickFunction TickFunction_EndPhysics;

	/** Any item that needs rigid body and cloth simulation to be complete before being executed. */
	FAggregatedTickFunction TickFunction_PostPhysics;

	/** Any item that needs the update work to be done before being ticked. */
	FAggregatedTickFunction TickFunction_PostUpdateWork;

	/** Catchall for anything demoted to the end. */
	FAggregatedTickFunction TickFunction_LastDemotable;
	
};

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
#include "TickAggregatorWorldSubsystem.h"
#include "UObject/Interface.h"
#include "TickAggregatorInterface.generated.h"

// disables all component's actor's own tick function that inherit ITickAggregatorInterface 
#define SETUP_AGGREGATED_TICK_CTOR() \
	this->PrimaryActorTick.bCanEverTick = false; \
	for (UActorComponent* Component : GetComponents()) \
	{ \
		if (IsValid(Component) && Component->Implements<UTickAggregatorInterface>()) \
		{ \
			Component->PrimaryComponentTick.bCanEverTick = false; \
		} \
	} \

// This class does not need to be modified.
UINTERFACE()
class INTAXTICKAGGREGATINGPLUGIN_API UTickAggregatorInterface : public UInterface
{
	GENERATED_BODY()
};

class UTickAggregatorWorldSubsystem;
/**
 * 
 */
class INTAXTICKAGGREGATINGPLUGIN_API ITickAggregatorInterface
{
	GENERATED_BODY()

	friend UTickAggregatorWorldSubsystem;

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	virtual void AggregatedTick(float DeltaTime) {  }

	/** Blueprint tick that is called in a separate loop than native tick functions. So unlike Unreal Engine's default implementation,
	 * child BP class' this tick function won't directly be called after it's owner's tick function. */
	UFUNCTION(BlueprintImplementableEvent, DisplayName="Aggregated Tick")
	void BlueprintAggregatedTick(float DeltaTime);

	// @todo expose this to BPs properly...
	/*UFUNCTION(BlueprintCallable, Category="Tick Aggregator")
	static void BP_RemoveDuringTick(UObject* This);*/

	/** Helper function to register object. */
	void RegisterToAggregatedTick(UObject* This) const;
	/** Helper function to remove object. */
	void RemoveFromAggregatedTick(UObject* This) const;

	/** Helper function to remove the object from system when it's destroyed. */
	void NotifyObjectDestroyed(UWorld* World, UObject* Context) const;

	/** Should actor automatically registered to tick aggregator on spawn or if it's placed to level? */
	virtual bool ShouldAutomaticallyRegisterActor() const { return true; }

	/** Should component be registered automatically after it's actor? */
	virtual bool ShouldAutomaticallyRegisterComponent(UActorComponent* Component) const { return true; }

	/** Return true if owner object does not care about instruction order. This is only useful if you have less than a few
	 * instances of the owner in the world. Ticking them without order by bypassing tick manager gives a slight optimization
	 * on the long run as the count of objects registered to unordered tick array increases since QueueTick function's cost is reducing. */
	virtual bool ShouldTickAsUnordered() const { return false; }
	
	virtual ETickingGroup GetTickingGroup() const { return TG_PostPhysics; }

	virtual ETickingGroup OverrideTickingGroupForComponent(UActorComponent* ActorComponent) const { return TG_MAX; }

	/** Destroy actor during tick function. Aggregator will save the object into an array and remove it on it's own tick function. */
	void DestroyDuringTick(UWorld* World, UObject* Context) const;

private:
	/**
	 * EXPERIMENTAL - NOT UTILIZED YET
	 * Owner's index in the aggregated tick function's array.
	 */
	int32 AggregatedElementArrayIndex = INDEX_NONE;
	
};
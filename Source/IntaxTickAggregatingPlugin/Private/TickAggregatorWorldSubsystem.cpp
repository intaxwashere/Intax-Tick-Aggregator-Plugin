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

#include "TickAggregatorWorldSubsystem.h"
#include "EngineUtils.h"
#include "TickAggregatorInterface.h"

ENUM_RANGE_BY_FIRST_AND_LAST(ETickingGroup, TG_PrePhysics, TG_NewlySpawned);

UTickAggregatorWorldSubsystem::UTickAggregatorWorldSubsystem()
{
	bAutomaticallyRegisterAllPlacedActorsOnLevel = true;
}

void UTickAggregatorWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();

	const uint64 S = FPlatformTime::Cycles64();
	
	// world should be valid if we are in a world subsystem..
	check(GetWorld());

	// YOLO:
	
	TickFunction_PrePhysics.TickGroup = TG_PrePhysics;
	TickFunction_PrePhysics.RegisterTickFunction(GetWorld()->PersistentLevel);
	
	TickFunction_StartPhysics.TickGroup = TG_StartPhysics;
	TickFunction_StartPhysics.RegisterTickFunction(GetWorld()->PersistentLevel);
	
	TickFunction_DuringPhysics.TickGroup = TG_DuringPhysics;
	TickFunction_DuringPhysics.RegisterTickFunction(GetWorld()->PersistentLevel);
	
	TickFunction_EndPhysics.TickGroup = TG_EndPhysics;
	TickFunction_EndPhysics.RegisterTickFunction(GetWorld()->PersistentLevel);
	
	TickFunction_PostPhysics.TickGroup = TG_PostPhysics;
	TickFunction_PostPhysics.RegisterTickFunction(GetWorld()->PersistentLevel);
	
	TickFunction_PostUpdateWork.TickGroup = TG_PostUpdateWork;
	TickFunction_PostUpdateWork.RegisterTickFunction(GetWorld()->PersistentLevel);
	
	TickFunction_LastDemotable.TickGroup = TG_LastDemotable;
	TickFunction_LastDemotable.RegisterTickFunction(GetWorld()->PersistentLevel);

	if (bAutomaticallyRegisterAllPlacedActorsOnLevel)
	{
		int32 ImplementedActorCount = 0;
		// iterate all alive actors in the world and register them automatically if they implement the interface.
		for (AActor* Actor : TActorRange<AActor>(GetWorld(), AActor::StaticClass(), (EActorIteratorFlags::SkipPendingKill | EActorIteratorFlags::OnlyActiveLevels)))
		{
			if (IsValid(Actor) && Actor->Implements<UTickAggregatorInterface>())
			{
				const ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(Actor);
				if (Interface->ShouldAutomaticallyRegisterActor())
				{
					const bool bWantsUnorderedTick = Interface->ShouldTickAsUnordered();
					if (bWantsUnorderedTick)
					{
						RegisterUnorderedObject(Actor);
					}
					else
					{
						RegisterActor(Actor);
					}
					
					ImplementedActorCount++;
				}
			}
		}

		UE_LOG(LogTemp, Log, TEXT("Automatically added %d actors to Tick Aggregator World subsystem on PostInitialize period."), ImplementedActorCount);
	}

	// for dynamically spawned actors we need to use this delegate to get a callback from them.
	OnActorSpawnedHandle.BindUObject(this, &UTickAggregatorWorldSubsystem::OnActorSpawned);
	GetWorld()->AddOnActorSpawnedHandler(OnActorSpawnedHandle);

	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UTickAggregatorWorldSubsystem::OnLevelAddedToWorld);
	FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UTickAggregatorWorldSubsystem::OnLevelRemovedFromWorld);
	
	const uint64 E = FPlatformTime::Cycles64();
	UE_LOG(LogTemp, Log, TEXT("It took %f milliseconds to PostInitialize %s"), static_cast<float>(FPlatformTime::ToMilliseconds64(E - S)), *GetName());
}

void UTickAggregatorWorldSubsystem::RegisterObject(UObject* Object)
{
	const UWorld* World = GetWorld();

	// register the object in next frame because subsystem might be initialized before every actor (which is VERY possible)
	// so we won't have race condition issues. this could be solved with setting a boolean like "bWorldInitialized" etc.
	// but having a one frame delay affect nothing practically so not really worth the hassle.
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Object]
	{
		if (IsValid(Object) && ensure(Object->Implements<UTickAggregatorInterface>()))
		{
			const ITickAggregatorInterface* ObjectInterface = CastChecked<ITickAggregatorInterface>(Object);

			ETickingGroup TickingGroup;

			// if object is a component, we need to call it's owner's relevant functions to tweak/override it's settings.
			UActorComponent* Component = Cast<UActorComponent>(Object);
			if (Component && Component->GetOwner() && Component->GetOwner()->Implements<UTickAggregatorInterface>())
			{
				if (Component->IsComponentTickEnabled())
				{
					Component->SetComponentTickEnabled(false);
#if WITH_EDITOR
					UE_LOG(LogTemp, Warning, TEXT("Component %s had tick enabled. Prefer using SETUP_AGGREGATED_TICK_CTOR() on owning actor's constructor if it has an owner."));
#endif
				}
				
				const ITickAggregatorInterface* ComponentOwnerInterface = CastChecked<ITickAggregatorInterface>(Component->GetOwner());
				TickingGroup                                            = ComponentOwnerInterface->OverrideTickingGroupForComponent(Component);
				if (TickingGroup == TG_MAX)
				{
					TickingGroup = Component->PrimaryComponentTick.TickGroup;
				}
			}
			else 
			{
				TickingGroup = ObjectInterface->GetTickingGroup();
			}

			FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByEnum(TickingGroup);
			if (ensure(FoundTickFunction))
			{
				FoundTickFunction->AddNewObject(Object);
			}
			else
			{
				UE_LOG(LogTemp, Warning,
				       TEXT(
					       "(%s) Ticking Group '%s' is not allowed in Tick Aggregator Subsystem. Please use supported types instead. (Did you forgot to override GetTickingGroup() in interface?)"
				       ), *Object->GetName(), *UEnum::GetValueAsString(TickingGroup));
			}
		}
	}));
}

void UTickAggregatorWorldSubsystem::RemoveObject(UObject* Object)
{
	if (IsValid(Object)
#if WITH_EDITOR
		&& ensure(Object->Implements<UTickAggregatorInterface>())
#endif
		)
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->RemoveObject(Object);
	}
}

void UTickAggregatorWorldSubsystem::OnRegisteredObjectDestroyed(UObject* DestroyedObject)
{
	RemoveObject(DestroyedObject);
}

void UTickAggregatorWorldSubsystem::RegisterActor(AActor* SpawnedActor)
{
	RegisterObject(SpawnedActor);

	// look for actor's components too
	// components need to reg
	for (UActorComponent* Component : SpawnedActor->GetComponents())
	{
		if (IsValid(Component))
		{
			const bool bImplementsInterface = Component->Implements<UTickAggregatorInterface>();
			ITickAggregatorInterface* Interface = bImplementsInterface
				                                      ? CastChecked<ITickAggregatorInterface>(Component)
				                                      : nullptr;
			const bool bShouldAutoRegisterComponent = Interface
				                                          ? Interface->ShouldAutomaticallyRegisterComponent(Component)
				                                          : false;
			if (bShouldAutoRegisterComponent)
			{
				RegisterObject(Component);
			}
		}
	}
	
	SpawnedActor->OnDestroyed.AddUniqueDynamic(this, &UTickAggregatorWorldSubsystem::OnRegisteredActorDestroyed);
}

void UTickAggregatorWorldSubsystem::RemoveActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}
	
	RemoveObject(Actor);

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (IsValid(Component) && Component->Implements<UTickAggregatorInterface>())
		{
			RemoveObject(Component);
		}
	}
}

void UTickAggregatorWorldSubsystem::RegisterUnorderedObject(UObject* Object)
{
	// register it on next frame to let beginplay or other init functions run first.
	const UWorld* World = GetWorld();
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Object]
	{
		if (IsValid(Object))
		{
			FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
			check(FoundTickFunction);
			FoundTickFunction->AddNewUnorderedObject(Object);
		}
	}));
}

void UTickAggregatorWorldSubsystem::RemoveUnorderedObject(UObject* Object)
{
	if (IsValid(Object))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->RemoveUnorderedObject(Object);
	}
}

void UTickAggregatorWorldSubsystem::RegisterUnorderedActor(AActor* Actor)
{
	if (IsValid(Actor))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Actor);
		check(FoundTickFunction);
		FoundTickFunction->AddNewUnorderedObject(Actor);
	}
}

void UTickAggregatorWorldSubsystem::RemoveUnorderedActor(AActor* Actor)
{
	if (IsValid(Actor))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Actor);
		check(FoundTickFunction);
		FoundTickFunction->RemoveUnorderedObject(Actor);
	}
}

void UTickAggregatorWorldSubsystem::NotifyRemoveRequestDuringTick(UObject* Object)
{
	if (IsValid(Object))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->RemoveObjectOnNextTick(Object);
	}
}

void UTickAggregatorWorldSubsystem::NotifyRemoveRequestDuringTickUnordered(UObject* Object)
{
	if (IsValid(Object))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->RemoveUnorderedObjectOnNextTick(Object);
	}
}

void UTickAggregatorWorldSubsystem::OnActorSpawned(AActor* SpawnedActor)
{
	if (IsValid(SpawnedActor) && SpawnedActor->Implements<UTickAggregatorInterface>())
	{
		const ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(SpawnedActor);
		if (Interface->ShouldAutomaticallyRegisterActor())
		{
			RegisterActor(SpawnedActor);
		}
	}
}

void UTickAggregatorWorldSubsystem::OnLevelAddedToWorld(ULevel* Level, UWorld* World)
{
	if (IsValid(Level) && IsValid(World))
	{
		for (AActor* Actor : Level->Actors)
		{
			// @todo this might be quite expensive? 
			if (IsValid(Actor) && Actor->Implements<UTickAggregatorInterface>())
			{
				RegisterActor(Actor);
			}
		}
	}
}

void UTickAggregatorWorldSubsystem::OnLevelRemovedFromWorld(ULevel* Level, UWorld* World)
{
	if (IsValid(Level) && IsValid(World))
	{
		for (AActor* Actor : Level->Actors)
		{
			// @todo this might be quite expensive? 
			if (IsValid(Actor) && Actor->Implements<UTickAggregatorInterface>())
			{
				RemoveActor(Actor);
			}
		}
	}
}

FAggregatedTickFunction* UTickAggregatorWorldSubsystem::GetTickFunctionByObject(UObject* Object)
{
	const ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(Object);
	const ETickingGroup TickingGroup = Interface->GetTickingGroup();
	FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByEnum(TickingGroup);
	return FoundTickFunction;
}

FAggregatedTickFunction* UTickAggregatorWorldSubsystem::GetTickFunctionByEnum(ETickingGroup TickingGroup)
{
	switch (TickingGroup)
	{
	case TG_PrePhysics:
		return &TickFunction_PrePhysics;
	case TG_DuringPhysics:
		return &TickFunction_DuringPhysics;
	case TG_EndPhysics:
		return &TickFunction_EndPhysics;
	case TG_LastDemotable:
		return &TickFunction_LastDemotable;
	case TG_PostPhysics:
		return &TickFunction_PostPhysics;
	case TG_StartPhysics:
		return &TickFunction_StartPhysics;
	case TG_PostUpdateWork:
		return &TickFunction_PostUpdateWork;
	default:
		break;
	}
	
	return nullptr;
}

void UTickAggregatorWorldSubsystem::PrintAggregatedTickSubscriberCount()
{
#if !UE_BUILD_SHIPPING
	int32 Num = 0;
	for (const ETickingGroup TickGroup : TEnumRange<ETickingGroup>())
	{
		const FAggregatedTickFunction* TickFunction = GetTickFunctionByEnum(TickGroup);
		if (TickFunction)
		{
			Num += TickFunction->GetNum();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Count of objects in tick aggregator is %d"), Num);
#endif
}

void UTickAggregatorWorldSubsystem::OnRegisteredActorDestroyed(AActor* DestroyedActor)
{
	if (IsValid(DestroyedActor))
	{
		for (UActorComponent* ActorComponent : DestroyedActor->GetComponents())
		{
			if (IsValid(ActorComponent) && ActorComponent->Implements<UTickAggregatorInterface>())
			{
				OnRegisteredObjectDestroyed(ActorComponent);
			}
		}
	}
	
	OnRegisteredObjectDestroyed(DestroyedActor);
}




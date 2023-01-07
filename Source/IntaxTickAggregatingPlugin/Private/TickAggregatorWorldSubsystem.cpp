// Copyright INTAX Interactive, all rights reserved.

#include "TickAggregatorWorldSubsystem.h"
#include "EngineUtils.h"
#include "TickAggregatorInterface.h"

// @todo i dont know if I'm sinning by doing this in here...
ENUM_RANGE_BY_FIRST_AND_LAST(ETickingGroup, TG_PrePhysics, TG_NewlySpawned);

UTickAggregatorWorldSubsystem::UTickAggregatorWorldSubsystem()
{
	bAutomaticallyRegisterAllPlacedActorsOnLevel = true;
}

bool UTickAggregatorWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const bool bSuper = Super::ShouldCreateSubsystem(Outer);
	const bool bHasDerivedClasses = HasAnyDerivedClasses();

	// only create subsystem if super function allows us and if we dont have any child type.
	return bSuper && !bHasDerivedClasses;
}

void UTickAggregatorWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();

	// for dynamically spawned actors we need to use this delegate to get a callback from them.
	OnActorSpawnedHandle.BindUObject(this, &UTickAggregatorWorldSubsystem::OnActorSpawned);
	GetWorld()->AddOnActorSpawnedHandler(OnActorSpawnedHandle);

	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UTickAggregatorWorldSubsystem::OnLevelAddedToWorld);
	FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UTickAggregatorWorldSubsystem::OnLevelRemovedFromWorld);

	Intax::TA::Private::SubsystemInstance = this; // set the global reference to subsystem, this is used inside of the macros
	Intax::TA::Private::CachedGameWorld = CastChecked<UWorld>(GetOuter()); // UWorldSubsystem's outers are their UWorlds.
	// gamemode calls the world begin play so some games can delay it - we need to handle that case in here.
	if (Intax::TA::Private::CachedGameWorld->HasBegunPlay())
	{
		StartTickAggregator();
	}
	else
	{
		Intax::TA::Private::CachedGameWorld->OnWorldBeginPlay.AddUObject(this, &ThisClass::StartTickAggregator);
	}
	Intax::TA::OnTickAggregatorInitialized.Broadcast(this); // notify interested system about our initialization
}

void UTickAggregatorWorldSubsystem::Deinitialize()
{
	Intax::TA::Private::SubsystemInstance = nullptr;
	Intax::TA::OnTickAggregatorDeinitialized.Broadcast(this);
}

void UTickAggregatorWorldSubsystem::StartTickAggregator()
{
	const uint64 S = FPlatformTime::Cycles64();
	
	// world should be valid if we are in a world subsystem..
	check(GetWorld());
	
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

	// check if we can automatically register placed actors on the level.
	if (bAutomaticallyRegisterAllPlacedActorsOnLevel)
	{
		int32 ImplementedActorCount = 0;
		// iterate all alive actors in the world and register them automatically if they implement the interface.
		for (AActor* Actor : TActorRange<AActor>(GetWorld(), AActor::StaticClass(), (EActorIteratorFlags::SkipPendingKill | EActorIteratorFlags::OnlyActiveLevels)))
		{
			if (IsValid(Actor) && Actor->Implements<UTickAggregatorInterface>())
			{
				const ITickAggregatorInterface* Interface = CastChecked<ITickAggregatorInterface>(Actor);
				if (ITickAggregatorInterface::Execute_ShouldAutomaticallyRegisterActor(Actor))
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

	const uint64 E = FPlatformTime::Cycles64();
	UE_LOG(LogTemp, Log, TEXT("It took %f milliseconds to PostInitialize %s"), static_cast<float>(FPlatformTime::ToMilliseconds64(E - S)), *GetName());
}

bool UTickAggregatorWorldSubsystem::HasAnyDerivedClasses() const
{
	TArray<UClass*> OutClasses;
	GetDerivedClasses(GetClass(), OutClasses);
	return OutClasses.Num() > 0;
}

FTickAggregatorFunctionHandle UTickAggregatorWorldSubsystem::RegisterNativeObject(const UObject* Object, const FAggregatedTickDelegate& Function, const ETickingGroup TickingGroup, ETickAggregatorTickCategory::Type Category, const FName TickFunctionGroup)
{
	if (!IsValid(Object))
	{
		return Intax::TA::MakeInvalidFunctionHandle();
	}

	if (Category == ETickAggregatorTickCategory::TC_MAX)
	{
		return Intax::TA::MakeInvalidFunctionHandle();
	}

	if (TickingGroup == TG_MAX)
	{
		return Intax::TA::MakeInvalidFunctionHandle();
	}

	FAggregatedTickFunction* TickFunction = GetTickFunctionByEnum(TickingGroup);
	if (!ensure(TickFunction))
	{
		return Intax::TA::MakeInvalidFunctionHandle();
	}

	return TickFunction->RegisterNativeFunction(Object, Function, Category, TickFunctionGroup);
}

bool UTickAggregatorWorldSubsystem::RemoveNativeObject(const FTickAggregatorFunctionHandle& InHandle)
{
	if (!InHandle.IsValid())
	{
		return false;
	}

	const ETickingGroup TickingGroup = InHandle.GetTickingGroup();
	if (TickingGroup == TG_MAX)
	{
		return false;
	}

	const ETickAggregatorTickCategory::Type TickCategory = InHandle.GetTickCategory();
	if (TickCategory == ETickAggregatorTickCategory::TC_MAX)
	{
		return false;
	}

	FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByEnum(TickingGroup);
	if (ensure(FoundTickFunction))
	{
		return false;
	}

	const bool bUnordered = TickCategory == ETickAggregatorTickCategory::TC_UNORDERED;
	if (bUnordered)
	{
		return FoundTickFunction->RemoveUnorderedNativeFunction(InHandle);
	}
	else
	{
		return FoundTickFunction->RemoveNativeFunction(InHandle);
	}
}

bool UTickAggregatorWorldSubsystem::RegisterBlueprintObject(UObject* Object, const ETickAggregatorTickCategory::Type TickCategory, const ETickingGroup TickingGroup)
{
	if (!IsValid(Object) || TickCategory == ETickAggregatorTickCategory::TC_MAX || TickingGroup == TG_MAX || !Object->Implements<UTickAggregatorInterface>())
	{
		return false;
	}

	FAggregatedTickFunction* TickFunction = GetTickFunctionByEnum(TickingGroup);
	if (!TickFunction)
	{
		return false;
	}

	return TickFunction->RegisterBlueprintFunction(Object, TickCategory);
}

bool UTickAggregatorWorldSubsystem::RemoveBlueprintObject(UObject* Object, const ETickAggregatorTickCategory::Type TickCategory, const ETickingGroup TickingGroup)
{
	if (!IsValid(Object) || TickCategory == ETickAggregatorTickCategory::TC_MAX || TickingGroup == TG_MAX || !Object->Implements<UTickAggregatorInterface>())
	{
		return false;
	}

	FAggregatedTickFunction* TickFunction = GetTickFunctionByEnum(TickingGroup);
	if (!TickFunction)
	{
		return false;
	}

	//return TickFunction->RegisterBlueprintFunction(Object, TickCategory);
	return true;
}

void UTickAggregatorWorldSubsystem::RegisterObject(UObject* Object)
{
	const UWorld* World = GetWorld();

	// register the object in next frame because subsystem might be initialized before some actors (which is possible, i guess?)
	// so we won't have race condition issues. this could be solved with setting a boolean like "bWorldInitialized" etc.
	// but having a one frame delay affect nothing practically so not really worth the hassle.
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Object]
	{
		if (IsValid(Object) && ensure(Object->Implements<UTickAggregatorInterface>()))
		{
			ETickingGroup TickingGroup = TG_MAX;

			// if object is a component, we need to call it's owner's relevant functions to tweak/override it's settings.
			UActorComponent* Component = Cast<UActorComponent>(Object);
			if (Component && Component->GetOwner() && Component->GetOwner()->Implements<UTickAggregatorInterface>())
			{
				if (Component->IsComponentTickEnabled())
				{
					Component->SetComponentTickEnabled(false);
					UE_LOG(LogTemp, Warning, TEXT("Component %s had tick enabled. Prefer using SETUP_AGGREGATED_TICK_CTOR() on owning actor's constructor if it has an owner."), *Component->GetName());
				}

				// check if actor overrides this component's tick group
				const ETickingGroup OverrideTickGroup = ITickAggregatorInterface::Execute_OverrideTickingGroupForComponent(Object, Component);
				if (OverrideTickGroup == TG_MAX) // if it returns TG_MAX that means we can assume actor doesnt override tick group.
				{
					// check if component itself returned a specific tick group.
					const ETickingGroup ComponentTickGroup = ITickAggregatorInterface::Execute_GetTickingGroup(Component);
					// ensure its not TG_MAX either, if it is, use PrimaryComponentTick's default value. 
					TickingGroup = ComponentTickGroup != TG_MAX ? ComponentTickGroup : Component->PrimaryComponentTick.TickGroup;
				}
			}
			else // if its not a component, just get ticking group.
			{
				TickingGroup = ITickAggregatorInterface::Execute_GetTickingGroup(Object);
			}

			const bool bHasValidTickingGroup = TickingGroup != TG_MAX;
			ensureMsgf(bHasValidTickingGroup, TEXT("Could not receive a valid ticking group for object %s. TG_MAX is considered as invalid ticking group. (Did you forgot to override GetTickingGroup() in interface?)"), *Object->GetName());
			if (!bHasValidTickingGroup)
			{
				return;
			}

			FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByEnum(TickingGroup);
			check(FoundTickFunction);
			FoundTickFunction->Legacy_AddNewObject(Object);
			//FoundTickFunction->AddNewObject(Object);
		}
	}));
}

void UTickAggregatorWorldSubsystem::RemoveObject(UObject* Object)
{
	if (IsValid(Object) && ensure(Object->Implements<UTickAggregatorInterface>()))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->Legacy_RemoveObject(Object);
	}
}

void UTickAggregatorWorldSubsystem::OnRegisteredObjectDestroyed(UObject* DestroyedObject)
{
	RemoveObject(DestroyedObject);

	// @todo handle new system
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
			const bool bShouldAutoRegisterComponent = bImplementsInterface
				                                          ? ITickAggregatorInterface::Execute_ShouldAutomaticallyRegisterComponent(SpawnedActor, Component)
				                                          : false;
			if (bShouldAutoRegisterComponent)
			{
				RegisterObject(Component);
			}
		}
	}

	// check if ondestroyed is already bound, if not add new function.
	if (!SpawnedActor->OnDestroyed.IsAlreadyBound(this, &UTickAggregatorWorldSubsystem::OnRegisteredActorDestroyed))
	{
		SpawnedActor->OnDestroyed.AddUniqueDynamic(this, &UTickAggregatorWorldSubsystem::OnRegisteredActorDestroyed);
	}
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
			FoundTickFunction->Legacy_AddNewUnorderedObject(Object);
		}
	}));
}

void UTickAggregatorWorldSubsystem::RemoveUnorderedObject(UObject* Object)
{
	if (IsValid(Object))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->Legacy_RemoveUnorderedObject(Object);
	}
}

void UTickAggregatorWorldSubsystem::RegisterUnorderedActor(AActor* Actor)
{
	if (IsValid(Actor))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Actor);
		check(FoundTickFunction);
		FoundTickFunction->Legacy_AddNewUnorderedObject(Actor);
	}
}

void UTickAggregatorWorldSubsystem::RemoveUnorderedActor(AActor* Actor)
{
	if (IsValid(Actor))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Actor);
		check(FoundTickFunction);
		FoundTickFunction->Legacy_RemoveUnorderedObject(Actor);
	}
}

void UTickAggregatorWorldSubsystem::NotifyRemoveRequestDuringTick(UObject* Object)
{
	if (IsValid(Object))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->Legacy_RemoveObjectOnNextTick(Object);
	}
}

void UTickAggregatorWorldSubsystem::NotifyRemoveRequestDuringTickUnordered(UObject* Object)
{
	if (IsValid(Object))
	{
		FAggregatedTickFunction* FoundTickFunction = GetTickFunctionByObject(Object);
		check(FoundTickFunction);
		FoundTickFunction->Legacy_RemoveUnorderedObjectOnNextTick(Object);
	}
}

void UTickAggregatorWorldSubsystem::OnActorSpawned(AActor* SpawnedActor)
{
	if (IsValid(SpawnedActor) && SpawnedActor->Implements<UTickAggregatorInterface>())
	{
		if (ITickAggregatorInterface::Execute_ShouldAutomaticallyRegisterActor(SpawnedActor))
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
	const ETickingGroup TickingGroup = ITickAggregatorInterface::Execute_GetTickingGroup(Object);
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
			//Num += TickFunction->GetNum();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Count of objects in tick aggregator is %d"), Num);
#endif
}

void UTickAggregatorWorldSubsystem::TickAggregatorDumpRegisteredObjects()
{
#if !UE_BUILD_SHIPPING
	TickFunction_PrePhysics.DumpTicks();
	TickFunction_StartPhysics.DumpTicks(); 
	TickFunction_DuringPhysics.DumpTicks();
	TickFunction_EndPhysics.DumpTicks(); 
	TickFunction_PostPhysics.DumpTicks(); 
	TickFunction_PostUpdateWork.DumpTicks(); 
	TickFunction_LastDemotable.DumpTicks();
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




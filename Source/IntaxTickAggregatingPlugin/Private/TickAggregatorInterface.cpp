// Copyright INTAX Interactive, all rights reserved.


#include "TickAggregatorInterface.h"
#include "TickAggregatorWorldSubsystem.h"

void ITickAggregatorInterface::RegisterToAggregatedTick(UObject* This)
{
	const UWorld* World = IsValid(This) ? This->GetWorld() : nullptr;
	if (ensure(IsValid(World)))
	{
		UTickAggregatorWorldSubsystem* WorldSubsystem = World->GetSubsystem<UTickAggregatorWorldSubsystem>();
		if (AActor* Actor = Cast<AActor>(This))
		{
			WorldSubsystem->RegisterActor(Actor);
		}
		else
		{
			WorldSubsystem->RegisterObject(This);
		}
	}
}

void ITickAggregatorInterface::RemoveFromAggregatedTick(UObject* This)
{
	const UWorld* World = IsValid(This) ? This->GetWorld() : nullptr;
	if (ensure(IsValid(World)))
	{
		UTickAggregatorWorldSubsystem* WorldSubsystem = World->GetSubsystem<UTickAggregatorWorldSubsystem>();
		if (AActor* Actor = Cast<AActor>(This))
		{
			WorldSubsystem->RemoveActor(Actor);
		}
		else
		{
			WorldSubsystem->RemoveObject(This);
		}
	}
}

void ITickAggregatorInterface::NotifyObjectDestroyed(UWorld* World, UObject* Context)
{
	if (IsValid(World) && IsValid(Context))
	{
		UTickAggregatorWorldSubsystem* WorldSubsystem = World->GetSubsystem<UTickAggregatorWorldSubsystem>();
		WorldSubsystem->OnRegisteredObjectDestroyed(Context);
	}
}

void ITickAggregatorInterface::DestroyDuringTick(UWorld* World, UObject* Context)
{
	if (IsValid(World) && IsValid(Context))
	{
		UTickAggregatorWorldSubsystem* WorldSubsystem = World->GetSubsystem<UTickAggregatorWorldSubsystem>();
		WorldSubsystem->NotifyRemoveRequestDuringTick(Context);
	}
}

#include "WidgetTransitionAsyncAction.h"

#include "Engine/Engine.h"
#include "WidgetTransitionSubsystem.h"

UWidgetTransitionAsyncAction* UWidgetTransitionAsyncAction::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	UWidgetTransitionAsyncAction* Action = NewObject<UWidgetTransitionAsyncAction>();
	Action->PendingTransition = MoveTemp(Transition);
	Action->EventValue = Action->PendingTransition.bUseFrom ? Action->PendingTransition.FromValue : FWidgetTransitionValue();
	Action->EventValue.Type = Action->PendingTransition.ToValue.Type;
	Action->WorldContextObject = WorldContextObject;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UWidgetTransitionAsyncAction::Activate()
{
	FWidgetTransitionCallbacks Callbacks;
	const UObject* Context = WorldContextObject.Get();
	if (!IsValid(Context))
	{
		Finished.Broadcast(EventValue);
		SetReadyToDestroy();
		return;
	}
	if (Started.IsBound())
	{
		Callbacks.OnStarted.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleStarted);
	}
	if (Updated.IsBound())
	{
		Callbacks.OnUpdated.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleUpdated);
	}
	// Finished must remain bound even when its execution output is unused so this action can release itself.
	Callbacks.OnFinished.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleFinished);
	const UWorld* World = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::LogAndReturnNull);
	if (UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr)
	{
		Subsystem->StartTransition(MoveTemp(PendingTransition), MoveTemp(Callbacks));
	}
}

void UWidgetTransitionAsyncAction::HandleStarted(FWidgetTransitionValue Value)
{
	EventValue = Value;
	Started.Broadcast(EventValue);
}

void UWidgetTransitionAsyncAction::HandleUpdated(FWidgetTransitionValue Value)
{
	EventValue = Value;
	Updated.Broadcast(EventValue);
}

void UWidgetTransitionAsyncAction::HandleFinished(FWidgetTransitionValue Value)
{
	EventValue = Value;
	Finished.Broadcast(EventValue);
	SetReadyToDestroy();
}

#if WITH_DEV_AUTOMATION_TESTS
bool UWidgetTransitionAsyncAction::InitializeUpdateForTesting(FWidgetTransition Transition)
{
	PendingTransition = MoveTemp(Transition);
	EventValue = PendingTransition.bUseFrom ? PendingTransition.FromValue : FWidgetTransitionValue();
	EventValue.Type = PendingTransition.ToValue.Type;
	return Updated.IsBound();
}

void UWidgetTransitionAsyncAction::DispatchUpdatedForTesting(FWidgetTransitionValue Value)
{
	HandleUpdated(Value);
}
#endif

#include "WidgetTransition.h"

#include "Components/Widget.h"
#include "WidgetTransitionPrivate.h"

UWidgetTransitionAsyncAction* UWidgetTransitionAsyncAction::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	UWidgetTransitionAsyncAction* Action = NewObject<UWidgetTransitionAsyncAction>();
	Action->PendingTransition = MoveTemp(Transition);
	Action->EventTargetValue = Action->PendingTransition.ToValue;
	Action->EventValue = Action->PendingTransition.bUseFrom ? Action->PendingTransition.FromValue : FWidgetTransitionValue();
	Action->EventValue.Type = Action->EventTargetValue.Type;
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
	UWidget* Widget = PendingTransition.Widget.Get();
	const bool bHasBinding = IsValid(Widget) && !PendingTransition.WidgetProperty.IsNone();
	const bool bResolved = bHasBinding && (WidgetTransitionPrivate::IsMaterialBinding(PendingTransition.WidgetProperty)
										   ? EventBinding.ResolveMaterial(Widget, WidgetTransitionPrivate::GetMaterialParameter(PendingTransition.WidgetProperty))
										   : EventBinding.Resolve(Widget, PendingTransition.WidgetProperty.ToString()));
	if (bResolved)
	{
		EventValue.Type = EventBinding.ValueType;
		EventTargetValue.Type = EventBinding.ValueType;
		RefreshEventValue(Widget);
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
	WidgetTransitionPrivate::StartTransition(Context, MoveTemp(PendingTransition), MoveTemp(Callbacks));
}

void UWidgetTransitionAsyncAction::RefreshEventValue(UWidget* Widget)
{
	FVector4f Channels;
	if (EventBinding.Read(Widget, Channels))
	{
		EventValue.Channels = Channels;
	}
}

void UWidgetTransitionAsyncAction::HandleStarted(UWidget* Widget)
{
	RefreshEventValue(Widget);
	Started.Broadcast(EventValue);
}

void UWidgetTransitionAsyncAction::HandleUpdated(FWidgetTransitionValue Value)
{
	EventValue = Value;
	Updated.Broadcast(EventValue);
}

void UWidgetTransitionAsyncAction::HandleFinished(UWidget* Widget)
{
	FVector4f Channels;
	if (EventBinding.Read(Widget, Channels))
	{
		EventValue.Channels = Channels;
	}
	else
	{
		EventValue = EventTargetValue;
	}
	Finished.Broadcast(EventValue);
	SetReadyToDestroy();
}

#if WITH_DEV_AUTOMATION_TESTS
bool UWidgetTransitionAsyncAction::InitializeUpdateForTesting(FWidgetTransition Transition)
{
	PendingTransition = MoveTemp(Transition);
	EventTargetValue = PendingTransition.ToValue;
	EventValue = PendingTransition.bUseFrom ? PendingTransition.FromValue : FWidgetTransitionValue();
	EventValue.Type = EventTargetValue.Type;
	EventBinding.Invalidate();
	return Updated.IsBound();
}

void UWidgetTransitionAsyncAction::DispatchUpdatedForTesting(FWidgetTransitionValue Value)
{
	HandleUpdated(Value);
}
#endif

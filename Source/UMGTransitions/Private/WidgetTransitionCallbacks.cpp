#include "WidgetTransitionCallbacks.h"

#include "Components/Widget.h"
#include "WidgetTransitionSubsystem.h"

void FWidgetTransitionCallbackStore::EnsureLinks(UWidgetTransitionSubsystem& Subsystem)
{
	if (Links.Num() < Subsystem.Transitions.Num())
	{
		Links.SetNum(Subsystem.Transitions.Num());
	}
}

void FWidgetTransitionCallbackStore::Register(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionCallbacks Callbacks)
{
	EnsureLinks(Subsystem);
	const FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
	FWidgetTransitionCallbackLinks& TransitionLinks = Links[TransitionIndex];
	const bool bNeedsUpdateState = Callbacks.OnUpdated.IsBound() || Callbacks.OnUpdatedNative.IsBound() || (Transition.bBound && Transition.PropertyBinding.IsFieldNotify());
	if (Callbacks.OnStarted.IsBound() || Callbacks.OnFinished.IsBound() || Callbacks.OnStartedNative.IsBound() || Callbacks.OnFinishedNative.IsBound())
	{
		FWidgetTransitionLifecycleCallbacks LifecycleRecord;
		LifecycleRecord.OnStarted = MoveTemp(Callbacks.OnStarted);
		LifecycleRecord.OnFinished = MoveTemp(Callbacks.OnFinished);
		LifecycleRecord.OnStartedNative = MoveTemp(Callbacks.OnStartedNative);
		LifecycleRecord.OnFinishedNative = MoveTemp(Callbacks.OnFinishedNative);
		LifecycleRecord.TransitionIndex = TransitionIndex;
		TransitionLinks.LifecycleIndex = LifecycleCallbacks.Emplace(MoveTemp(LifecycleRecord));
	}
	if (bNeedsUpdateState)
	{
		FWidgetTransitionUpdateState UpdateState;
		UpdateState.OnUpdated = MoveTemp(Callbacks.OnUpdated);
		UpdateState.OnUpdatedNative = MoveTemp(Callbacks.OnUpdatedNative);
		UpdateState.TransitionIndex = TransitionIndex;
		UpdateState.bFieldNotify = Transition.bBound && Transition.PropertyBinding.IsFieldNotify();
		TransitionLinks.UpdateStateIndex = UpdateStates.Emplace(MoveTemp(UpdateState));
	}
}

void FWidgetTransitionCallbackStore::Remove(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex)
{
	EnsureLinks(Subsystem);
	const FWidgetTransitionCallbackLinks RemovedLinks = Links[TransitionIndex];
	if (RemovedLinks.UpdateStateIndex != INDEX_NONE)
	{
		const int32 StateIndex = RemovedLinks.UpdateStateIndex;
		UpdateStates.RemoveAtSwap(StateIndex);
		if (StateIndex < UpdateStates.Num())
		{
			FWidgetTransitionUpdateState& MovedState = UpdateStates[StateIndex];
			Links[MovedState.TransitionIndex].UpdateStateIndex = StateIndex;
		}
	}
	if (RemovedLinks.LifecycleIndex != INDEX_NONE)
	{
		const int32 LifecycleIndex = RemovedLinks.LifecycleIndex;
		LifecycleCallbacks.RemoveAtSwap(LifecycleIndex);
		if (LifecycleIndex < LifecycleCallbacks.Num())
		{
			Links[LifecycleCallbacks[LifecycleIndex].TransitionIndex].LifecycleIndex = LifecycleIndex;
		}
	}
	Links.RemoveAtSwap(TransitionIndex);
	if (TransitionIndex < Links.Num())
	{
		const FWidgetTransitionCallbackLinks& MovedLinks = Links[TransitionIndex];
		if (MovedLinks.LifecycleIndex != INDEX_NONE)
		{
			LifecycleCallbacks[MovedLinks.LifecycleIndex].TransitionIndex = TransitionIndex;
		}
		if (MovedLinks.UpdateStateIndex != INDEX_NONE)
		{
			UpdateStates[MovedLinks.UpdateStateIndex].TransitionIndex = TransitionIndex;
		}
	}
}

bool FWidgetTransitionCallbackStore::IsPendingRemoval(const UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex) const
{
	return Subsystem.PendingRemovalIndices.Contains(TransitionIndex);
}

void FWidgetTransitionCallbackStore::QueueStarted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex)
{
	EnsureLinks(Subsystem);
	const int32 LifecycleIndex = Links[TransitionIndex].LifecycleIndex;
	if (LifecycleIndex == INDEX_NONE || !LifecycleCallbacks.IsValidIndex(LifecycleIndex))
	{
		return;
	}
	const FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
	const FWidgetTransitionLifecycleCallbacks& Callbacks = LifecycleCallbacks[LifecycleIndex];
	if (Callbacks.OnStarted.IsBound())
	{
		FWidgetTransitionValue Value = Transition.FromValue;
		Value.Type = Transition.ToValue.Type;
		StartedEvents.Add({ Callbacks.OnStarted, Callbacks.OnStartedNative, MoveTemp(Value), Transition.Widget, false });
	}
}

void FWidgetTransitionCallbackStore::StoreOverrideValue(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value)
{
	const int32 UpdateStateIndex = Links[TransitionIndex].UpdateStateIndex;
	if (UpdateStateIndex == INDEX_NONE)
	{
		return;
	}
	FWidgetTransitionUpdateState& UpdateState = UpdateStates[UpdateStateIndex];
	UpdateState.OverrideValue = MoveTemp(Value);
	UpdateState.bHasOverrideSample = true;
}

void FWidgetTransitionCallbackStore::QueueCompleted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value)
{
	EnsureLinks(Subsystem);
	const FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
	const FWidgetTransitionCallbackLinks& TransitionLinks = Links[TransitionIndex];
	if (TransitionLinks.UpdateStateIndex != INDEX_NONE && UpdateStates.IsValidIndex(TransitionLinks.UpdateStateIndex))
	{
		const FWidgetTransitionUpdateState& UpdateState = UpdateStates[TransitionLinks.UpdateStateIndex];
		if (UpdateState.OnUpdated.IsBound())
		{
			FinalUpdatedEvents.Add({ UpdateState.OnUpdated, UpdateState.OnUpdatedNative, Value });
		}
	}
	if (TransitionLinks.LifecycleIndex != INDEX_NONE && LifecycleCallbacks.IsValidIndex(TransitionLinks.LifecycleIndex))
	{
		const FWidgetTransitionLifecycleCallbacks& LifecycleRecord = LifecycleCallbacks[TransitionLinks.LifecycleIndex];
		FinishedEvents.Add({ LifecycleRecord.OnFinished, LifecycleRecord.OnFinishedNative, MoveTemp(Value), Transition.Widget, Transition.bRemoveFromParent });
	}
	else if (Transition.bRemoveFromParent)
	{
		FinishedEvents.Add({ {}, {}, MoveTemp(Value), Transition.Widget, true });
	}
}

void FWidgetTransitionCallbackStore::DispatchLifecycleEvents(TArray<FWidgetTransitionLifecycleEvent>& Events)
{
	for (const FWidgetTransitionLifecycleEvent& Event : Events)
	{
		UWidget* Widget = Event.Widget.Get();
		const FOnWidgetTransitionUpdate Callback = Event.Callback;
		Callback.ExecuteIfBound(Event.Value);
		Event.NativeCallback.ExecuteIfBound(Event.Value);
		if (Event.bRemoveFromParent && IsValid(Widget))
		{
			Widget->RemoveFromParent();
		}
	}
	Events.Reset();
}

void FWidgetTransitionCallbackStore::DispatchFinalUpdatedCallbacks()
{
	for (const FWidgetTransitionUpdateEvent& Event : FinalUpdatedEvents)
	{
		const FOnWidgetTransitionUpdate Callback = Event.Callback;
		Callback.ExecuteIfBound(Event.Value);
		Event.NativeCallback.ExecuteIfBound(Event.Value);
	}
	FinalUpdatedEvents.Reset();
}

void FWidgetTransitionCallbackStore::TickUpdateStates(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
{
	for (int32 StateIndex = 0; StateIndex < UpdateStates.Num(); ++StateIndex)
	{
		FWidgetTransitionUpdateState& UpdateState = UpdateStates[StateIndex];
		if (!Subsystem.Transitions.IsValidIndex(UpdateState.TransitionIndex) || IsPendingRemoval(Subsystem, UpdateState.TransitionIndex))
		{
			continue;
		}
		FWidgetTransition& Transition = Subsystem.Transitions[UpdateState.TransitionIndex];
		if (!Transition.bStarted || Transition.CurrentTime < Transition.Delay)
		{
			continue;
		}
		const bool bDispatchUpdate = Transition.UpdateInterval <= 0.0f || (UpdateState.UpdateElapsed += DeltaTime) >= Transition.UpdateInterval;
		const bool bHasOverrideSample = UpdateState.bHasOverrideSample;
		FWidgetTransitionValue OverrideValue;
		if (bHasOverrideSample)
		{
			OverrideValue = UpdateState.OverrideValue;
			UpdateState.bHasOverrideSample = false;
		}
		if (bDispatchUpdate)
		{
			UpdateState.UpdateElapsed = Transition.UpdateInterval <= 0.0f ? 0.0f : FMath::Fmod(UpdateState.UpdateElapsed, Transition.UpdateInterval);
			const FOnWidgetTransitionUpdate Updated = UpdateState.OnUpdated;
			const FOnWidgetTransitionNativeUpdate NativeUpdated = UpdateState.OnUpdatedNative;
			FWidgetTransitionValue Value = MoveTemp(OverrideValue);
			if (!bHasOverrideSample)
			{
				const FWidgetTransitionSample Sample = Subsystem.SampleTransition(Transition);
				Value.Channels = Sample.Value;
				Value.Type = Transition.ToValue.Type;
			}
			UWidget* Widget = Transition.Widget.Get();
			if (UpdateState.bFieldNotify)
			{
				if (Transition.PropertyBinding.Apply(Widget, Value.Channels, false))
				{
					Transition.PropertyBinding.BroadcastFieldNotify(Widget);
				}
			}
			Updated.ExecuteIfBound(Value);
			NativeUpdated.ExecuteIfBound(Value);
		}
	}
}

void FWidgetTransitionCallbackStore::TickAndDispatch(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
{
	DispatchLifecycleEvents(StartedEvents);
	TickUpdateStates(Subsystem, DeltaTime);
	DispatchFinalUpdatedCallbacks();
	DispatchLifecycleEvents(FinishedEvents);
}

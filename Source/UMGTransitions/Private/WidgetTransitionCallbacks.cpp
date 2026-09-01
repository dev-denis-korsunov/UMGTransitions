#include "WidgetTransitionCallbacks.h"

#include "Components/Widget.h"
#include "WidgetTransitionSubsystem.h"

namespace WidgetTransitionCallbacks
{
	void EnsureLinks(UWidgetTransitionSubsystem& Subsystem)
	{
		if (Subsystem.CallbackLinks.Num() < Subsystem.Transitions.Num())
		{
			Subsystem.CallbackLinks.SetNum(Subsystem.Transitions.Num());
		}
	}

	void Register(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionCallbacks Callbacks)
	{
		EnsureLinks(Subsystem);
		const FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
		FWidgetTransitionCallbackLinks& Links = Subsystem.CallbackLinks[TransitionIndex];
		const bool bNeedsUpdateState = Callbacks.OnUpdated.IsBound() || (Transition.bBound && Transition.PropertyBinding.IsFieldNotify());
		if (Callbacks.OnStarted.IsBound() || Callbacks.OnFinished.IsBound())
		{
			FWidgetTransitionLifecycleCallbacks LifecycleCallbacks;
			LifecycleCallbacks.OnStarted = MoveTemp(Callbacks.OnStarted);
			LifecycleCallbacks.OnFinished = MoveTemp(Callbacks.OnFinished);
			LifecycleCallbacks.TransitionIndex = TransitionIndex;
			Links.LifecycleIndex = Subsystem.LifecycleCallbacks.Emplace(MoveTemp(LifecycleCallbacks));
		}
		if (bNeedsUpdateState)
		{
			FWidgetTransitionUpdateState UpdateState;
			UpdateState.OnUpdated = MoveTemp(Callbacks.OnUpdated);
			UpdateState.TransitionIndex = TransitionIndex;
			UpdateState.bFieldNotify = Transition.bBound && Transition.PropertyBinding.IsFieldNotify();
			Links.UpdateStateIndex = Subsystem.UpdateStates.Emplace(MoveTemp(UpdateState));
		}
	}

	void Remove(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex)
	{
		EnsureLinks(Subsystem);
		const FWidgetTransitionCallbackLinks RemovedLinks = Subsystem.CallbackLinks[TransitionIndex];
		if (RemovedLinks.UpdateStateIndex != INDEX_NONE)
		{
			const int32 StateIndex = RemovedLinks.UpdateStateIndex;
			Subsystem.UpdateStates.RemoveAtSwap(StateIndex);
			if (StateIndex < Subsystem.UpdateStates.Num())
			{
				FWidgetTransitionUpdateState& MovedState = Subsystem.UpdateStates[StateIndex];
				Subsystem.CallbackLinks[MovedState.TransitionIndex].UpdateStateIndex = StateIndex;
			}
		}
		if (RemovedLinks.LifecycleIndex != INDEX_NONE)
		{
			const int32 LifecycleIndex = RemovedLinks.LifecycleIndex;
			Subsystem.LifecycleCallbacks.RemoveAtSwap(LifecycleIndex);
			if (LifecycleIndex < Subsystem.LifecycleCallbacks.Num())
			{
				Subsystem.CallbackLinks[Subsystem.LifecycleCallbacks[LifecycleIndex].TransitionIndex].LifecycleIndex = LifecycleIndex;
			}
		}
		Subsystem.CallbackLinks.RemoveAtSwap(TransitionIndex);
		if (TransitionIndex < Subsystem.CallbackLinks.Num())
		{
			const FWidgetTransitionCallbackLinks& MovedLinks = Subsystem.CallbackLinks[TransitionIndex];
			if (MovedLinks.LifecycleIndex != INDEX_NONE)
			{
				Subsystem.LifecycleCallbacks[MovedLinks.LifecycleIndex].TransitionIndex = TransitionIndex;
			}
			if (MovedLinks.UpdateStateIndex != INDEX_NONE)
			{
				Subsystem.UpdateStates[MovedLinks.UpdateStateIndex].TransitionIndex = TransitionIndex;
			}
		}
	}

	bool IsPendingRemoval(const UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex)
	{
		return Subsystem.PendingRemovalIndices.Contains(TransitionIndex);
	}

	void QueueStarted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex)
	{
		EnsureLinks(Subsystem);
		const int32 LifecycleIndex = Subsystem.CallbackLinks[TransitionIndex].LifecycleIndex;
		if (LifecycleIndex == INDEX_NONE || !Subsystem.LifecycleCallbacks.IsValidIndex(LifecycleIndex))
		{
			return;
		}
		const FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
		const FWidgetTransitionLifecycleCallbacks& Callbacks = Subsystem.LifecycleCallbacks[LifecycleIndex];
		if (Callbacks.OnStarted.IsBound())
		{
			FWidgetTransitionValue Value = Transition.FromValue;
			Value.Type = Transition.ToValue.Type;
			Subsystem.StartedCallbackEvents.Add({ Callbacks.OnStarted, MoveTemp(Value), Transition.Widget, false });
		}
	}

	void StoreOverrideValue(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value)
	{
		const int32 UpdateStateIndex = Subsystem.CallbackLinks[TransitionIndex].UpdateStateIndex;
		if (UpdateStateIndex == INDEX_NONE)
		{
			return;
		}
		FWidgetTransitionUpdateState& UpdateState = Subsystem.UpdateStates[UpdateStateIndex];
		UpdateState.OverrideValue = MoveTemp(Value);
		UpdateState.bHasOverrideSample = true;
	}

	void QueueCompleted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value)
	{
		EnsureLinks(Subsystem);
		const FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
		const FWidgetTransitionCallbackLinks& Links = Subsystem.CallbackLinks[TransitionIndex];
		if (Links.UpdateStateIndex != INDEX_NONE && Subsystem.UpdateStates.IsValidIndex(Links.UpdateStateIndex))
		{
			const FWidgetTransitionUpdateState& UpdateState = Subsystem.UpdateStates[Links.UpdateStateIndex];
			if (UpdateState.OnUpdated.IsBound())
			{
				Subsystem.FinalUpdatedCallbackEvents.Add({ UpdateState.OnUpdated, Value });
			}
		}
		if (Links.LifecycleIndex != INDEX_NONE && Subsystem.LifecycleCallbacks.IsValidIndex(Links.LifecycleIndex))
		{
			const FWidgetTransitionLifecycleCallbacks& LifecycleCallbacks = Subsystem.LifecycleCallbacks[Links.LifecycleIndex];
			Subsystem.FinishedCallbackEvents.Add({ LifecycleCallbacks.OnFinished, MoveTemp(Value), Transition.Widget, Transition.bRemoveFromParent });
		}
		else if (Transition.bRemoveFromParent)
		{
			Subsystem.FinishedCallbackEvents.Add({ {}, MoveTemp(Value), Transition.Widget, true });
		}
	}

	static void DispatchLifecycleEvents(TArray<FWidgetTransitionLifecycleEvent>& Events)
	{
		for (const FWidgetTransitionLifecycleEvent& Event : Events)
		{
			UWidget* Widget = Event.Widget.Get();
			const FOnWidgetTransitionUpdate Callback = Event.Callback;
			Callback.ExecuteIfBound(Event.Value);
			if (Event.bRemoveFromParent && IsValid(Widget))
			{
				Widget->RemoveFromParent();
			}
		}
		Events.Reset();
	}

	static void DispatchFinalUpdatedCallbacks(TArray<FWidgetTransitionUpdateEvent>& Events)
	{
		for (const FWidgetTransitionUpdateEvent& Event : Events)
		{
			const FOnWidgetTransitionUpdate Callback = Event.Callback;
			Callback.ExecuteIfBound(Event.Value);
		}
		Events.Reset();
	}

	static void TickUpdateStates(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
	{
		for (int32 StateIndex = 0; StateIndex < Subsystem.UpdateStates.Num(); ++StateIndex)
		{
			FWidgetTransitionUpdateState& UpdateState = Subsystem.UpdateStates[StateIndex];
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
			}
		}
	}

	void TickAndDispatch(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
	{
		DispatchLifecycleEvents(Subsystem.StartedCallbackEvents);
		TickUpdateStates(Subsystem, DeltaTime);
		DispatchFinalUpdatedCallbacks(Subsystem.FinalUpdatedCallbackEvents);
		DispatchLifecycleEvents(Subsystem.FinishedCallbackEvents);
	}
}

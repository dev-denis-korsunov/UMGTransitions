#pragma once

#include "CoreMinimal.h"
#include "WidgetTransition.h"

#include "WidgetTransitionCallbacks.generated.h"

class UWidgetTransitionSubsystem;
class UBlueprintAsyncActionBase;

UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetTransitionUpdate, FWidgetTransitionValue, Value);

/** Callback input package used while a transition is created. */
struct FWidgetTransitionCallbacks
{
	FOnWidgetTransitionUpdate OnStarted;
	FOnWidgetTransitionUpdate OnFinished;
	FOnWidgetTransitionUpdate OnUpdated;
	/** Async owner released on completion or cancellation. */
	TWeakObjectPtr<UBlueprintAsyncActionBase> AsyncOwner;
};

/** Rare lifecycle callbacks stored separately from the update hot path. */
struct FWidgetTransitionLifecycleCallbacks
{
	FOnWidgetTransitionUpdate OnStarted;
	FOnWidgetTransitionUpdate OnFinished;
	TWeakObjectPtr<UBlueprintAsyncActionBase> AsyncOwner;
	int32 TransitionIndex = INDEX_NONE;
};

/** Dense state for Updated callbacks and FieldNotify throttling. */
struct FWidgetTransitionUpdateState
{
	FOnWidgetTransitionUpdate OnUpdated;
	FWidgetTransitionValue OverrideValue;
	int32 TransitionIndex = INDEX_NONE;
	float UpdateElapsed = 0.0f;
	bool bFieldNotify = false;
	bool bHasOverrideSample = false;
};

/** Callback-system sidecar stored parallel to the transition array. */
struct FWidgetTransitionCallbackLinks
{
	int32 LifecycleIndex = INDEX_NONE;
	int32 UpdateStateIndex = INDEX_NONE;
};

/** Rare lifecycle callback copied into a short dispatch queue. */
struct FWidgetTransitionLifecycleEvent
{
	FOnWidgetTransitionUpdate Callback;
	FWidgetTransitionValue Value;
};

/** Final update callback copied before its completed transition is removed. */
struct FWidgetTransitionUpdateEvent
{
	FOnWidgetTransitionUpdate Callback;
	FWidgetTransitionValue Value;
};

/** Owns callback sidecars and transient dispatch queues for a subsystem. */
struct FWidgetTransitionCallbackStore
{
	void Register(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionCallbacks Callbacks);
	void EnsureLinks(UWidgetTransitionSubsystem& Subsystem);
	void Remove(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex);
	void QueueStarted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex);
	void StoreOverrideValue(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value);
	void QueueCompleted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value);
	void TickAndDispatch(UWidgetTransitionSubsystem& Subsystem, float DeltaTime);
	/** Dispatches starts queued after a transition handoff without advancing update timers. */
	void DispatchStartedEvents();

	TArray<FWidgetTransitionLifecycleCallbacks> LifecycleCallbacks;
	TArray<FWidgetTransitionUpdateState> UpdateStates;
	TArray<FWidgetTransitionCallbackLinks> Links;
	TArray<FWidgetTransitionLifecycleEvent> StartedEvents;
	TArray<FWidgetTransitionUpdateEvent> FinalUpdatedEvents;
	TArray<FWidgetTransitionLifecycleEvent> FinishedEvents;

private:
	bool IsPendingRemoval(const UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex) const;
	void DispatchLifecycleEvents(TArray<FWidgetTransitionLifecycleEvent>& Events);
	void DispatchFinalUpdatedCallbacks();
	void TickUpdateStates(UWidgetTransitionSubsystem& Subsystem, float DeltaTime);
};

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Spring.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "WidgetTransition.h"
#include "WidgetTransitionCallbacks.h"

#include "WidgetTransitionSubsystem.generated.h"

struct FWidgetTransitionSample
{
	FVector4f Value = FVector4f::Zero();
	bool bCompleted = false;
};

/** Transition and callbacks retained until an active transition for the same property finishes. */
struct FQueuedWidgetTransition
{
	FWidgetTransition Transition;
	FWidgetTransitionCallbacks Callbacks;
};

/** Identifies the independent FIFO queue for one widget property. */
struct FWidgetTransitionQueueKey
{
	FObjectKey Widget;
	FName WidgetProperty;

	FWidgetTransitionQueueKey() = default;
	FWidgetTransitionQueueKey(UWidget* InWidget, FName InWidgetProperty)
		: Widget(InWidget)
		, WidgetProperty(InWidgetProperty)
	{
	}

	bool operator==(const FWidgetTransitionQueueKey& Other) const
	{
		return Widget == Other.Widget && WidgetProperty == Other.WidgetProperty;
	}
};

FORCEINLINE uint32 GetTypeHash(const FWidgetTransitionQueueKey& Key)
{
	return HashCombine(GetTypeHash(Key.Widget), GetTypeHash(Key.WidgetProperty));
}

/** Amortized FIFO storage that does not shift its tail for every Pipe handoff. */
struct FWidgetTransitionQueue
{
	TArray<FQueuedWidgetTransition> Entries;
	int32 Head = 0;

	bool IsEmpty() const
	{
		return Head >= Entries.Num();
	}

	int32 Num() const
	{
		return Entries.Num() - Head;
	}

	void Enqueue(FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks)
	{
		Entries.Add({ MoveTemp(Transition), MoveTemp(Callbacks) });
	}

	FQueuedWidgetTransition Dequeue();

private:
	void Compact();
};

/** Owns and ticks all active widget transitions for one world. */
UCLASS()
class UMGTRANSITIONS_API UWidgetTransitionSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UWidgetTransitionSubsystem, STATGROUP_Tickables);
	}
	virtual bool IsTickableInEditor() const override
	{
		return true;
	}

	/** Normalizes and starts a transition in this world. */
	bool StartTransition(FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks = {});
	/** Requests removal of every transition owned by Widget. */
	void ClearTransitions(UWidget* Widget);
	/** Samples the current interpolated value and completion state. */
	FWidgetTransitionSample SampleTransition(const FWidgetTransition& Transition) const;

	/** Dense transition records; removal uses RemoveAtSwap and repairs spring-owner indices. */
	TArray<FWidgetTransition> Transitions;
	/** Dense spring simulation pass, stored separately from transition records. */
	TArray<FWidgetTransitionSpring> Springs;
	/** Owning transition array index for every entry in Springs. */
	TArray<int32> SpringTransitionIndices;
	/** Callback sidecars and transient dispatch queues. */
	FWidgetTransitionCallbackStore CallbackStore;
	/** Transition indices requested for removal during callback dispatch. */
	TArray<int32> PendingRemovalIndices;
	/** Independent FIFO queues, grouped by owning widget property. */
	TMap<FWidgetTransitionQueueKey, FWidgetTransitionQueue> QueuedTransitions;
	/** Defers structural mutation while transition and callback passes are active. */
	bool bDeferringTransitionRemovals = false;
	/** Prevents an admitted queued transition from re-queuing or clearing its siblings. */
	bool bStartingQueuedTransition = false;

#if WITH_DEV_AUTOMATION_TESTS
	/** Adds a transition and its callback package without requiring an initialized UWorld. */
	void AddTransitionForTesting(FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks);
	/** Invokes the transition hot path without requiring an initialized UWorld. */
	void TickTransitionsForTesting(float DeltaTime);
	/** Clears transitions through the same removal path used by Clear All Widget Transitions. */
	void ClearTransitionsForTesting(UWidget* Widget);
#endif

private:
	void RemoveSpring(FWidgetTransition& Transition);
	void RemoveTransition(int32 TransitionIndex);
	void RequestTransitionRemoval(int32 TransitionIndex);
	void FlushPendingRemovals();
	bool HasActiveTransition(UWidget* Widget, FName WidgetProperty) const;
	bool HasQueuedTransition(UWidget* Widget, FName WidgetProperty) const;
	void CancelQueuedTransitions(UWidget* Widget, FName WidgetProperty);
	void CancelQueuedTransitions(UWidget* Widget);
	bool StartQueuedTransitions();
	void StartSpring(int32 TransitionIndex, FWidgetTransition& Transition, FVector4f InitialVelocity = FVector4f::Zero());
	bool RestartTransition(int32 TransitionIndex, FWidgetTransition& Transition);
	void TickTransitions(float DeltaTime);
};

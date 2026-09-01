#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "WidgetTransition.h"

#include "WidgetTransitionAsyncAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWidgetTransitionAsyncValue, FWidgetTransitionValue, Value);

/** Blueprint async action which exposes transition lifecycle callbacks as execution outputs. */
UCLASS()
class UMGTRANSITIONS_API UWidgetTransitionAsyncAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncValue Started;
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncValue Updated;
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncValue Finished;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Add Widget Transition Async"), Category = "Widget Transition")
	static UWidgetTransitionAsyncAction* AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition);

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleStarted(FWidgetTransitionValue Value);
	UFUNCTION()
	void HandleUpdated(FWidgetTransitionValue Value);
	UFUNCTION()
	void HandleFinished(FWidgetTransitionValue Value);

	FWidgetTransition PendingTransition;
	FWidgetTransitionValue EventValue;
	TWeakObjectPtr<const UObject> WorldContextObject;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** Initializes the update output path without a UWorld for automation benchmarks. */
	bool InitializeUpdateForTesting(FWidgetTransition Transition);
	/** Dispatches one update through the same async output handler used at runtime. */
	void DispatchUpdatedForTesting(FWidgetTransitionValue Value);
#endif
};

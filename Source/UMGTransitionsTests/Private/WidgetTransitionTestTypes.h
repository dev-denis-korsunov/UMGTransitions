#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "WidgetTransition.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "WidgetTransitionTestTypes.generated.h"

class UWidgetTransitionSubsystem;
class UTextBlock;

/** Observes cancellation cleanup without requiring a GameInstance. */
UCLASS()
class UWidgetTransitionTestAsyncOwner final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	bool bReleased = false;
	virtual void SetReadyToDestroy() override
	{
		bReleased = true;
		Super::SetReadyToDestroy();
	}
};

UCLASS()
class UWidgetTransitionTestEventReceiver final : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleStarted(FWidgetTransitionValue InValue);
	UFUNCTION()
	void HandleUpdated(FWidgetTransitionValue InValue);
	UFUNCTION()
	void HandleUpdatedAndCapture(FWidgetTransitionValue InValue);
	UFUNCTION()
	void HandleFinished(FWidgetTransitionValue InValue);
	UFUNCTION()
	void HandleAsyncUpdated(FWidgetTransitionValue InValue);

	int32 StartedCount = 0;
	int32 UpdatedCount = 0;
	int32 FinishedCount = 0;
	int32 AsyncValueUpdateCount = 0;
	FWidgetTransitionValue LastTransitionValue;
	TWeakObjectPtr<UTextBlock> CounterText;
	TWeakObjectPtr<UWidgetTransitionSubsystem> SubsystemToClear;
	TWeakObjectPtr<UWidget> WidgetToClear;
	bool bAppendTransitionsOnUpdate = false;
};

UCLASS()
class UWidgetComposerTestUserWidget final : public UUserWidget
{
	GENERATED_BODY()
};

/** Test widget that exposes a numeric transition property to a UMG text binding. */
UCLASS()
class UWidgetTransitionTestCounterUserWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(FieldNotify)
	float CounterValue = 0.0f;

	UFUNCTION()
	FText GetCounterText();
};

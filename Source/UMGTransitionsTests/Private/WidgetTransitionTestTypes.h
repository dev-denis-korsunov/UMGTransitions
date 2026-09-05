#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "WidgetTransition.h"

#include "WidgetTransitionTestTypes.generated.h"

class UWidgetTransitionSubsystem;
class UTextBlock;

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
class UWidgetSelectorTestUserWidget final : public UUserWidget
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

/** Verifies the optional synchronization convention used by Slate-backed widgets. */
UCLASS()
class UWidgetTransitionTestSynchronizedWidget final : public UImage
{
	GENERATED_BODY()

public:
	UPROPERTY()
	float AnimatedValue = 0.0f;

	UFUNCTION()
	void SynchronizeTransitionProperty(const FString& PropertyPath)
	{
		LastSynchronizedProperty = PropertyPath;
		++SynchronizationCount;
	}

	FString LastSynchronizedProperty;
	int32 SynchronizationCount = 0;
};

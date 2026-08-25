#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "CoreMinimal.h"

#include "WidgetTransitionTestTypes.generated.h"

class UWidgetTransitionSubsystem;

UCLASS()
class UWidgetTransitionTestEventReceiver final : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleStarted(UWidget* InWidget);
	UFUNCTION()
	void HandleUpdated(UWidget* InWidget, float NormalizedProgress, float EasedProgress);
	UFUNCTION()
	void HandleFinished(UWidget* InWidget);

	int32 StartedCount = 0;
	int32 UpdatedCount = 0;
	int32 FinishedCount = 0;
	TWeakObjectPtr<UWidget> LastWidget;
	TWeakObjectPtr<UWidgetTransitionSubsystem> SubsystemToClear;
	TWeakObjectPtr<UWidget> WidgetToClear;
	bool bAppendTransitionsOnUpdate = false;
};

UCLASS()
class UWidgetSelectorTestUserWidget final : public UUserWidget
{
	GENERATED_BODY()
};

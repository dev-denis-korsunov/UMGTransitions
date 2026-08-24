#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "CoreMinimal.h"

#include "WidgetTransitionTestTypes.generated.h"

UCLASS()
class UWidgetTransitionTestEventReceiver final : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleStarted(UWidget* InWidget)
	{
		++StartedCount;
		LastWidget = InWidget;
	}

	int32 StartedCount = 0;
	TWeakObjectPtr<UWidget> LastWidget;
};

UCLASS()
class UWidgetSelectorTestUserWidget final : public UUserWidget
{
	GENERATED_BODY()
};

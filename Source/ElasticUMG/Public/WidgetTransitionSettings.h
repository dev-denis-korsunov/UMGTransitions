#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DeveloperSettings.h"

#include "WidgetTransitionSettings.generated.h"

/** A named easing curve edited with Unreal's standard Curve Editor. */
USTRUCT()
struct ELASTICUMG_API FWidgetTransitionEasing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Easing")
	FName Name;

	UPROPERTY(EditAnywhere, Config, Category = "Easing")
	FRuntimeFloatCurve Curve;
};

/** Project-wide easing presets used by widget transitions. */
UCLASS(Config = ElasticUMG, DefaultConfig, meta = (DisplayName = "Elastic UMG"))
class ELASTICUMG_API UWidgetTransitionSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWidgetTransitionSettings();

	UPROPERTY(EditAnywhere, Config, Category = "Preview", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float PreviewDuration = 1.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Easing", meta = (TitleProperty = "Name"))
	TArray<FWidgetTransitionEasing> EasingFunctions;

	const FWidgetTransitionEasing* FindEasing(FName Name) const;
	float EvaluateEasing(FName Name, float Alpha) const;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("ElasticUMG"); }
	virtual FText GetSectionText() const override { return NSLOCTEXT("ElasticUMG", "SettingsSection", "Elastic UMG"); }
};

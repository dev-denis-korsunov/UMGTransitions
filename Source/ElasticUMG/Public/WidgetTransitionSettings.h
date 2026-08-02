#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "WidgetTransitionSettings.generated.h"

/** Value carried by the Easing pin. Split it to edit the two cubic-bezier handles. */
USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetTransitionEasingValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Easing")
	FVector2D ControlPoint1 = FVector2D(0.25f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Easing")
	FVector2D ControlPoint2 = FVector2D(0.25f, 1.0f);
};

/** A CSS-style cubic Bezier: fixed endpoints (0,0) and (1,1), plus two editable handles. */
USTRUCT()
struct ELASTICUMG_API FWidgetTransitionEasing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Easing")
	FName Name;

	UPROPERTY(EditAnywhere, Config, Category = "Easing")
	FVector2D ControlPoint1 = FVector2D(0.25f, 0.1f);

	UPROPERTY(EditAnywhere, Config, Category = "Easing")
	FVector2D ControlPoint2 = FVector2D(0.25f, 1.0f);

	float Evaluate(float Alpha) const;
	static float EvaluateCubicBezier(FVector2D InControlPoint1, FVector2D InControlPoint2, float Alpha);
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

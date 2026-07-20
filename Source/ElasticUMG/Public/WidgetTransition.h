#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"

#include "WidgetTransition.generated.h"

class UWidget;

UENUM()
enum class EWidgetTransitionValueType : uint8
{
	Float,
	Bool,
	Vector2D,
	LinearColor,
};

USTRUCT()
struct ELASTICUMG_API FWidgetTransitionPropertyBinding
{
	GENERATED_BODY()

	TWeakObjectPtr<UWidget> Widget;
	FDynamicPropertyPath CachedPropertyPath;
	bool bResolved = false;
	bool bUsesDouble = false;
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;

	bool Resolve(UWidget* InWidget, const FString& InPropertyPath);
	void Invalidate();
	bool Apply(float Value) const;
	bool Apply(bool Value) const;
	bool Apply(const FVector2D& Value) const;
	bool Apply(const FLinearColor& Value) const;
	bool Read(float& OutValue) const;
	bool Read(bool& OutValue) const;
	bool Read(FVector2D& OutValue) const;
	bool Read(FLinearColor& OutValue) const;
};

/** Common runtime representation used by each strongly typed transition storage. */
template <typename TValue>
struct TWidgetTransition
{
	TWeakObjectPtr<UWidget> Widget;
	FString WidgetProperty;
	FWidgetTransitionPropertyBinding PropertyBinding;
	TValue FromValue{};
	TValue ToValue{};
	float Time = 0.0f;
	float Delay = 0.0f;
	float CurrentTime = 0.0f;
};

/** A separate typed transition container. The subsystem owns one instance per supported value type. */
template <typename TValue>
struct TWidgetTransitionStorage
{
	TSparseArray<TWidgetTransition<TValue>> Transitions;
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void CreateFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float TargetValue, float Time = 0.0f, float Delay = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void CreateBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool TargetValue, float Time = 0.0f, float Delay = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void CreateVectorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D TargetValue, float Time = 0.0f, float Delay = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void CreateColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor TargetValue, float Time = 0.0f, float Delay = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget);
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ETickableTickType GetTickableTickType() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UWidgetTransitionSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }

	TWidgetTransitionStorage<float> FloatTransitions;
	TWidgetTransitionStorage<bool> BoolTransitions;
	TWidgetTransitionStorage<FVector2D> VectorTransitions;
	TWidgetTransitionStorage<FLinearColor> ColorTransitions;
};

#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Misc/TVariant.h"
#include "SpringFloat.h"
#include "Subsystems/WorldSubsystem.h"

#include "WidgetTransition.generated.h"

class UWidget;

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnFloatWidgetTransitionUpdate, UWidget*, Widget, float, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnBoolWidgetTransitionUpdate, UWidget*, Widget, bool, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnVectorWidgetTransitionUpdate, UWidget*, Widget, FVector2D, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnColorWidgetTransitionUpdate, UWidget*, Widget, FLinearColor, Value, float, Alpha);

UENUM()
enum class EWidgetTransitionValueType : uint8
{
	Float,
	Bool,
	Vector2D,
	LinearColor,
};

/** The two endpoints of a transition always use the same alternative. */
using FTransitionValue = TVariant<float, FVector2D, bool, FLinearColor>;

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
	bool Apply(const FTransitionValue& Value) const;
	bool Read(FTransitionValue& OutValue) const;
};

/** Runtime transition with compact tagged From/To values. */
struct FWidgetTransition
{
	TWeakObjectPtr<UWidget> Widget;
	FString WidgetProperty;
	FWidgetTransitionPropertyBinding PropertyBinding;
	FTransitionValue FromValue;
	FTransitionValue ToValue;
	float Time = 0.0f;
	float Delay = 0.0f;
	float CurrentTime = 0.0f;
	int32 RepeatCount = 0;
	int32 CompletedRepeats = 0;
	bool bYoYo = false;
	bool bRemoveFromParent = false;
	bool bUseSpring = false;
	float SpringFactor = 200.0f;
	float DampingFactor = 16.0f;
	float MaxVelocity = 1600.0f;
	float CompleteTolerance = 0.01f;
	TSharedPtr<FSpringFloat> FloatSpring;
	TSharedPtr<FSpringFloat> VectorXSpring;
	TSharedPtr<FSpringFloat> VectorYSpring;
	FOnFloatWidgetTransitionUpdate FloatOnUpdate;
	FOnBoolWidgetTransitionUpdate BoolOnUpdate;
	FOnVectorWidgetTransitionUpdate VectorOnUpdate;
	FOnColorWidgetTransitionUpdate ColorOnUpdate;
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float ToValue, FOnFloatWidgetTransitionUpdate OnUpdate, float Time = 0.0f, float Delay = 0.0f, int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseSpring = false, float SpringFactor = 200.0f, float DampingFactor = 16.0f, float MaxVelocity = 1600.0f, float CompleteTolerance = 0.01f, bool bUseFrom = false, float FromValue = 0.0f);

	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool ToValue, FOnBoolWidgetTransitionUpdate OnUpdate, float Time = 0.0f, float Delay = 0.0f, int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseFrom = false, bool FromValue = false);

	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateVectorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D ToValue, FOnVectorWidgetTransitionUpdate OnUpdate, float Time = 0.0f, float Delay = 0.0f, int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseSpring = false, float SpringFactor = 200.0f, float DampingFactor = 16.0f, float MaxVelocity = 1600.0f, float CompleteTolerance = 0.01f, bool bUseFrom = false, FVector2D FromValue = FVector2D::ZeroVector);

	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor ToValue, FOnColorWidgetTransitionUpdate OnUpdate, float Time = 0.0f, float Delay = 0.0f, int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseFrom = false, FLinearColor FromValue = FLinearColor::White);

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

	TSparseArray<FWidgetTransition> Transitions;
};

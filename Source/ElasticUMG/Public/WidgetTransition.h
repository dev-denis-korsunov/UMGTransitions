#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/ContentWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetTransition.generated.h"

class FSpringFloat;
class FCustomFloatTransitionPropertyImpl;
class UWidgetTransitionSubsystem;
class UWidgetTransitionFunctionLibrary;
class UWidget;

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCustomFloatPropertyUpdate, float, NewValue);

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCustomFloatPropertyComplete, float, CompleteValue);

UENUM(BlueprintType)
enum class EWidgetProperty : uint8
{
	TranslationX,
	TranslationY,
	ScaleX,
	ScaleY,
	BothSquareScale,
	ShearX,
	ShearY,
	Angle,
	Opacity,
	PivotX,
	PivotY,

	Custom UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct ELASTICUMG_API FCustomFloatTransitionProperty
{
	GENERATED_BODY()

	FCustomFloatTransitionProperty()
	{
	}

	explicit FCustomFloatTransitionProperty(const TSharedPtr<FCustomFloatTransitionPropertyImpl>& PropertyImpl);

	TSharedPtr<FCustomFloatTransitionPropertyImpl> GetPropertyImpl() const;

	friend uint32 GetTypeHash(const FCustomFloatTransitionProperty& TransitionProperty)
	{
		return GetTypeHash(TransitionProperty.GetPropertyImpl());
	}

	bool operator==(const FCustomFloatTransitionProperty& Other) const
	{
		return GetPropertyImpl() == Other.GetPropertyImpl();
	}

protected:
	mutable TSharedPtr<FCustomFloatTransitionPropertyImpl> CustomTransitionPropertyImpl = nullptr;
};

template<>
struct TStructOpsTypeTraits<FCustomFloatTransitionProperty>
	: public TStructOpsTypeTraitsBase2<FCustomFloatTransitionProperty>
{
	enum
	{
		WithIdenticalViaEquality = true
	};
};

USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetTransition
{
	GENERATED_BODY()

	FWidgetTransition()
		: bFrom(false)
		, bChangeFromPropertyAfterDelay(false)
		, bPipe(false)
		, bFromVisibility(false)
		, bToVisibility(false)
		, bRemoveFromParent(false)
		, bSpring(false)
	{
	}

protected:
	friend UWidgetTransitionFunctionLibrary;
	friend UWidgetTransitionSubsystem;

	TWeakObjectPtr<UWidget> Widget = nullptr;
	EWidgetProperty WidgetProperty = EWidgetProperty::TranslationX;
	TWeakPtr<FCustomFloatTransitionPropertyImpl> CustomProperty = nullptr;
	TSharedPtr<FSpringFloat> SpringFloat = nullptr;

	float ToValue = 0.0f;
	float FromValue = 0.0f;

	float Delay = 0.0f;
	float Time = 0.0f;

	FRichCurve InterpolationCurve;

	float CurrentValue = 0.0f;
	float CurrentTime = 0.0f;

	ESlateVisibility FromVisibility = ESlateVisibility::SelfHitTestInvisible;
	ESlateVisibility ToVisibility = ESlateVisibility::SelfHitTestInvisible;

	uint8 bFrom : 1;
	uint8 bChangeFromPropertyAfterDelay : 1;
	uint8 bPipe : 1;
	uint8 bFromVisibility : 1;
	uint8 bToVisibility : 1;
	uint8 bRemoveFromParent : 1;
	uint8 bSpring : 1;

public:
	void SetWidgetPropertyValue(const float Value, const bool bLastFrame = false) const;
	float GetWidgetPropertyValue() const;
	float GetRemainingTime() const;
	bool Equal(const FWidgetTransition& Trs) const;
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WidgetTransition", meta = (WorldContext = "WorldContextObject"))
	static void AddWidgetTransition(const UObject* WorldContextObject, UWidget* UserWidget, FWidgetTransition Transition);

	UFUNCTION(BlueprintCallable, Category = "WidgetTransition", meta = (WorldContext = "WorldContextObject"))
	static void AddWidgetTransitionArray(const UObject* WorldContextObject, UWidget* UserWidget, const TArray<FWidgetTransition>& TransitionArray);

	UFUNCTION(BlueprintCallable, Category = "WidgetTransition", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* UserWidget);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "Delay, Interpolation"))
	static FWidgetTransition CreateWidgetTransition(EWidgetProperty WidgetProperty = EWidgetProperty::TranslationX, float TargetValue = 0.0f, float Time = 0.0f, float Delay = 0.0f, UCurveFloat* Interpolation = nullptr);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "Delay, Interpolation"))
	static FWidgetTransition CreateWidgetCustomFloatTransition(const FCustomFloatTransitionProperty& CustomProperty, float TargetValue = 0.0f, float Time = 0.0f, float Delay = 0.0f, UCurveFloat* Interpolation = nullptr);

	UFUNCTION(BlueprintCallable, Category = "WidgetTransition")
	static void SetCustomFloatTransitionPropertyDelegate(const FCustomFloatTransitionProperty& CustomProperty, UPARAM(DisplayName = "Event") FOnCustomFloatPropertyUpdate Delegate);

	UFUNCTION(BlueprintCallable, Category = "WidgetTransition")
	static void SetCustomFloatTransitionPropertyValue(const FCustomFloatTransitionProperty& CustomProperty, const float NewValue);

	UFUNCTION(BlueprintCallable, Category = "WidgetTransition")
	static void SetCustomFloatTransitionPropertyCompleteDelegate(const FCustomFloatTransitionProperty& CustomProperty, UPARAM(DisplayName = "Event") FOnCustomFloatPropertyComplete Delegate);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static float GetCustomFloatTransitionPropertyValue(const FCustomFloatTransitionProperty& CustomProperty);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bFrom, bChangePropertyAfterDelay"))
	static FWidgetTransition From(const FWidgetTransition& Transition, bool bFrom = true, float FromValue = 0.0f, bool bChangePropertyAfterDelay = false);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bPipe"))
	static FWidgetTransition Pipe(const FWidgetTransition& Transition, bool bPipe = true);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bRemoveFromParent"))
	static FWidgetTransition RemoveFromParent(const FWidgetTransition& Transition, bool bRemoveFromParent = true);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static FWidgetTransition Curve(const FWidgetTransition& Transition, const FRuntimeFloatCurve& Curve);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bVisibility"))
	static FWidgetTransition Visibility(const FWidgetTransition& Transition, const ESlateVisibility FromVisibility = ESlateVisibility::HitTestInvisible, const ESlateVisibility ToVisibility = ESlateVisibility::HitTestInvisible, const bool bVisibility = true);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bToVisibility"))
	static FWidgetTransition ToVisibility(const FWidgetTransition& Transition, const ESlateVisibility ToVisibility = ESlateVisibility::HitTestInvisible, const bool bToVisibility = true);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bFromVisibility"))
	static FWidgetTransition FromVisibility(const FWidgetTransition& Transition, const ESlateVisibility FromVisibility = ESlateVisibility::HitTestInvisible, const bool bFromVisibility = true);

	UFUNCTION(BlueprintPure, Category="WidgetTransition", meta = (AdvancedDisplay = "SpringFactor, DampingFactor, MaxVelocity, CompleteTolerance, bElastic"))
	static FWidgetTransition Spring(const FWidgetTransition& Transition, float SpringFactor = 180.0f, const float DampingFactor = 16.0f, const float MaxVelocity = 1800.0f, float CompleteTolerance = 0.01f, bool bElastic = true);

	UFUNCTION(BlueprintPure, meta=(DisplayName="Equal (CustomFloatTransitionProperty)", CompactNodeTitle="==", BlueprintThreadSafe), Category="WidgetTransition")
	static bool EqualEqual_TrsCustomPropTrsCustomProp(const FCustomFloatTransitionProperty& A, const FCustomFloatTransitionProperty& B);
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~FTickableGameObject interface
	virtual ETickableTickType GetTickableTickType() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMHWidgetTransitionSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }
	//~End of FTickableGameObject interface

protected:
	friend UWidgetTransitionFunctionLibrary;
	TSparseArray<FWidgetTransition> WidgetTransitions;
};

class FCustomFloatTransitionPropertyImpl final
{
public:
	void SetValue(const float InValue, const bool bDispatchEvent = true);
	FORCEINLINE float GetValue() const { return Value; }
	void SetChangeValueDelegate(const FOnCustomFloatPropertyUpdate& Delegate);
	void SetCompleteValueDelegate(const FOnCustomFloatPropertyComplete& Delegate);

protected:
	friend FWidgetTransition;

	void DispatchUpdateValue() const;
	void DispatchCompleteValue() const;

	FOnCustomFloatPropertyUpdate OnValueUpdate;
	FOnCustomFloatPropertyComplete OnValueComplete;
	float Value = 0.0f;
};

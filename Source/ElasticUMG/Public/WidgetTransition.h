#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Binding/DynamicPropertyPath.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SpringFloat.h"

#include "WidgetTransition.generated.h"

class UWidgetTransitionSubsystem;
class UWidgetTransitionFunctionLibrary;
class UWidget;

USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetPropertyBindingSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget Transition Binding")
	FString PropertyPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget Transition Binding")
	bool bAllowSlotBinding = true;
};

USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetBindablePropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Transition Binding")
	FName PropertyName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Transition Binding")
	FString PropertyPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Transition Binding")
	bool bIsSlotProperty = false;
};

USTRUCT()
struct ELASTICUMG_API FResolvedWidgetPropertyBinding
{
	GENERATED_BODY()

	TWeakObjectPtr<UWidget> Widget;
	FWidgetPropertyBindingSpec Spec;
	FDynamicPropertyPath CachedPropertyPath;
	bool bResolved = false;

	bool Resolve(UWidget* InWidget, const FWidgetPropertyBindingSpec& InSpec);
	void Invalidate();
	bool ApplyFloat(float Value) const;
	bool ReadFloat(float& OutValue) const;
};

USTRUCT(BlueprintType)
struct ELASTICUMG_API FAnimationUpdateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	UWidget* Widget = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	UWidget* FromWidget = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	UWidget* ToWidget = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	bool bFirstFrame = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	bool bEndFrame = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	float Value = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation Update")
	float RelativeValue = 0.0f;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetTransitionUpdate, FAnimationUpdateResult, UpdateResult);

UENUM(BlueprintType)
enum class EWidgetProperty : uint8
{
	Custom,
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
		, bYoYo(false)
	{
	}

protected:
	friend UWidgetTransitionFunctionLibrary;
	friend UWidgetTransitionSubsystem;

	TWeakObjectPtr<UWidget> Widget = nullptr;
	EWidgetProperty WidgetProperty = EWidgetProperty::TranslationX;
	TSharedPtr<FSpringFloat> SpringFloat = nullptr;
	FWidgetPropertyBindingSpec PropertyBindingSpec;
	FResolvedWidgetPropertyBinding PropertyBinding;

	// Делегат привязывается через BindOnUpdate
	FOnWidgetTransitionUpdate OnUpdate;

	float ToValue = 0.0f;
	float FromValue = 0.0f;

	// Исходные From/To — нужны для YoYo
	float OriginalFromValue = 0.0f;
	float OriginalToValue = 0.0f;

	float Delay = 0.0f;
	float Time = 0.0f;

	FRichCurve InterpolationCurve;

	float CurrentValue = 0.0f;
	float CurrentTime = 0.0f;

	// -1 = бесконечно, 0 = не повторять, N = повторить ещё N раз
	int32 RepeatCount = 0;
	int32 CurrentRepeatCount = 0;

	ESlateVisibility FromVisibility = ESlateVisibility::SelfHitTestInvisible;
	ESlateVisibility ToVisibility = ESlateVisibility::SelfHitTestInvisible;

	uint8 bFrom : 1;
	uint8 bChangeFromPropertyAfterDelay : 1;
	uint8 bPipe : 1;
	uint8 bFromVisibility : 1;
	uint8 bToVisibility : 1;
	uint8 bRemoveFromParent : 1;
	uint8 bSpring : 1;
	uint8 bYoYo : 1;

public:
	float EvaluateTransitionValue() const;
	void SetWidgetPropertyValue(const float Value, const bool bLastFrame = false) const;
	float GetWidgetPropertyValue() const;
	float GetRemainingTime() const;
	bool Equal(const FWidgetTransition& Trs) const;
	bool HasWidgetPropertyAccess() const;

	FORCEINLINE float GetElapsedTime() const { return FMath::Max(CurrentTime - Delay, 0.0f); }
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

	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "SpringFactor, DampingFactor, MaxVelocity, CompleteTolerance, bElastic"))
	static FWidgetTransition Spring(const FWidgetTransition& Transition, float SpringFactor = 180.0f, const float DampingFactor = 16.0f, const float MaxVelocity = 1800.0f, float CompleteTolerance = 0.01f, bool bElastic = true);

	/**
	 * Повторяет анимацию указанное количество раз.
	 * RepeatCount = -1 — бесконечно, RepeatCount = N — ещё N раз после первого проигрывания.
	 */
	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static FWidgetTransition Repeat(const FWidgetTransition& Transition, int32 RepeatCount = -1);

	/**
	 * YoYo — каждый нечётный повтор проигрывается в обратном направлении (пинг-понг).
	 * Работает совместно с Repeat.
	 */
	UFUNCTION(BlueprintPure, Category = "WidgetTransition", meta = (AdvancedDisplay = "bYoYo"))
	static FWidgetTransition YoYo(const FWidgetTransition& Transition, bool bYoYo = true);

	/** Привязывает BlueprintPure функцию к событию обновления значения. */
	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static FWidgetTransition BindOnUpdate(const FWidgetTransition& Transition, FOnWidgetTransitionUpdate OnUpdate);

	/** Тестовый режим: анимирует float-свойство по строковому пути через reflection. */
	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static FWidgetTransition BindPropertyPath(const FWidgetTransition& Transition, const FString& InPropertyPath);

	/**
	 * Привязывает float-свойство виджета. В Blueprint graph параметр пути получает picker
	 * на основании типа подключенного Widget; для прямой designer-переменной также видны Slot-свойства.
	 */
	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static FWidgetTransition BindWidgetProperty(const FWidgetTransition& Transition, UWidget* Widget, const FString& InPropertyPath);

	UFUNCTION(BlueprintPure, Category = "WidgetTransition")
	static TArray<FWidgetBindablePropertyInfo> GetBindableWidgetProperties(UWidget* Widget);
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

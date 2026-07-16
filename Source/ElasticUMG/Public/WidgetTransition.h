// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Curves/CurveFloat.h"
#include "WidgetTransition.generated.h"

class UWidget;
class UUserWidget;

/**
 * Enum for widget properties that can be transitioned
 */
UENUM()
enum class EWidgetProperty : uint8
{
	// Position
	Position,
	
	// Size
	Size,
	
	// Visibility
	Visibility,
	
	// Render Transform
	RenderTransform_Pivot,
	RenderTransform_Rotation,
	RenderTransform_Scale,
	RenderTransform_Translation,

	// Color and Opacity
	ColorAndOpacity,
	ForegroundColor,

	// Margin
	Margin,

	// Padding
	Padding,

	// Font Size
	FontSize,

	// Text
	Text,

	// Brush
	Brush,
	
	// Custom Property
	CustomProperty,
};

/**
 * Structure to hold a widget transition
 */
USTRUCT()
struct ELASTICUMG_API FWidgetTransition
{
	GENERATED_BODY()

public:
	FWidgetTransition() = default;
	FWidgetTransition(const FWidgetTransition& Other) = default;
	FWidgetTransition(FWidgetTransition&& Other) = default;
	FWidgetTransition& operator=(const FWidgetTransition& Other) = default;
	FWidgetTransition& operator=(FWidgetTransition&& Other) = default;

	/** The widget to transition */
	UPROPERTY()
	TWeakObjectPtr<UWidget> Widget;

	/** The property to transition */
	UPROPERTY()
	EWidgetProperty WidgetProperty;

	/** The target value for the transition */
	UPROPERTY()
	float TargetValue;

	/** The duration of the transition */
	UPROPERTY()
	float Duration;

	/** The delay before starting the transition */
	UPROPERTY()
	float Delay;

	/** The curve to use for the transition */
	UPROPERTY()
	FRuntimeFloatCurve Curve;

	/** Whether this is a spring transition */
	UPROPERTY()
	bool bSpring;

	/** Spring factor for spring transitions */
	UPROPERTY()
	float SpringFactor;

	/** Damping factor for spring transitions */
	UPROPERTY()
	float DampingFactor;

	/** Maximum velocity for spring transitions */
	UPROPERTY()
	float MaxVelocity;

	/** Whether to repeat the transition */
	UPROPERTY()
	bool bRepeat;

	/** Number of times to repeat */
	UPROPERTY()
	int32 RepeatCount;

	/** Whether to use YoYo pattern */
	UPROPERTY()
	bool bYoYo;

	/** Whether this is a one-time transition */
	UPROPERTY()
	bool bOneTime;

	/** Whether to remove from parent when complete */
	UPROPERTY()
	bool bRemoveFromParent;

	/** The function to call on update */
	UPROPERTY()
	FOnWidgetTransitionUpdate OnUpdate;

	/** The function to call when complete */
	UPROPERTY()
	FOnWidgetTransitionComplete OnComplete;

	/** Current time of the transition */
	float CurrentTime;

	/** Whether the transition is completed */
	bool bCompleted;

	/** Whether the transition is started */
	bool bStarted;

	/** Start value for the transition */
	float StartValue;

	/** Target value for the transition */
	float TargetValueInternal;

	/** Whether this is a visibility transition */
	bool bIsVisibilityTransition;

	/** Whether to use custom curve */
	bool bUseCustomCurve;

	/** Whether to use spring */
	bool bUseSpring;

	/** Whether to repeat */
	bool bUseRepeat;

	/** Whether to use YoYo */
	bool bUseYoYo;

	/** Whether to remove from parent */
	bool bUseRemoveFromParent;

	/** Whether to call update function */
	bool bUseOnUpdate;

	/** Whether to call complete function */
	bool bUseOnComplete;

public:
	void SetWidgetPropertyValue(const float Value, const bool bLastFrame = false) const;
	float GetWidgetPropertyValue() const;
	float GetRemainingTime() const;
	bool Equal(const FWidgetTransition& Trs) const;
	
	FORCEINLINE float GetElapsedTime() const { return FMath::Max(CurrentTime - Delay, 0.0f); }
	
	void Reset();
	void Start();
	void Tick(float DeltaTime);
	bool IsCompleted() const;
	bool IsStarted() const;
};

/**
 * Function library for widget transitions
 */
UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary : public UObject
{
	GENERATED_BODY()

public:
	/** Add a transition to a widget */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static void AddWidgetTransition(const UObject* WorldContextObject, UWidget* UserWidget, FWidgetTransition Transition);

	/** Add multiple transitions to a widget */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static void AddWidgetTransitionArray(const UObject* WorldContextObject, UWidget* UserWidget, const TArray<FWidgetTransition>& Transitions);

	/** Clear all transitions for a widget */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static void ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* UserWidget);

	/** Create a new transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition CreateWidgetTransition(EWidgetProperty WidgetProperty = EWidgetProperty::Position, float TargetValue = 0.0f, float Duration = 1.0f, float Delay = 0.0f);

	/** Create a transition from another transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition From(const FWidgetTransition& Transition, bool bFrom = true, float FromValue = 0.0f);

	/** Create a transition that pipes to another transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition Pipe(const FWidgetTransition& Transition, bool bPipe = true);

	/** Create a transition that removes from parent */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition RemoveFromParent(const FWidgetTransition& Transition, bool bRemoveFromParent = true);

	/** Create a transition with a curve */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition Curve(const FWidgetTransition& Transition, const FRuntimeFloatCurve& Curve);

	/** Create a visibility transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition Visibility(const FWidgetTransition& Transition, const ESlateVisibility FromVisibility, const ESlateVisibility ToVisibility);

	/** Create a transition to visibility */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition ToVisibility(const FWidgetTransition& Transition, const ESlateVisibility ToVisibility);

	/** Create a transition from visibility */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition FromVisibility(const FWidgetTransition& Transition, const ESlateVisibility FromVisibility);

	/** Create a spring transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition Spring(const FWidgetTransition& Transition, float SpringFactor = 180.0f, float DampingFactor = 20.0f, float MaxVelocity = 5000.0f);

	/** Create a repeat transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition Repeat(const FWidgetTransition& Transition, int32 RepeatCount = -1);

	/** Create a YoYo transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition YoYo(const FWidgetTransition& Transition, bool bYoYo = true);

	/** Bind an update function to a transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition BindOnUpdate(const FWidgetTransition& Transition, FOnWidgetTransitionUpdate OnUpdate);

	/** Bind a complete function to a transition */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition")
	static FWidgetTransition BindOnComplete(const FWidgetTransition& Transition, FOnWidgetTransitionComplete OnComplete);
};

/**
 * Subsystem for managing widget transitions
 */
UCLASS()
class ELASTICUMG_API UWidgetTransitionSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UWidgetTransitionSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }

private:
	/** Array of active transitions */
	TArray<FWidgetTransition> ActiveTransitions;

	/** Array of pending transitions to add */
	TArray<FWidgetTransition> PendingTransitions;
};

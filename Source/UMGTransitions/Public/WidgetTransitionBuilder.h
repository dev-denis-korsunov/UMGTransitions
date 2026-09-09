#pragma once

#include "CoreMinimal.h"
#include "WidgetTransitionCallbacks.h"


class UWidget;
class UWidgetTransitionSubsystem;
class UCurveTable;

/**
 * Native fluent contract for creating and starting one widget transition.
 *
 * The builder owns the transition specification until Add() is called. Code
 * outside the plugin should use this type instead of writing FWidgetTransition
 * fields directly; Blueprint construction remains available through the
 * function library.
 */
class UMGTRANSITIONS_API FWidgetTransitionBuilder final
{
public:
	/** Creates a builder. Context is used to locate the owning world subsystem. */
	static FWidgetTransitionBuilder Make(const UObject* Context);

	FWidgetTransitionBuilder& Target(UWidget* Widget, FName WidgetProperty = NAME_None);
	FWidgetTransitionBuilder& Property(FName WidgetProperty);
	FWidgetTransitionBuilder& From(float Value);
	FWidgetTransitionBuilder& From(FVector2D Value);
	FWidgetTransitionBuilder& From(FLinearColor Value);
	FWidgetTransitionBuilder& From(FWidgetTransitionValue Value);
	FWidgetTransitionBuilder& To(float Value);
	FWidgetTransitionBuilder& To(FVector2D Value);
	FWidgetTransitionBuilder& To(FLinearColor Value);
	FWidgetTransitionBuilder& To(FWidgetTransitionValue Value);
	FWidgetTransitionBuilder& Time(float Seconds);
	FWidgetTransitionBuilder& Delay(float Seconds);
	FWidgetTransitionBuilder& SpringForce(float Value);
	FWidgetTransitionBuilder& SpringDamping(float Value);
	FWidgetTransitionBuilder& SpringMaxSpeed(float Value);
	FWidgetTransitionBuilder& FitSimulationToTime(bool bEnabled = true);
	FWidgetTransitionBuilder& IgnoreDelay(bool bEnabled = true);
	FWidgetTransitionBuilder& RepeatDelay(bool bEnabled = true);
	FWidgetTransitionBuilder& Repeat(int32 Count);
	FWidgetTransitionBuilder& YoYo(bool bEnabled = true);
	FWidgetTransitionBuilder& EventInterval(float Seconds);
	FWidgetTransitionBuilder& ColorMix(EWidgetTransitionColorMix Value);
	FWidgetTransitionBuilder& Easing(UCurveTable* CurveTable, FName RowName);

	FWidgetTransitionBuilder& BindStart(FOnWidgetTransitionUpdate Callback);
	FWidgetTransitionBuilder& BindUpdate(FOnWidgetTransitionUpdate Callback);
	FWidgetTransitionBuilder& BindFinish(FOnWidgetTransitionUpdate Callback);

	/** Adds the built transition to the context's world subsystem. Returns false on invalid input. */
	bool Add();
	/** Returns the configured transition without adding it to the subsystem. */
	FWidgetTransition GetTransition() const;

private:
	FWidgetTransitionBuilder(const UObject* InContext, UWidget* InWidget, FName InWidgetProperty);

	const UObject* Context = nullptr;
	FWidgetTransition Transition;
	FWidgetTransitionCallbacks Callbacks;
};

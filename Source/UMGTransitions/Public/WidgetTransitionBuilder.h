#pragma once

#include "CoreMinimal.h"
#include "WidgetTransitionCallbacks.h"


class UWidget;
class UWorld;
class UWidgetTransitionSubsystem;

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
	/** Creates a builder for one world. */
	static FWidgetTransitionBuilder Make(UWorld* WorldContext);

	FWidgetTransitionBuilder& Target(UWidget* Widget, FName WidgetProperty = NAME_None);
	FWidgetTransitionBuilder& Property(FName WidgetProperty);
	/** Sets the starting value. SetImmediate applies it before Delay; false applies it when Delay ends. */
	FWidgetTransitionBuilder& From(float Value, bool bSetImmediate = true);
	FWidgetTransitionBuilder& From(FVector2D Value, bool bSetImmediate = true);
	FWidgetTransitionBuilder& From(FLinearColor Value, bool bSetImmediate = true);
	FWidgetTransitionBuilder& From(FWidgetTransitionValue Value, bool bSetImmediate = true);
	FWidgetTransitionBuilder& To(float Value);
	FWidgetTransitionBuilder& To(FVector2D Value);
	FWidgetTransitionBuilder& To(FLinearColor Value);
	FWidgetTransitionBuilder& To(FWidgetTransitionValue Value);
	FWidgetTransitionBuilder& Time(float Seconds);
	FWidgetTransitionBuilder& Delay(float Seconds);
	FWidgetTransitionBuilder& SpringForce(float Value);
	FWidgetTransitionBuilder& SpringDamping(float Value);
	FWidgetTransitionBuilder& SpringMaxSpeed(float Value);
	FWidgetTransitionBuilder& FitToTime(bool bEnabled = true);
	FWidgetTransitionBuilder& RepeatDelay(bool bEnabled = true);
	FWidgetTransitionBuilder& Repeat(int32 Count);
	FWidgetTransitionBuilder& YoYo(bool bEnabled = true);
	FWidgetTransitionBuilder& EventInterval(float Seconds);
	FWidgetTransitionBuilder& ColorMix(EWidgetTransitionColorMix Value);
	FWidgetTransitionBuilder& Easing(FWidgetTransitionEasing Easing);

	FWidgetTransitionBuilder& BindStart(FOnWidgetTransitionUpdate Callback);
	FWidgetTransitionBuilder& BindUpdate(FOnWidgetTransitionUpdate Callback);
	FWidgetTransitionBuilder& BindFinish(FOnWidgetTransitionUpdate Callback);

	/** Adds the built transition to the world's subsystem. Returns false on invalid input. */
	bool Add();
	/** Returns the configured transition without adding it to the subsystem. */
	FWidgetTransition GetTransition() const;

private:
	FWidgetTransitionBuilder(UWorld* InWorldContext, UWidget* InWidget, FName InWidgetProperty);

	TWeakObjectPtr<UWorld> WorldContext;
	FWidgetTransition Transition;
	FWidgetTransitionCallbacks Callbacks;
};

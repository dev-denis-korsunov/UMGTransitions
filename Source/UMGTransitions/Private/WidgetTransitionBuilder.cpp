#include "WidgetTransitionBuilder.h"

#include "WidgetTransitionSubsystem.h"

FWidgetTransitionBuilder::FWidgetTransitionBuilder(UWorld* InWorldContext, UWidget* InWidget, FName InWidgetProperty)
	: WorldContext(InWorldContext)
{
	Transition.Widget = InWidget;
	Transition.WidgetProperty = InWidgetProperty;
}

FWidgetTransitionBuilder FWidgetTransitionBuilder::Make(UWorld* WorldContext)
{
	return FWidgetTransitionBuilder(WorldContext, nullptr, NAME_None);
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::Target(UWidget* Widget, FName WidgetProperty)
{
	Transition.Widget = Widget;
	Transition.WidgetProperty = WidgetProperty;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::Property(FName WidgetProperty)
{
	Transition.WidgetProperty = WidgetProperty;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::From(float Value, bool bSetImmediate)
{
	Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(Value);
	Transition.bSetFrom = true;
	Transition.bSetImmediate = bSetImmediate;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::From(FVector2D Value, bool bSetImmediate)
{
	Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(Value);
	Transition.bSetFrom = true;
	Transition.bSetImmediate = bSetImmediate;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::From(FLinearColor Value, bool bSetImmediate)
{
	Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(Value);
	Transition.bSetFrom = true;
	Transition.bSetImmediate = bSetImmediate;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::From(FWidgetTransitionValue Value, bool bSetImmediate)
{
	Transition.FromValue = MoveTemp(Value);
	Transition.bSetFrom = true;
	Transition.bSetImmediate = bSetImmediate;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::To(float Value)
{
	Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(Value);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::To(FVector2D Value)
{
	Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(Value);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::To(FLinearColor Value)
{
	Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(Value);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::To(FWidgetTransitionValue Value)
{
	Transition.ToValue = MoveTemp(Value);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::Time(float Seconds)
{
	Transition.Time = FMath::Max(0.0f, Seconds);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::Delay(float Seconds)
{
	Transition.Delay = FMath::Max(0.0f, Seconds);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::SpringForce(float Value)
{
	Transition.bUseSpring = true;
	Transition.SpringForce = FMath::Max(1.0f, Value);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::SpringDamping(float Value)
{
	Transition.bUseSpring = true;
	Transition.SpringDamping = FMath::Clamp(Value, 0.0f, 1.0f);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::SpringMaxSpeed(float Value)
{
	Transition.bUseSpring = true;
	Transition.SpringMaxSpeed = FMath::Max(0.0f, Value);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::FitToTime(bool bEnabled)
{
	Transition.bUseSpring = true;
	Transition.bFitToTime = bEnabled;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::RepeatDelay(bool bEnabled)
{
	Transition.bRepeatDelay = bEnabled;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::Repeat(int32 Count)
{
	Transition.RepeatCount = FMath::Max(-1, Count);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::YoYo(bool bEnabled)
{
	Transition.bYoYo = bEnabled;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::EventInterval(float Seconds)
{
	Transition.EventInterval = FMath::Max(0.0f, Seconds);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::ColorMix(EWidgetTransitionColorMix Value)
{
	Transition.ColorMix = Value;
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::Easing(FWidgetTransitionEasing Easing)
{
	Easing.Clamp();
	Transition.Easing = MoveTemp(Easing);
	Transition.bUseEasing = true;
	Transition.bUseSpring = false;
	return *this;
}

FWidgetTransition FWidgetTransitionBuilder::GetTransition() const
{
	return Transition;
}

bool FWidgetTransitionBuilder::Add()
{
	UWorld* World = WorldContext.Get();
	if (!IsValid(Transition.Widget.Get()) || !IsValid(World))
	{
		return false;
	}
	UWidgetTransitionSubsystem* Subsystem = World->GetSubsystem<UWidgetTransitionSubsystem>();
	if (Subsystem == nullptr)
	{
		return false;
	}
	return Subsystem->StartTransition(MoveTemp(Transition), MoveTemp(Callbacks));
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::BindStart(FOnWidgetTransitionUpdate Callback)
{
	Callbacks.OnStarted = MoveTemp(Callback);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::BindUpdate(FOnWidgetTransitionUpdate Callback)
{
	Callbacks.OnUpdated = MoveTemp(Callback);
	return *this;
}

FWidgetTransitionBuilder& FWidgetTransitionBuilder::BindFinish(FOnWidgetTransitionUpdate Callback)
{
	Callbacks.OnFinished = MoveTemp(Callback);
	return *this;
}

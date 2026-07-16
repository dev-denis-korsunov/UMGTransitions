#include "WidgetTransition.h"

#include "SpringFloat.h"
#include "Engine/Engine.h"

#include "Blueprint/UserWidget.h"

void FWidgetTransition::SetWidgetPropertyValue(const float Value, const bool bLastFrame) const
{
	if (!Widget.IsValid())
	{
		return;
	}

	if (bLastFrame && bToVisibility)
	{
		Widget->SetVisibility(ToVisibility);
	}

	if (bLastFrame && bRemoveFromParent)
	{
		Widget->RemoveFromParent();
	}

	FWidgetTransform widgetTransform = Widget->GetRenderTransform();
	FVector2D widgetPivot = Widget->GetRenderTransformPivot();

	switch (WidgetProperty)
	{
	case EWidgetProperty::TranslationX:
	{
		widgetTransform.Translation.X = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::TranslationY:
	{
		widgetTransform.Translation.Y = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::ScaleX:
	{
		widgetTransform.Scale.X = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::ScaleY:
	{
		widgetTransform.Scale.Y = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::BothSquareScale:
	{
		widgetTransform.Scale.X = Value;
		widgetTransform.Scale.Y = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::ShearX:
	{
		widgetTransform.Shear.X = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::ShearY:
	{
		widgetTransform.Shear.Y = Value;
		Widget->SetRenderTransform(widgetTransform);
		break;
	}
	case EWidgetProperty::Angle:
	{
		Widget->SetRenderTransformAngle(Value);
		break;
	}
	case EWidgetProperty::Opacity:
	{
		Widget->SetRenderOpacity(Value);
		break;
	}
	case EWidgetProperty::PivotX:
	{
		widgetPivot.X = Value;
		Widget->SetRenderTransformPivot(widgetPivot);
		break;
	}
	case EWidgetProperty::PivotY:
	{
		widgetPivot.Y = Value;
		Widget->SetRenderTransformPivot(widgetPivot);
		break;
	}
	default: break;
	}

	if (OnUpdate.IsBound())
	{
		OnUpdate.Execute(Value, GetElapsedTime());
	}

	if (bLastFrame && OnComplete.IsBound())
	{
		OnComplete.Execute(Value);
	}
}

void FWidgetTransition::DispatchStartEvent() const
{
	if (OnStart.IsBound())
	{
		OnStart.Execute(CurrentValue);
	}
}

float FWidgetTransition::GetWidgetPropertyValue() const
{
	float PropertyValue = 0.0f;

	if (!Widget.IsValid())
	{
		return PropertyValue;
	}

	const FWidgetTransform widgetTransform = Widget->GetRenderTransform();
	const FVector2D widgetPivot = Widget->GetRenderTransformPivot();

	PropertyValue = WidgetProperty == EWidgetProperty::TranslationX ? widgetTransform.Translation.X : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::TranslationY ? widgetTransform.Translation.Y : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::ScaleX ? widgetTransform.Scale.X : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::ScaleY ? widgetTransform.Scale.Y : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::BothSquareScale ? widgetTransform.Scale.X : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::ShearX ? widgetTransform.Shear.X : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::ShearY ? widgetTransform.Shear.Y : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::Angle ? widgetTransform.Angle : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::Opacity ? Widget->GetRenderOpacity() : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::PivotX ? widgetPivot.X : PropertyValue;
	PropertyValue = WidgetProperty == EWidgetProperty::PivotY ? widgetPivot.Y : PropertyValue;

	return PropertyValue;
}

float FWidgetTransition::GetRemainingTime() const
{
	const float RemainingTime = Delay + Time - CurrentTime;
	check(RemainingTime >= 0);

	return RemainingTime;
}

bool FWidgetTransition::Equal(const FWidgetTransition& Trs) const
{
	return Trs.Widget.Get() == Widget.Get()
		&& Trs.WidgetProperty == WidgetProperty;
}

void UWidgetTransitionFunctionLibrary::AddWidgetTransition(const UObject* WorldContextObject, UWidget* UserWidget, FWidgetTransition Transition)
{
	if (!IsValid(UserWidget))
	{
		return;
	}

	const UWorld* world = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!IsValid(world))
	{
		return;
	}

	auto* widgetTrsSubsystem = world->GetSubsystem<UWidgetTransitionSubsystem>();
	if (!IsValid(widgetTrsSubsystem))
	{
		return;
	}

	Transition.Widget = UserWidget;

	// "custom from" value behavior
	if (Transition.bFrom)
	{
		const bool bSetPropertyAfterDelay = Transition.bChangeFromPropertyAfterDelay && Transition.Delay > 0.0f;
		if (!bSetPropertyAfterDelay && !Transition.bPipe)
		{
			Transition.SetWidgetPropertyValue(Transition.FromValue);
		}
	}
	else
	{
		Transition.FromValue = Transition.GetWidgetPropertyValue();
	}

	if (Transition.bPipe)
	{
		TArray<FWidgetTransition> foundTransitions;
		float accumulatedDelay = 0.0f;

		for (auto it = widgetTrsSubsystem->WidgetTransitions.CreateIterator(); it; ++it)
		{
			if (Transition.Equal(*it))
			{
				accumulatedDelay += it->GetRemainingTime();
				foundTransitions.Add(*it);
			}
		}

		foundTransitions.Sort([](const FWidgetTransition& A, const FWidgetTransition& B) {
			return A.GetRemainingTime() < B.GetRemainingTime();
		});

		if (foundTransitions.Num() > 0)
		{
			Transition.FromValue = foundTransitions[0].ToValue;
		}

		Transition.Delay += accumulatedDelay;
	}
	else
	{
		for (auto it = widgetTrsSubsystem->WidgetTransitions.CreateIterator(); it; ++it)
		{
			if (Transition.Equal(*it))
			{
				it.RemoveCurrent();
			}
		}
	}

	if (Transition.Widget.IsValid() && Transition.bFromVisibility)
	{
		Transition.Widget->SetVisibility(Transition.FromVisibility);
	}

	if (Transition.bSpring)
	{
		Transition.SpringFloat->Start(Transition.FromValue, Transition.ToValue);
	}

	Transition.OriginalFromValue = Transition.FromValue;
	Transition.OriginalToValue = Transition.ToValue;

	widgetTrsSubsystem->WidgetTransitions.Emplace(Transition);
}

void UWidgetTransitionFunctionLibrary::AddWidgetTransitionArray(const UObject* WorldContextObject, UWidget* UserWidget, const TArray<FWidgetTransition>& TransitionArray)
{
	for (const auto& Transition : TransitionArray)
	{
		AddWidgetTransition(WorldContextObject, UserWidget, Transition);
	}
}

void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* UserWidget)
{
	if (!IsValid(UserWidget))
	{
		return;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!IsValid(World))
	{
		return;
	}

	auto* WidgetTrsSubsystem = World->GetSubsystem<UWidgetTransitionSubsystem>();
	if (!IsValid(WidgetTrsSubsystem))
	{
		return;
	}

	for (auto It = WidgetTrsSubsystem->WidgetTransitions.CreateIterator(); It; ++It)
	{
		if (It->Widget == UserWidget)
		{
			It.RemoveCurrent();
		}
	}
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateWidgetTransition(EWidgetProperty WidgetProperty, float TargetValue, float Time, float Delay, UCurveFloat* Interpolation)
{
	FWidgetTransition newTransition;

	newTransition.WidgetProperty = WidgetProperty;
	newTransition.ToValue = TargetValue;
	newTransition.Time = Time;
	newTransition.Delay = Delay;
	newTransition.InterpolationCurve = IsValid(Interpolation) ? Interpolation->FloatCurve : FRichCurve();

	return newTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::From(const FWidgetTransition& Transition, bool bFrom, float FromValue, bool bChangePropertyAfterDelay)
{
	auto newTransition = Transition;
	newTransition.bFrom = bFrom;
	newTransition.FromValue = FromValue;
	newTransition.bChangeFromPropertyAfterDelay = bChangePropertyAfterDelay;

	return newTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Pipe(const FWidgetTransition& Transition, bool bPipe)
{
	auto newTransition = Transition;
	newTransition.bPipe = bPipe;

	return newTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::RemoveFromParent(const FWidgetTransition& Transition, bool bRemoveFromParent)
{
	auto newTransition = Transition;
	newTransition.bRemoveFromParent = bRemoveFromParent;

	return newTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Curve(const FWidgetTransition& Transition, const FRuntimeFloatCurve& Curve)
{
	auto newTransition = Transition;
	if (auto* richCurve = Curve.GetRichCurveConst())
	{
		newTransition.InterpolationCurve = *richCurve;
	}

	return newTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Visibility(const FWidgetTransition& Transition, const ESlateVisibility FromVisibility, const ESlateVisibility ToVisibility, const bool bVisibility)
{
	auto NewTransition = Transition;

	NewTransition.bToVisibility = bVisibility;
	NewTransition.bFromVisibility = bVisibility;

	NewTransition.FromVisibility = FromVisibility;
	NewTransition.ToVisibility = ToVisibility;

	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::ToVisibility(const FWidgetTransition& Transition, const ESlateVisibility ToVisibility, const bool bToVisibility)
{
	auto NewTransition = Transition;
	NewTransition.bToVisibility = bToVisibility;
	NewTransition.ToVisibility = ToVisibility;

	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::FromVisibility(const FWidgetTransition& Transition, const ESlateVisibility FromVisibility, const bool bFromVisibility)
{
	auto NewTransition = Transition;
	NewTransition.bFromVisibility = bFromVisibility;
	NewTransition.FromVisibility = FromVisibility;

	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Spring(const FWidgetTransition& Transition, float SpringFactor, const float DampingFactor, const float MaxVelocity, const float CompleteTolerance, bool bElastic)
{
	auto NewTransition = Transition;
	NewTransition.bSpring = true;
	NewTransition.SpringFloat = MakeShareable(new FSpringFloat(SpringFactor, DampingFactor, MaxVelocity, CompleteTolerance));

	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Repeat(const FWidgetTransition& Transition, const int32 RepeatCount)
{
	auto NewTransition = Transition;
	NewTransition.RepeatCount = RepeatCount;
	NewTransition.CurrentRepeatCount = 0;
	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::YoYo(const FWidgetTransition& Transition, const bool bYoYo)
{
	auto NewTransition = Transition;
	NewTransition.bYoYo = bYoYo;
	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::BindOnUpdate(const FWidgetTransition& Transition, FOnWidgetTransitionUpdate OnUpdate)
{
	auto NewTransition = Transition;
	NewTransition.OnUpdate = OnUpdate;
	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::BindOnComplete(const FWidgetTransition& Transition, FOnWidgetTransitionComplete OnComplete)
{
	auto NewTransition = Transition;
	NewTransition.OnComplete = OnComplete;
	return NewTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::BindOnStart(const FWidgetTransition& Transition, FOnWidgetTransitionStart OnStart)
{
	auto NewTransition = Transition;
	NewTransition.OnStart = OnStart;
	return NewTransition;
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const
{
	// The CDO of this should never tick
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (auto It = WidgetTransitions.CreateIterator(); It; ++It)
	{
		if (!It->Widget.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		/**
		 * To maintain the smoothness of the animation, we adhere to a delta time value that does not exceed 1/20.
		 * This corresponds to a framerate of 20 fps.
		 */
		It->CurrentTime += FMath::Min(DeltaTime, 1.0f / 20.0f);

		if (It->CurrentTime < It->Delay)
		{
			continue;
		}

		if (!It->bStartDispatched)
		{
			It->bStartDispatched = true;
			It->DispatchStartEvent();
		}

		/**
		 * This code handles the transition of a widget's property value either through a spring effect or linear/curve-based interpolation.
		 *
		 * If the spring effect (`bSpring`) is enabled and the spring object is valid, the code will update the value using spring physics,
		 * ignoring any interpolation curve settings. In this case, the value is updated based on spring dynamics, and interpolation
		 * (both linear and curve-based) does not take effect.
		 *
		 * When the spring effect is disabled, the code calculates the progress (`alpha`) of the transition and applies either linear
		 * interpolation or an interpolation curve (if provided) to smoothly transition the value from the start to the end.
		 */
		bool bTransitionEnd;
		if (It->bSpring && It->SpringFloat.IsValid())
		{
			It->SpringFloat->Tick(DeltaTime);
			It->CurrentValue = It->SpringFloat->GetValue();

			bTransitionEnd = It->SpringFloat->IsCompleted();
		}
		else
		{
			const float Alpha = FMath::Clamp((It->CurrentTime - It->Delay) / It->Time, 0.0f, 1.0f);
			bTransitionEnd = Alpha == 1.0f;

			if (It->InterpolationCurve.IsEmpty())
			{
				It->CurrentValue = FMath::Lerp(It->FromValue, It->ToValue, Alpha);
			}
			else
			{
				It->CurrentValue = FMath::Lerp(It->FromValue, It->ToValue, It->InterpolationCurve.Eval(Alpha));
			}
		}

		It->SetWidgetPropertyValue(It->CurrentValue, bTransitionEnd);

		if (bTransitionEnd)
		{
			const bool bCanRepeat = It->RepeatCount == -1 || It->CurrentRepeatCount < It->RepeatCount;
			if (bCanRepeat)
			{
				It->CurrentRepeatCount++;

				if (It->bYoYo)
				{
					Swap(It->FromValue, It->ToValue);
					if (It->bSpring && It->SpringFloat.IsValid())
					{
						It->SpringFloat->Start(It->FromValue, It->ToValue);
					}
				}

				It->CurrentTime = It->Delay;
			}
			else
			{
				It.RemoveCurrent();
			}
		}
	}
}

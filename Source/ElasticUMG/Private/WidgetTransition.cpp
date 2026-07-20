#include "WidgetTransition.h"

#include "SpringFloat.h"
#include "Engine/Engine.h"
#include "UObject/UnrealType.h"
#include "PropertyPathHelpers.h"

#include "Blueprint/UserWidget.h"

bool FWidgetTransitionPropertyBinding::Resolve(UWidget* InWidget, const FString& InPropertyPath)
{
	Widget = InWidget;
	CachedPropertyPath = FDynamicPropertyPath(InPropertyPath);
	bResolved = Widget.IsValid()
		&& CachedPropertyPath.IsValid();
	bUsesDouble = false;

	if (bResolved && CachedPropertyPath.Resolve(Widget.Get()))
	{
		const FProperty* LeafProperty = CastField<FProperty>(CachedPropertyPath.GetLastSegment().GetField().ToField());
		const FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty);
		bUsesDouble = LeafProperty && LeafProperty->IsA<FDoubleProperty>();
		if (LeafProperty && (LeafProperty->IsA<FFloatProperty>() || bUsesDouble))
		{
			ValueType = EWidgetTransitionValueType::Float;
		}
		else if (StructProperty && StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			ValueType = EWidgetTransitionValueType::Vector2D;
		}
		else if (StructProperty && StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			ValueType = EWidgetTransitionValueType::LinearColor;
		}
		else if (LeafProperty && LeafProperty->IsA<FBoolProperty>())
		{
			ValueType = EWidgetTransitionValueType::Bool;
		}
		else
		{
			bResolved = false;
		}
	}
	else
	{
		bResolved = false;
	}
	return bResolved;
}

void FWidgetTransitionPropertyBinding::Invalidate()
{
	Widget.Reset();
	CachedPropertyPath = FDynamicPropertyPath();
	bResolved = false;
	bUsesDouble = false;
	ValueType = EWidgetTransitionValueType::Float;
}

bool FWidgetTransitionPropertyBinding::ApplyFloat(float Value) const
{
	if (!bResolved || !Widget.IsValid() || !CachedPropertyPath.IsValid())
	{
		return false;
	}

	return bUsesDouble
		? PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, static_cast<double>(Value))
		: PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value);
}

bool FWidgetTransitionPropertyBinding::ReadFloat(float& OutValue) const
{
	if (!bResolved || !Widget.IsValid() || !CachedPropertyPath.IsValid())
	{
		return false;
	}

	if (bUsesDouble)
	{
		double DoubleValue = 0.0;
		if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, DoubleValue))
		{
			return false;
		}

		OutValue = static_cast<float>(DoubleValue);
		return true;
	}

	return PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue);
}

bool FWidgetTransitionPropertyBinding::ApplyValue(const FWidgetTransitionValue& Value) const
{
	if (!bResolved || !Widget.IsValid() || Value.Type != ValueType)
	{
		return false;
	}

	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float: return ApplyFloat(Value.FloatValue);
	case EWidgetTransitionValueType::Vector2D: return PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.Vector2DValue);
	case EWidgetTransitionValueType::LinearColor: return PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.LinearColorValue);
	case EWidgetTransitionValueType::Bool: return PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.BoolValue);
	default: return false;
	}
}

bool FWidgetTransitionPropertyBinding::ReadValue(FWidgetTransitionValue& OutValue) const
{
	if (!bResolved || !Widget.IsValid())
	{
		return false;
	}

	OutValue.Type = ValueType;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float: return ReadFloat(OutValue.FloatValue);
	case EWidgetTransitionValueType::Vector2D: return PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue.Vector2DValue);
	case EWidgetTransitionValueType::LinearColor: return PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue.LinearColorValue);
	case EWidgetTransitionValueType::Bool: return PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue.BoolValue);
	default: return false;
	}
}

float FWidgetTransition::EvaluateTransitionValue() const
{
	if (bSpring && SpringFloat.IsValid())
	{
		return SpringFloat->GetValue();
	}

	if (Time <= 0.0f)
	{
		return ToValue;
	}

	const float Alpha = FMath::Clamp((CurrentTime - Delay) / Time, 0.0f, 1.0f);
	if (InterpolationCurve.IsEmpty())
	{
		return FMath::Lerp(FromValue, ToValue, Alpha);
	}

	return FMath::Lerp(FromValue, ToValue, InterpolationCurve.Eval(Alpha));
}

void FWidgetTransition::SetWidgetPropertyValue(const float Value, const bool bLastFrame) const
{
	if (!Widget.IsValid())
	{
		return;
	}

	const bool bCanModifyWidget = PropertyBinding.bResolved;
	if (bCanModifyWidget && bLastFrame && bToVisibility)
	{
		Widget->SetVisibility(ToVisibility);
	}

	if (bCanModifyWidget && bLastFrame && bRemoveFromParent)
	{
		Widget->RemoveFromParent();
	}

	if (PropertyBinding.bResolved)
	{
		PropertyBinding.ApplyFloat(Value);
	}

	if (OnUpdate.IsBound())
	{
		FAnimationUpdateResult UpdateResult;
		UpdateResult.Widget = Widget.Get();
		UpdateResult.Value = Value;
		UpdateResult.RelativeValue = GetElapsedTime();
		OnUpdate.Execute(UpdateResult);
	}

	// No OnComplete delegate execution since we're removing it
}

float FWidgetTransition::GetWidgetPropertyValue() const
{
	float PropertyValue = 0.0f;

	if (!Widget.IsValid())
	{
		return PropertyValue;
	}

	if (PropertyBinding.bResolved)
	{
		PropertyBinding.ReadFloat(PropertyValue);
		return PropertyValue;
	}

	return PropertyValue;
}

FWidgetTransitionValue FWidgetTransition::EvaluateTypedTransitionValue(bool bTransitionEnd) const
{
	FWidgetTransitionValue Result = TypedToValue;
	if (TypedToValue.Type == EWidgetTransitionValueType::Bool)
	{
		Result.BoolValue = bTransitionEnd ? TypedToValue.BoolValue : TypedFromValue.BoolValue;
		return Result;
	}

	const float Alpha = Time <= 0.0f ? 1.0f : FMath::Clamp((CurrentTime - Delay) / Time, 0.0f, 1.0f);
	const float InterpolatedAlpha = InterpolationCurve.IsEmpty() ? Alpha : InterpolationCurve.Eval(Alpha);
	switch (TypedToValue.Type)
	{
	case EWidgetTransitionValueType::Float:
		Result.FloatValue = FMath::Lerp(TypedFromValue.FloatValue, TypedToValue.FloatValue, InterpolatedAlpha);
		break;
	case EWidgetTransitionValueType::Vector2D:
		Result.Vector2DValue = FMath::Lerp(TypedFromValue.Vector2DValue, TypedToValue.Vector2DValue, InterpolatedAlpha);
		break;
	case EWidgetTransitionValueType::LinearColor:
		Result.LinearColorValue = FMath::Lerp(TypedFromValue.LinearColorValue, TypedToValue.LinearColorValue, InterpolatedAlpha);
		break;
	default:
		break;
	}
	return Result;
}

void FWidgetTransition::SetWidgetPropertyValue(const FWidgetTransitionValue& Value, bool bLastFrame) const
{
	if (!Widget.IsValid())
	{
		return;
	}

	if (PropertyBinding.bResolved && bLastFrame && bToVisibility)
	{
		Widget->SetVisibility(ToVisibility);
	}
	if (PropertyBinding.bResolved && bLastFrame && bRemoveFromParent)
	{
		Widget->RemoveFromParent();
	}
	if (PropertyBinding.bResolved)
	{
		PropertyBinding.ApplyValue(Value);
	}
}

bool FWidgetTransition::GetWidgetPropertyValue(FWidgetTransitionValue& OutValue) const
{
	return PropertyBinding.ReadValue(OutValue);
}

bool FWidgetTransition::HasWidgetPropertyAccess() const
{
	return PropertyBinding.bResolved;
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

void UWidgetTransitionFunctionLibrary::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	UWidget* UserWidget = Transition.Widget.Get();
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
	Transition.PropertyBinding.Invalidate();
	if (!Transition.WidgetProperty.IsEmpty())
	{
		Transition.PropertyBinding.Resolve(UserWidget, Transition.WidgetProperty);
	}

	// An empty WidgetProperty keeps the transition in custom delegate mode.
	if (Transition.HasWidgetPropertyAccess())
	{
		if (Transition.bUsesTypedValue)
		{
			Transition.GetWidgetPropertyValue(Transition.TypedFromValue);
		}
		// "custom from" value behavior
		else if (Transition.bFrom)
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

	if (Transition.HasWidgetPropertyAccess() && Transition.Widget.IsValid() && Transition.bFromVisibility)
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

void UWidgetTransitionFunctionLibrary::AddWidgetTransitionArray(const UObject* WorldContextObject, const TArray<FWidgetTransition>& TransitionArray)
{
	for (const auto& Transition : TransitionArray)
	{
		AddWidgetTransition(WorldContextObject, Transition);
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

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateWidgetTransition(float TargetValue, float Time, float Delay, UCurveFloat* Interpolation)
{
	FWidgetTransition newTransition;

	newTransition.ToValue = TargetValue;
	newTransition.Time = Time;
	newTransition.Delay = Delay;
	newTransition.InterpolationCurve = IsValid(Interpolation) ? Interpolation->FloatCurve : FRichCurve();

	return newTransition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::BindWidgetProperty(const FWidgetTransition& Transition, UWidget* Widget, const FString& WidgetProperty)
{
	FWidgetTransition NewTransition = Transition;
	NewTransition.Widget = Widget;
	NewTransition.WidgetProperty = WidgetProperty;
	NewTransition.PropertyBinding.Invalidate();
	return NewTransition;
}

void UWidgetTransitionFunctionLibrary::StartTypedWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, const FWidgetTransitionValue& TargetValue, float Time, float Delay)
{
	FWidgetTransition Transition = BindWidgetProperty(CreateWidgetTransition(0.0f, Time, Delay), Widget, WidgetProperty);
	Transition.bUsesTypedValue = true;
	Transition.TypedToValue = TargetValue;
	AddWidgetTransition(WorldContextObject, Transition);
}

void UWidgetTransitionFunctionLibrary::StartFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float TargetValue, float Time, float Delay)
{
	FWidgetTransitionValue Value;
	Value.Type = EWidgetTransitionValueType::Float;
	Value.FloatValue = TargetValue;
	StartTypedWidgetTransition(WorldContextObject, Widget, WidgetProperty, Value, Time, Delay);
}

void UWidgetTransitionFunctionLibrary::StartVector2DWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D TargetValue, float Time, float Delay)
{
	FWidgetTransitionValue Value;
	Value.Type = EWidgetTransitionValueType::Vector2D;
	Value.Vector2DValue = TargetValue;
	StartTypedWidgetTransition(WorldContextObject, Widget, WidgetProperty, Value, Time, Delay);
}

void UWidgetTransitionFunctionLibrary::StartLinearColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor TargetValue, float Time, float Delay)
{
	FWidgetTransitionValue Value;
	Value.Type = EWidgetTransitionValueType::LinearColor;
	Value.LinearColorValue = TargetValue;
	StartTypedWidgetTransition(WorldContextObject, Widget, WidgetProperty, Value, Time, Delay);
}

void UWidgetTransitionFunctionLibrary::StartBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool TargetValue, float Time, float Delay)
{
	FWidgetTransitionValue Value;
	Value.Type = EWidgetTransitionValueType::Bool;
	Value.BoolValue = TargetValue;
	StartTypedWidgetTransition(WorldContextObject, Widget, WidgetProperty, Value, Time, Delay);
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
			It->CurrentValue = It->EvaluateTransitionValue();
			bTransitionEnd = It->SpringFloat->IsCompleted();
		}
		else
		{
			It->CurrentValue = It->EvaluateTransitionValue();
			bTransitionEnd = It->Time <= 0.0f || It->CurrentTime >= (It->Delay + It->Time);
		}

		if (It->bUsesTypedValue)
		{
			It->SetWidgetPropertyValue(It->EvaluateTypedTransitionValue(bTransitionEnd), bTransitionEnd);
		}
		else
		{
			It->SetWidgetPropertyValue(It->CurrentValue, bTransitionEnd);
		}

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

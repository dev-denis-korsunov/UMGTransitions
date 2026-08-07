#include "WidgetTransition.h"
#include "WidgetTransitionSettings.h"

#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "PropertyPathHelpers.h"
#include "UObject/UnrealType.h"

template <typename TValue>
static FTransitionValue MakeTransitionValue(TValue&& Value)
{
	FTransitionValue Result;
	Result.Emplace<std::decay_t<TValue>>(Forward<TValue>(Value));
	return Result;
}

bool FWidgetTransitionPropertyBinding::Resolve(UWidget* InWidget, const FString& InPropertyPath)
{
	CachedPropertyPath = FDynamicPropertyPath(InPropertyPath);
	bResolved = IsValid(InWidget) && CachedPropertyPath.IsValid() && CachedPropertyPath.Resolve(InWidget);
	bUsesDouble = false;
	if (!bResolved)
	{
		return false;
	}

	const FProperty* LeafProperty = CastField<FProperty>(CachedPropertyPath.GetLastSegment().GetField().ToField());
	const FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty);
	bUsesDouble = LeafProperty && LeafProperty->IsA<FDoubleProperty>();
	if (LeafProperty && (LeafProperty->IsA<FFloatProperty>() || bUsesDouble)) ValueType = EWidgetTransitionValueType::Float;
	else if (LeafProperty && LeafProperty->IsA<FBoolProperty>()) ValueType = EWidgetTransitionValueType::Bool;
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FVector2D>::Get()) ValueType = EWidgetTransitionValueType::Vector2D;
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FLinearColor>::Get()) ValueType = EWidgetTransitionValueType::LinearColor;
	else bResolved = false;
	return bResolved;
}

void FWidgetTransitionPropertyBinding::Invalidate()
{
	CachedPropertyPath = FDynamicPropertyPath();
	bResolved = false;
	bUsesDouble = false;
	ValueType = EWidgetTransitionValueType::Float;
}

bool FWidgetTransitionPropertyBinding::Apply(UWidget* Widget, const FTransitionValue& Value) const
{
	if (!bResolved || !IsValid(Widget)) return false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
		if (!Value.IsType<float>()) return false;
		return bUsesDouble
			? PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, static_cast<double>(Value.Get<float>()))
			: PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.Get<float>());
	case EWidgetTransitionValueType::Bool:
		return Value.IsType<bool>() && PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.Get<bool>());
	case EWidgetTransitionValueType::Vector2D:
		return Value.IsType<FVector2D>() && PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.Get<FVector2D>());
	case EWidgetTransitionValueType::LinearColor:
		return Value.IsType<FLinearColor>() && PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.Get<FLinearColor>());
	default: return false;
	}
}

bool FWidgetTransitionPropertyBinding::Read(UWidget* Widget, FTransitionValue& OutValue) const
{
	if (!bResolved || !IsValid(Widget)) return false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
	{
		float FloatValue = 0.0f;
		if (!bUsesDouble)
		{
			if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, FloatValue)) return false;
		}
		else
		{
			double DoubleValue = 0.0;
			if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, DoubleValue)) return false;
			FloatValue = static_cast<float>(DoubleValue);
		}
		OutValue.Emplace<float>(FloatValue);
		return true;
	}
	case EWidgetTransitionValueType::Bool:
	{
		bool BoolValue = false;
		if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, BoolValue)) return false;
		OutValue.Emplace<bool>(BoolValue);
		return true;
	}
	case EWidgetTransitionValueType::Vector2D:
	{
		FVector2D VectorValue = FVector2D::ZeroVector;
		if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, VectorValue)) return false;
		OutValue.Emplace<FVector2D>(VectorValue);
		return true;
	}
	case EWidgetTransitionValueType::LinearColor:
	{
		FLinearColor ColorValue = FLinearColor::White;
		if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, ColorValue)) return false;
		OutValue.Emplace<FLinearColor>(ColorValue);
		return true;
	}
	default: return false;
	}
}

static void ClearTransitionsForWidget(UWidgetTransitionSubsystem& Subsystem, UWidget* Widget)
{
	for (auto It = Subsystem.Transitions.CreateIterator(); It; ++It)
	{
		if (It->Widget == Widget)
		{
			Subsystem.EventCallbacks.Remove(It->TransitionId);
			It.RemoveCurrent();
		}
	}
}

static void StartSprings(FActiveWidgetTransition& Transition)
{
	if (!Transition.bSpring) return;
	const float Frequency = 4.0f * FMath::Pow(6.0f, FMath::Clamp(Transition.SpringSpeed, 0.0f, 1.0f));
	const float SpringFactor = Frequency * Frequency;
	const float DampingRatio = FMath::Lerp(1.0f, 0.15f, FMath::Clamp(Transition.SpringBounce, 0.0f, 1.0f));
	const float DampingFactor = 2.0f * DampingRatio * Frequency;
	if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Float)
	{
		Transition.SpringState = MakeUnique<FWidgetTransitionSpringState>();
		Transition.SpringState->Spring.Emplace<FSpringFloat>(SpringFactor, DampingFactor);
		FSpringFloat& Spring = Transition.SpringState->Spring.Get<FSpringFloat>();
		Spring.Start(Transition.FromValue.Get<float>(), Transition.ToValue.Get<float>());
	}
	else if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Vector2D)
	{
		const FVector2D& From = Transition.FromValue.Get<FVector2D>();
		const FVector2D& To = Transition.ToValue.Get<FVector2D>();
		Transition.SpringState = MakeUnique<FWidgetTransitionSpringState>();
		Transition.SpringState->Spring.Emplace<FSpringVector2D>(SpringFactor, DampingFactor);
		FSpringVector2D& Spring = Transition.SpringState->Spring.Get<FSpringVector2D>();
		Spring.Start(From, To);
	}
}

static bool RestartTransition(FActiveWidgetTransition& Transition)
{
	if (Transition.RepeatCount != -1 && Transition.CompletedRepeats >= Transition.RepeatCount) return false;
	++Transition.CompletedRepeats;
	Transition.CurrentTime = 0.0f;
	if (Transition.bYoYo) Swap(Transition.FromValue, Transition.ToValue);
	StartSprings(Transition);
	return true;
}

static void TickTransitions(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
{
	for (auto It = Subsystem.Transitions.CreateIterator(); It; ++It)
	{
		if (!It->Widget.IsValid())
		{
			Subsystem.EventCallbacks.Remove(It->TransitionId);
			It.RemoveCurrent();
			continue;
		}
		It->CurrentTime += FMath::Min(DeltaTime, 1.0f / 20.0f);
		if (It->CurrentTime < It->Delay) continue;
		if (!It->bStarted)
		{
			It->bStarted = true;
			if (const FWidgetTransitionEvents* Events = Subsystem.EventCallbacks.Find(It->TransitionId)) Events->OnStarted.ExecuteIfBound(It->Widget.Get());
		}
		bool bEnd = It->Time <= 0.0f || It->CurrentTime >= It->Delay + It->Time;
		const float Alpha = It->Time <= 0.0f ? 1.0f : FMath::Clamp((It->CurrentTime - It->Delay) / It->Time, 0.0f, 1.0f);
		const float EasedAlpha = FWidgetTransitionEasing::EvaluateCubicBezier(It->Easing.ControlPoint1, It->Easing.ControlPoint2, Alpha);
		if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::Bool)
		{
			const bool Value = bEnd ? It->ToValue.Get<bool>() : It->FromValue.Get<bool>();
			It->PropertyBinding.Apply(It->Widget.Get(), MakeTransitionValue(Value));
			if (FOnBoolWidgetTransitionUpdate* OnUpdate = It->UpdateCallback ? It->UpdateCallback->TryGet<FOnBoolWidgetTransitionUpdate>() : nullptr) OnUpdate->ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		else if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::Float)
		{
			float Value = FMath::Lerp(It->FromValue.Get<float>(), It->ToValue.Get<float>(), EasedAlpha);
			if (It->bSpring && It->SpringState.IsValid())
			{
				if (FSpringFloat* Spring = It->SpringState->Spring.TryGet<FSpringFloat>())
				{
					Spring->Tick(DeltaTime);
					Value = Spring->GetValue();
					bEnd = Spring->IsCompleted();
				}
			}
			It->PropertyBinding.Apply(It->Widget.Get(), MakeTransitionValue(Value));
			if (FOnFloatWidgetTransitionUpdate* OnUpdate = It->UpdateCallback ? It->UpdateCallback->TryGet<FOnFloatWidgetTransitionUpdate>() : nullptr) OnUpdate->ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		else if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::Vector2D)
		{
			FVector2D Value = FMath::Lerp(It->FromValue.Get<FVector2D>(), It->ToValue.Get<FVector2D>(), EasedAlpha);
			if (It->bSpring && It->SpringState.IsValid())
			{
				if (FSpringVector2D* Spring = It->SpringState->Spring.TryGet<FSpringVector2D>())
				{
					Spring->Tick(DeltaTime);
					Value = Spring->GetValue();
					bEnd = Spring->IsCompleted();
				}
			}
			It->PropertyBinding.Apply(It->Widget.Get(), MakeTransitionValue(Value));
			if (FOnVectorWidgetTransitionUpdate* OnUpdate = It->UpdateCallback ? It->UpdateCallback->TryGet<FOnVectorWidgetTransitionUpdate>() : nullptr) OnUpdate->ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		else if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::LinearColor)
		{
			const FLinearColor Value = FMath::Lerp(It->FromValue.Get<FLinearColor>(), It->ToValue.Get<FLinearColor>(), EasedAlpha);
			It->PropertyBinding.Apply(It->Widget.Get(), MakeTransitionValue(Value));
			if (FOnColorWidgetTransitionUpdate* OnUpdate = It->UpdateCallback ? It->UpdateCallback->TryGet<FOnColorWidgetTransitionUpdate>() : nullptr) OnUpdate->ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		if (bEnd && !RestartTransition(*It))
		{
			if (const FWidgetTransitionEvents* Events = Subsystem.EventCallbacks.Find(It->TransitionId)) Events->OnFinished.ExecuteIfBound(It->Widget.Get());
			if (It->bRemoveFromParent) It->Widget->RemoveFromParent();
			Subsystem.EventCallbacks.Remove(It->TransitionId);
			It.RemoveCurrent();
		}
	}
}

static FWidgetTransition MakeTransition(UWidget* Widget, const FString& WidgetProperty, FTransitionValue ToValue, EWidgetTransitionValueType ValueType, bool bUseFrom, FTransitionValue FromValue)
{
	FWidgetTransition Transition;
	Transition.Widget = Widget;
	Transition.WidgetProperty = FName(*WidgetProperty);
	Transition.ToValue = MoveTemp(ToValue);
	Transition.FromValue = MoveTemp(FromValue);
	Transition.ValueType = ValueType;
	Transition.bUseFrom = bUseFrom;
	return Transition;
}

void UWidgetTransitionFunctionLibrary::StartWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Description)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	UWidget* Widget = Description.Widget.Get();
	if (!IsValid(Widget) || !Subsystem) return;
	FActiveWidgetTransition Transition;
	Transition.Widget = Widget; Transition.WidgetProperty = Description.WidgetProperty; Transition.ToValue = MoveTemp(Description.ToValue); Transition.Time = Description.Time; Transition.Delay = Description.Delay; Transition.Easing = MoveTemp(Description.Easing);
	Transition.RepeatCount = FMath::Max(-1, Description.RepeatCount); Transition.bFrom = Description.bUseFrom; Transition.bChangeFromPropertyAfterDelay = !Description.bApplyValueBeforeDelay; Transition.bYoYo = Description.bYoYo; Transition.bRemoveFromParent = Description.bRemoveFromParent; Transition.bSpring = Description.bUseSpring;
	Transition.SpringSpeed = Description.SpringSpeed; Transition.SpringBounce = Description.SpringBounce;
	if (!Transition.PropertyBinding.Resolve(Widget, Description.WidgetProperty.ToString()) || Transition.PropertyBinding.ValueType != Description.ValueType || (!Description.bUseFrom && !Transition.PropertyBinding.Read(Widget, Transition.FromValue))) return;
	if (Description.bUseFrom) Transition.FromValue = MoveTemp(Description.FromValue);
	if (Transition.Delay > 0.0f && !Transition.bChangeFromPropertyAfterDelay) Transition.PropertyBinding.Apply(Widget, Transition.FromValue);
	if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Bool || Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::LinearColor) Transition.bSpring = false;
	if (!Description.UpdateCallback.IsType<FEmptyVariantState>()) Transition.UpdateCallback = MakeUnique<FWidgetTransitionUpdateCallback>(MoveTemp(Description.UpdateCallback));
	Transition.TransitionId = Subsystem->NextTransitionId++;
	if (Subsystem->NextTransitionId == 0) ++Subsystem->NextTransitionId;
	StartSprings(Transition);
	for (auto It = Subsystem->Transitions.CreateIterator(); It; ++It)
	{
		if (It->Widget == Widget && It->WidgetProperty == Description.WidgetProperty)
		{
			Subsystem->EventCallbacks.Remove(It->TransitionId);
			It.RemoveCurrent();
		}
	}
	if (Description.Events.HasBoundEvents()) Subsystem->EventCallbacks.Add(Transition.TransitionId, MoveTemp(Description.Events));
	Subsystem->Transitions.Emplace(MoveTemp(Transition));
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateFloatWidgetTransition(UWidget* Widget, const FString& WidgetProperty, float ToValue, bool bUseFrom, float FromValue)
{
	return MakeTransition(Widget, WidgetProperty, MakeTransitionValue(ToValue), EWidgetTransitionValueType::Float, bUseFrom, MakeTransitionValue(FromValue));
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateBoolWidgetTransition(UWidget* Widget, const FString& WidgetProperty, bool ToValue, bool bUseFrom, bool FromValue)
{
	return MakeTransition(Widget, WidgetProperty, MakeTransitionValue(ToValue), EWidgetTransitionValueType::Bool, bUseFrom, MakeTransitionValue(FromValue));
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateVectorWidgetTransition(UWidget* Widget, const FString& WidgetProperty, FVector2D ToValue, bool bUseFrom, FVector2D FromValue)
{
	return MakeTransition(Widget, WidgetProperty, MakeTransitionValue(ToValue), EWidgetTransitionValueType::Vector2D, bUseFrom, MakeTransitionValue(FromValue));
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateColorWidgetTransition(UWidget* Widget, const FString& WidgetProperty, FLinearColor ToValue, bool bUseFrom, FLinearColor FromValue)
{
	return MakeTransition(Widget, WidgetProperty, MakeTransitionValue(ToValue), EWidgetTransitionValueType::LinearColor, bUseFrom, MakeTransitionValue(FromValue));
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithDelay(FWidgetTransition Transition, float Delay, bool bApplyValueBeforeDelay)
{
	Transition.Delay = FMath::Max(0.0f, Delay);
	Transition.bApplyValueBeforeDelay = bApplyValueBeforeDelay;
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithEasing(FWidgetTransition Transition, FWidgetTransitionEasingValue Easing)
{
	Transition.Easing = MoveTemp(Easing);
	Transition.bUseSpring = false;
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithRepeat(FWidgetTransition Transition, int32 RepeatCount, bool bYoYo)
{
	Transition.RepeatCount = FMath::Max(-1, RepeatCount);
	Transition.bYoYo = bYoYo;
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithSpring(FWidgetTransition Transition, float SpringSpeed, float SpringBounce)
{
	if (Transition.ValueType == EWidgetTransitionValueType::Float || Transition.ValueType == EWidgetTransitionValueType::Vector2D)
	{
		Transition.bUseSpring = true;
		Transition.SpringSpeed = FMath::Clamp(SpringSpeed, 0.0f, 1.0f);
		Transition.SpringBounce = FMath::Clamp(SpringBounce, 0.0f, 1.0f);
	}
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithRemoveFromParent(FWidgetTransition Transition, bool bRemoveFromParent)
{
	Transition.bRemoveFromParent = bRemoveFromParent;
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithEvents(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished)
{
	Transition.Events.OnStarted = MoveTemp(OnStarted);
	Transition.Events.OnFinished = MoveTemp(OnFinished);
	return Transition;
}

template <typename TDelegate>
static FWidgetTransition WithUpdate(FWidgetTransition Transition, EWidgetTransitionValueType ExpectedType, TDelegate OnUpdate)
{
	if (Transition.ValueType == ExpectedType) Transition.UpdateCallback.Emplace<TDelegate>(MoveTemp(OnUpdate));
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::WithFloatUpdate(FWidgetTransition Transition, FOnFloatWidgetTransitionUpdate OnUpdate) { return WithUpdate(MoveTemp(Transition), EWidgetTransitionValueType::Float, MoveTemp(OnUpdate)); }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithBoolUpdate(FWidgetTransition Transition, FOnBoolWidgetTransitionUpdate OnUpdate) { return WithUpdate(MoveTemp(Transition), EWidgetTransitionValueType::Bool, MoveTemp(OnUpdate)); }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithVectorUpdate(FWidgetTransition Transition, FOnVectorWidgetTransitionUpdate OnUpdate) { return WithUpdate(MoveTemp(Transition), EWidgetTransitionValueType::Vector2D, MoveTemp(OnUpdate)); }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithColorUpdate(FWidgetTransition Transition, FOnColorWidgetTransitionUpdate OnUpdate) { return WithUpdate(MoveTemp(Transition), EWidgetTransitionValueType::LinearColor, MoveTemp(OnUpdate)); }

void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	ClearTransitionsForWidget(*Subsystem, Widget);
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }
void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickTransitions(*this, DeltaTime);
}

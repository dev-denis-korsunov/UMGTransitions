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

static void StartSprings(FWidgetTransition& Transition)
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

static bool RestartTransition(FWidgetTransition& Transition)
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

static void CreateWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FTransitionValue ToValue, float Time, float Delay, bool bApplyValueBeforeDelay, FWidgetTransitionEasingValue Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseSpring, float SpringSpeed, float SpringBounce, bool bUseFrom, FTransitionValue FromValue, EWidgetTransitionValueType ExpectedType, FOnFloatWidgetTransitionUpdate FloatOnUpdate, FOnBoolWidgetTransitionUpdate BoolOnUpdate, FOnVectorWidgetTransitionUpdate VectorOnUpdate, FOnColorWidgetTransitionUpdate ColorOnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	const FName PropertyPath(*WidgetProperty);
	FWidgetTransition Transition; Transition.Widget = Widget; Transition.WidgetProperty = PropertyPath; Transition.ToValue = MoveTemp(ToValue); Transition.Time = Time; Transition.Delay = Delay; Transition.Easing = MoveTemp(Easing);
	Transition.RepeatCount = FMath::Max(-1, RepeatCount); Transition.bFrom = bUseFrom; Transition.bChangeFromPropertyAfterDelay = !bApplyValueBeforeDelay; Transition.bYoYo = bYoYo; Transition.bRemoveFromParent = bRemoveFromParent; Transition.bSpring = bUseSpring;
	Transition.SpringSpeed = SpringSpeed; Transition.SpringBounce = SpringBounce;
	if (!Transition.PropertyBinding.Resolve(Widget, WidgetProperty) || Transition.PropertyBinding.ValueType != ExpectedType || (!bUseFrom && !Transition.PropertyBinding.Read(Widget, Transition.FromValue))) return;
	if (bUseFrom) Transition.FromValue = MoveTemp(FromValue);
	if (Transition.Delay > 0.0f && !Transition.bChangeFromPropertyAfterDelay) Transition.PropertyBinding.Apply(Widget, Transition.FromValue);
	if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Bool || Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::LinearColor) Transition.bSpring = false;
	if (FloatOnUpdate.IsBound())
	{
		Transition.UpdateCallback = MakeUnique<FWidgetTransitionUpdateCallback>();
		Transition.UpdateCallback->Emplace<FOnFloatWidgetTransitionUpdate>(MoveTemp(FloatOnUpdate));
	}
	else if (BoolOnUpdate.IsBound())
	{
		Transition.UpdateCallback = MakeUnique<FWidgetTransitionUpdateCallback>();
		Transition.UpdateCallback->Emplace<FOnBoolWidgetTransitionUpdate>(MoveTemp(BoolOnUpdate));
	}
	else if (VectorOnUpdate.IsBound())
	{
		Transition.UpdateCallback = MakeUnique<FWidgetTransitionUpdateCallback>();
		Transition.UpdateCallback->Emplace<FOnVectorWidgetTransitionUpdate>(MoveTemp(VectorOnUpdate));
	}
	else if (ColorOnUpdate.IsBound())
	{
		Transition.UpdateCallback = MakeUnique<FWidgetTransitionUpdateCallback>();
		Transition.UpdateCallback->Emplace<FOnColorWidgetTransitionUpdate>(MoveTemp(ColorOnUpdate));
	}
	Transition.TransitionId = Subsystem->NextTransitionId++;
	if (Subsystem->NextTransitionId == 0) ++Subsystem->NextTransitionId;
	FWidgetTransitionEvents Events; Events.OnStarted = MoveTemp(OnStarted); Events.OnFinished = MoveTemp(OnFinished);
	StartSprings(Transition);
	for (auto It = Subsystem->Transitions.CreateIterator(); It; ++It)
	{
		if (It->Widget == Widget && It->WidgetProperty == PropertyPath)
		{
			Subsystem->EventCallbacks.Remove(It->TransitionId);
			It.RemoveCurrent();
		}
	}
	if (Events.HasBoundEvents()) Subsystem->EventCallbacks.Add(Transition.TransitionId, MoveTemp(Events));
	Subsystem->Transitions.Emplace(MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::CreateFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float ToValue, FOnFloatWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time, float Delay, bool bApplyValueBeforeDelay, FWidgetTransitionEasingValue Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseSpring, float SpringSpeed, float SpringBounce, bool bUseFrom, float FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, bApplyValueBeforeDelay, MoveTemp(Easing), RepeatCount, bYoYo, bRemoveFromParent, bUseSpring, SpringSpeed, SpringBounce, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::Float, MoveTemp(OnUpdate), FOnBoolWidgetTransitionUpdate(), FOnVectorWidgetTransitionUpdate(), FOnColorWidgetTransitionUpdate(), MoveTemp(OnStarted), MoveTemp(OnFinished));
}

void UWidgetTransitionFunctionLibrary::CreateBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool ToValue, FOnBoolWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time, float Delay, bool bApplyValueBeforeDelay, FWidgetTransitionEasingValue Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseFrom, bool FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, bApplyValueBeforeDelay, MoveTemp(Easing), RepeatCount, bYoYo, bRemoveFromParent, false, 0.0f, 0.0f, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::Bool, FOnFloatWidgetTransitionUpdate(), MoveTemp(OnUpdate), FOnVectorWidgetTransitionUpdate(), FOnColorWidgetTransitionUpdate(), MoveTemp(OnStarted), MoveTemp(OnFinished));
}

void UWidgetTransitionFunctionLibrary::CreateVectorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D ToValue, FOnVectorWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time, float Delay, bool bApplyValueBeforeDelay, FWidgetTransitionEasingValue Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseSpring, float SpringSpeed, float SpringBounce, bool bUseFrom, FVector2D FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, bApplyValueBeforeDelay, MoveTemp(Easing), RepeatCount, bYoYo, bRemoveFromParent, bUseSpring, SpringSpeed, SpringBounce, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::Vector2D, FOnFloatWidgetTransitionUpdate(), FOnBoolWidgetTransitionUpdate(), MoveTemp(OnUpdate), FOnColorWidgetTransitionUpdate(), MoveTemp(OnStarted), MoveTemp(OnFinished));
}

void UWidgetTransitionFunctionLibrary::CreateColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor ToValue, FOnColorWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time, float Delay, bool bApplyValueBeforeDelay, FWidgetTransitionEasingValue Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseFrom, FLinearColor FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, bApplyValueBeforeDelay, MoveTemp(Easing), RepeatCount, bYoYo, bRemoveFromParent, false, 0.0f, 0.0f, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::LinearColor, FOnFloatWidgetTransitionUpdate(), FOnBoolWidgetTransitionUpdate(), FOnVectorWidgetTransitionUpdate(), MoveTemp(OnUpdate), MoveTemp(OnStarted), MoveTemp(OnFinished));
}

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

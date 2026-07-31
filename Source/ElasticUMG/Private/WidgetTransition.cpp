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
	Widget = InWidget;
	CachedPropertyPath = FDynamicPropertyPath(InPropertyPath);
	bResolved = Widget.IsValid() && CachedPropertyPath.IsValid() && CachedPropertyPath.Resolve(Widget.Get());
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
	Widget.Reset();
	CachedPropertyPath = FDynamicPropertyPath();
	bResolved = false;
	bUsesDouble = false;
	ValueType = EWidgetTransitionValueType::Float;
}

bool FWidgetTransitionPropertyBinding::Apply(const FTransitionValue& Value) const
{
	if (!bResolved || !Widget.IsValid()) return false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
		if (!Value.IsType<float>()) return false;
		return bUsesDouble
			? PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, static_cast<double>(Value.Get<float>()))
			: PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.Get<float>());
	case EWidgetTransitionValueType::Bool:
		return Value.IsType<bool>() && PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.Get<bool>());
	case EWidgetTransitionValueType::Vector2D:
		return Value.IsType<FVector2D>() && PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.Get<FVector2D>());
	case EWidgetTransitionValueType::LinearColor:
		return Value.IsType<FLinearColor>() && PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value.Get<FLinearColor>());
	default: return false;
	}
}

bool FWidgetTransitionPropertyBinding::Read(FTransitionValue& OutValue) const
{
	if (!bResolved || !Widget.IsValid()) return false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
	{
		float FloatValue = 0.0f;
		if (!bUsesDouble)
		{
			if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, FloatValue)) return false;
		}
		else
		{
			double DoubleValue = 0.0;
			if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, DoubleValue)) return false;
			FloatValue = static_cast<float>(DoubleValue);
		}
		OutValue.Emplace<float>(FloatValue);
		return true;
	}
	case EWidgetTransitionValueType::Bool:
	{
		bool BoolValue = false;
		if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, BoolValue)) return false;
		OutValue.Emplace<bool>(BoolValue);
		return true;
	}
	case EWidgetTransitionValueType::Vector2D:
	{
		FVector2D VectorValue = FVector2D::ZeroVector;
		if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, VectorValue)) return false;
		OutValue.Emplace<FVector2D>(VectorValue);
		return true;
	}
	case EWidgetTransitionValueType::LinearColor:
	{
		FLinearColor ColorValue = FLinearColor::White;
		if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, ColorValue)) return false;
		OutValue.Emplace<FLinearColor>(ColorValue);
		return true;
	}
	default: return false;
	}
}

static void ClearTransitionsForWidget(TSparseArray<FWidgetTransition>& Transitions, UWidget* Widget)
{
	for (auto It = Transitions.CreateIterator(); It; ++It)
	{
		if (It->Widget == Widget) It.RemoveCurrent();
	}
}

static void StartSprings(FWidgetTransition& Transition)
{
	if (!Transition.bUseSpring) return;
	if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Float)
	{
		Transition.FloatSpring = MakeShared<FSpringFloat>(Transition.SpringFactor, Transition.DampingFactor, Transition.MaxVelocity, Transition.CompleteTolerance);
		Transition.FloatSpring->Start(Transition.FromValue.Get<float>(), Transition.ToValue.Get<float>());
	}
	else if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Vector2D)
	{
		const FVector2D& From = Transition.FromValue.Get<FVector2D>();
		const FVector2D& To = Transition.ToValue.Get<FVector2D>();
		Transition.VectorXSpring = MakeShared<FSpringFloat>(Transition.SpringFactor, Transition.DampingFactor, Transition.MaxVelocity, Transition.CompleteTolerance);
		Transition.VectorYSpring = MakeShared<FSpringFloat>(Transition.SpringFactor, Transition.DampingFactor, Transition.MaxVelocity, Transition.CompleteTolerance);
		Transition.VectorXSpring->Start(From.X, To.X);
		Transition.VectorYSpring->Start(From.Y, To.Y);
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

static void TickTransitions(TSparseArray<FWidgetTransition>& Transitions, float DeltaTime)
{
	for (auto It = Transitions.CreateIterator(); It; ++It)
	{
		if (!It->Widget.IsValid()) { It.RemoveCurrent(); continue; }
		It->CurrentTime += FMath::Min(DeltaTime, 1.0f / 20.0f);
		if (It->CurrentTime < It->Delay) continue;
		bool bEnd = It->Time <= 0.0f || It->CurrentTime >= It->Delay + It->Time;
		const float Alpha = It->Time <= 0.0f ? 1.0f : FMath::Clamp((It->CurrentTime - It->Delay) / It->Time, 0.0f, 1.0f);
		const float EasedAlpha = GetDefault<UWidgetTransitionSettings>()->EvaluateEasing(It->Easing, Alpha);
		if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::Bool)
		{
			const bool Value = bEnd ? It->ToValue.Get<bool>() : It->FromValue.Get<bool>();
			It->PropertyBinding.Apply(MakeTransitionValue(Value));
			It->BoolOnUpdate.ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		else if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::Float)
		{
			float Value = FMath::Lerp(It->FromValue.Get<float>(), It->ToValue.Get<float>(), EasedAlpha);
			if (It->bUseSpring && It->FloatSpring.IsValid())
			{
				It->FloatSpring->Tick(DeltaTime);
				Value = It->FloatSpring->GetValue();
				bEnd = It->FloatSpring->IsCompleted();
			}
			It->PropertyBinding.Apply(MakeTransitionValue(Value));
			It->FloatOnUpdate.ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		else if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::Vector2D)
		{
			FVector2D Value = FMath::Lerp(It->FromValue.Get<FVector2D>(), It->ToValue.Get<FVector2D>(), EasedAlpha);
			if (It->bUseSpring && It->VectorXSpring.IsValid() && It->VectorYSpring.IsValid())
			{
				It->VectorXSpring->Tick(DeltaTime);
				It->VectorYSpring->Tick(DeltaTime);
				Value = FVector2D(It->VectorXSpring->GetValue(), It->VectorYSpring->GetValue());
				bEnd = It->VectorXSpring->IsCompleted() && It->VectorYSpring->IsCompleted();
			}
			It->PropertyBinding.Apply(MakeTransitionValue(Value));
			It->VectorOnUpdate.ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		else if (It->PropertyBinding.ValueType == EWidgetTransitionValueType::LinearColor)
		{
			const FLinearColor Value = FMath::Lerp(It->FromValue.Get<FLinearColor>(), It->ToValue.Get<FLinearColor>(), EasedAlpha);
			It->PropertyBinding.Apply(MakeTransitionValue(Value));
			It->ColorOnUpdate.ExecuteIfBound(It->Widget.Get(), Value, Alpha);
		}
		if (bEnd && !RestartTransition(*It))
		{
			if (It->bRemoveFromParent) It->Widget->RemoveFromParent();
			It.RemoveCurrent();
		}
	}
}

static void CreateWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FTransitionValue ToValue, float Time, float Delay, FName Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseSpring, float SpringFactor, float DampingFactor, float MaxVelocity, float CompleteTolerance, bool bUseFrom, FTransitionValue FromValue, EWidgetTransitionValueType ExpectedType, FOnFloatWidgetTransitionUpdate FloatOnUpdate, FOnBoolWidgetTransitionUpdate BoolOnUpdate, FOnVectorWidgetTransitionUpdate VectorOnUpdate, FOnColorWidgetTransitionUpdate ColorOnUpdate)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	FWidgetTransition Transition; Transition.Widget = Widget; Transition.WidgetProperty = WidgetProperty; Transition.ToValue = MoveTemp(ToValue); Transition.Time = Time; Transition.Delay = Delay; Transition.Easing = Easing;
	Transition.RepeatCount = FMath::Max(-1, RepeatCount); Transition.bYoYo = bYoYo; Transition.bRemoveFromParent = bRemoveFromParent; Transition.bUseSpring = bUseSpring;
	Transition.SpringFactor = SpringFactor; Transition.DampingFactor = DampingFactor; Transition.MaxVelocity = MaxVelocity; Transition.CompleteTolerance = CompleteTolerance;
	if (!Transition.PropertyBinding.Resolve(Widget, WidgetProperty) || Transition.PropertyBinding.ValueType != ExpectedType || (!bUseFrom && !Transition.PropertyBinding.Read(Transition.FromValue))) return;
	if (bUseFrom) Transition.FromValue = MoveTemp(FromValue);
	if (Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::Bool || Transition.PropertyBinding.ValueType == EWidgetTransitionValueType::LinearColor) Transition.bUseSpring = false;
	Transition.FloatOnUpdate = MoveTemp(FloatOnUpdate); Transition.BoolOnUpdate = MoveTemp(BoolOnUpdate); Transition.VectorOnUpdate = MoveTemp(VectorOnUpdate); Transition.ColorOnUpdate = MoveTemp(ColorOnUpdate);
	StartSprings(Transition);
	for (auto It = Subsystem->Transitions.CreateIterator(); It; ++It) if (It->Widget == Widget && It->WidgetProperty == WidgetProperty) It.RemoveCurrent();
	Subsystem->Transitions.Emplace(MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::CreateFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float ToValue, FOnFloatWidgetTransitionUpdate OnUpdate, float Time, float Delay, FName Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseSpring, float SpringFactor, float DampingFactor, float MaxVelocity, float CompleteTolerance, bool bUseFrom, float FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, Easing, RepeatCount, bYoYo, bRemoveFromParent, bUseSpring, SpringFactor, DampingFactor, MaxVelocity, CompleteTolerance, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::Float, MoveTemp(OnUpdate), FOnBoolWidgetTransitionUpdate(), FOnVectorWidgetTransitionUpdate(), FOnColorWidgetTransitionUpdate());
}

void UWidgetTransitionFunctionLibrary::CreateBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool ToValue, FOnBoolWidgetTransitionUpdate OnUpdate, float Time, float Delay, FName Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseFrom, bool FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, Easing, RepeatCount, bYoYo, bRemoveFromParent, false, 0.0f, 0.0f, 0.0f, 0.0f, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::Bool, FOnFloatWidgetTransitionUpdate(), MoveTemp(OnUpdate), FOnVectorWidgetTransitionUpdate(), FOnColorWidgetTransitionUpdate());
}

void UWidgetTransitionFunctionLibrary::CreateVectorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D ToValue, FOnVectorWidgetTransitionUpdate OnUpdate, float Time, float Delay, FName Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseSpring, float SpringFactor, float DampingFactor, float MaxVelocity, float CompleteTolerance, bool bUseFrom, FVector2D FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, Easing, RepeatCount, bYoYo, bRemoveFromParent, bUseSpring, SpringFactor, DampingFactor, MaxVelocity, CompleteTolerance, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::Vector2D, FOnFloatWidgetTransitionUpdate(), FOnBoolWidgetTransitionUpdate(), MoveTemp(OnUpdate), FOnColorWidgetTransitionUpdate());
}

void UWidgetTransitionFunctionLibrary::CreateColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor ToValue, FOnColorWidgetTransitionUpdate OnUpdate, float Time, float Delay, FName Easing, int32 RepeatCount, bool bYoYo, bool bRemoveFromParent, bool bUseFrom, FLinearColor FromValue)
{
	CreateWidgetTransition(WorldContextObject, Widget, WidgetProperty, MakeTransitionValue(ToValue), Time, Delay, Easing, RepeatCount, bYoYo, bRemoveFromParent, false, 0.0f, 0.0f, 0.0f, 0.0f, bUseFrom, MakeTransitionValue(FromValue), EWidgetTransitionValueType::LinearColor, FOnFloatWidgetTransitionUpdate(), FOnBoolWidgetTransitionUpdate(), FOnVectorWidgetTransitionUpdate(), MoveTemp(OnUpdate));
}

void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	ClearTransitionsForWidget(Subsystem->Transitions, Widget);
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }
void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickTransitions(Transitions, DeltaTime);
}

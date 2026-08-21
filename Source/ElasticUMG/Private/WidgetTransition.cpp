#include "WidgetTransition.h"

#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "PropertyPathHelpers.h"
#include "UObject/UnrealType.h"

namespace WidgetTransition
{
	static FWidgetTransitionValue MakeValue(float Value)
	{
		FWidgetTransitionValue Result;
		Result.Channels.X = Value;
		Result.Type = EWidgetTransitionValueType::Float;
		return Result;
	}

	static FWidgetTransitionValue MakeValue(const FVector2D& Value)
	{
		FWidgetTransitionValue Result;
		Result.Channels = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		Result.Type = EWidgetTransitionValueType::Vector2D;
		return Result;
	}

	static FWidgetTransitionValue MakeValue(const FLinearColor& Value)
	{
		FWidgetTransitionValue Result;
		Result.Channels = FVector4f(Value.R, Value.G, Value.B, Value.A);
		Result.Type = EWidgetTransitionValueType::LinearColor;
		return Result;
	}

	static bool NormalizeValue(const FWidgetTransitionValue& Value, uint8 ChannelCount, FVector4f& OutValue)
	{
		if (ChannelCount == 0 || ChannelCount > 4) return false;
		switch (Value.Type)
		{
		case EWidgetTransitionValueType::Float:
			OutValue = FVector4f(Value.Channels.X, Value.Channels.X, Value.Channels.X, Value.Channels.X);
			return true;
		case EWidgetTransitionValueType::Vector2D:
			if (ChannelCount != 2) return false;
			OutValue = Value.Channels;
			return true;
		case EWidgetTransitionValueType::LinearColor:
			if (ChannelCount != 4) return false;
			OutValue = Value.Channels;
			return true;
		default:
			return false;
		}
	}

	static bool IsSpringCompatible(uint8 ChannelCount) { return ChannelCount == 1 || ChannelCount == 2; }

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
		if (!Transition.bSpring || !IsSpringCompatible(Transition.PropertyBinding.ChannelCount)) return;
		const float Frequency = 4.0f * FMath::Pow(6.0f, FMath::Clamp(Transition.SpringSpeed, 0.0f, 1.0f));
		const float SpringFactor = Frequency * Frequency;
		const float DampingRatio = FMath::Lerp(1.0f, 0.15f, FMath::Clamp(Transition.SpringBounce, 0.0f, 1.0f));
		const float DampingFactor = 2.0f * DampingRatio * Frequency;
		Transition.SpringState = MakeUnique<FWidgetTransitionSpringState>();
		if (Transition.PropertyBinding.ChannelCount == 1)
		{
			Transition.SpringState->Spring.Emplace<FSpringFloat>(SpringFactor, DampingFactor);
			Transition.SpringState->Spring.Get<FSpringFloat>().Start(Transition.FromValue.X, Transition.ToValue.X);
		}
		else
		{
			Transition.SpringState->Spring.Emplace<FSpringVector2D>(SpringFactor, DampingFactor);
			Transition.SpringState->Spring.Get<FSpringVector2D>().Start(FVector2D(Transition.FromValue.X, Transition.FromValue.Y), FVector2D(Transition.ToValue.X, Transition.ToValue.Y));
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
			bool bEnd = !It->bSpring && (It->Time <= 0.0f || It->CurrentTime >= It->Delay + It->Time);
			const float Alpha = It->Time <= 0.0f ? 1.0f : FMath::Clamp((It->CurrentTime - It->Delay) / It->Time, 0.0f, 1.0f);
			const float EasedAlpha = FWidgetTransitionEasing::EvaluateCubicBezier(It->Easing.ControlPoint1, It->Easing.ControlPoint2, Alpha);
			FVector4f Value = FMath::Lerp(It->FromValue, It->ToValue, EasedAlpha);
			if (It->bSpring && It->SpringState.IsValid())
			{
				if (FSpringFloat* Spring = It->SpringState->Spring.TryGet<FSpringFloat>())
				{
					Spring->Tick(DeltaTime);
					Value.X = Spring->GetValue();
					bEnd = Spring->IsCompleted();
				}
				else if (FSpringVector2D* VectorSpring = It->SpringState->Spring.TryGet<FSpringVector2D>())
				{
					VectorSpring->Tick(DeltaTime);
					const FVector2D SpringValue = VectorSpring->GetValue();
					Value.X = SpringValue.X;
					Value.Y = SpringValue.Y;
					bEnd = VectorSpring->IsCompleted();
				}
			}
			It->PropertyBinding.Apply(It->Widget.Get(), Value);
			It->OnUpdate.ExecuteIfBound(It->Widget.Get(), Alpha, EasedAlpha);
			if (bEnd && !RestartTransition(*It))
			{
				if (const FWidgetTransitionEvents* Events = Subsystem.EventCallbacks.Find(It->TransitionId)) Events->OnFinished.ExecuteIfBound(It->Widget.Get());
				if (It->bRemoveFromParent) It->Widget->RemoveFromParent();
				Subsystem.EventCallbacks.Remove(It->TransitionId);
				It.RemoveCurrent();
			}
		}
	}
}

bool FWidgetTransitionPropertyBinding::Resolve(UWidget* InWidget, const FString& InPropertyPath)
{
	CachedPropertyPath = FDynamicPropertyPath(InPropertyPath);
	bResolved = IsValid(InWidget) && CachedPropertyPath.IsValid() && CachedPropertyPath.Resolve(InWidget);
	bUsesDouble = false;
	ChannelCount = 0;
	if (!bResolved) return false;
	const FProperty* LeafProperty = CastField<FProperty>(CachedPropertyPath.GetLastSegment().GetField().ToField());
	const FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty);
	bUsesDouble = LeafProperty && LeafProperty->IsA<FDoubleProperty>();
	if (LeafProperty && (LeafProperty->IsA<FFloatProperty>() || bUsesDouble)) { ValueType = EWidgetTransitionValueType::Float; ChannelCount = 1; }
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FVector2D>::Get()) { ValueType = EWidgetTransitionValueType::Vector2D; ChannelCount = 2; }
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FLinearColor>::Get()) { ValueType = EWidgetTransitionValueType::LinearColor; ChannelCount = 4; }
	else bResolved = false;
	return bResolved;
}

void FWidgetTransitionPropertyBinding::Invalidate()
{
	CachedPropertyPath = FDynamicPropertyPath();
	bResolved = false;
	bUsesDouble = false;
	ChannelCount = 0;
	ValueType = EWidgetTransitionValueType::Float;
}

bool FWidgetTransitionPropertyBinding::Apply(UWidget* Widget, const FVector4f& Value) const
{
	if (!bResolved || !IsValid(Widget)) return false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
		return bUsesDouble ? PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, static_cast<double>(Value.X)) : PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.X);
	case EWidgetTransitionValueType::Vector2D:
		return PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, FVector2D(Value.X, Value.Y));
	case EWidgetTransitionValueType::LinearColor:
		return PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, FLinearColor(Value.X, Value.Y, Value.Z, Value.W));
	default:
		return false;
	}
}

bool FWidgetTransitionPropertyBinding::Read(UWidget* Widget, FVector4f& OutValue) const
{
	if (!bResolved || !IsValid(Widget)) return false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
	{
		float Value = 0.0f;
		if (!bUsesDouble && !PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, Value)) return false;
		if (bUsesDouble) { double DoubleValue = 0.0; if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, DoubleValue)) return false; Value = static_cast<float>(DoubleValue); }
		OutValue = FVector4f(Value, Value, Value, Value);
		return true;
	}
	case EWidgetTransitionValueType::Vector2D:
	{
		FVector2D Value; if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, Value)) return false;
		OutValue = FVector4f(Value.X, Value.Y, 0.0f, 0.0f); return true;
	}
	case EWidgetTransitionValueType::LinearColor:
	{
		FLinearColor Value; if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, Value)) return false;
		OutValue = FVector4f(Value.R, Value.G, Value.B, Value.A); return true;
	}
	default: return false;
	}
}

void UWidgetTransitionFunctionLibrary::StartWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Description)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	UWidget* Widget = Description.Widget.Get();
	if (!IsValid(Widget) || !Subsystem) return;
	FActiveWidgetTransition Transition;
	Transition.Widget = Widget;
	Transition.WidgetProperty = Description.WidgetProperty;
	Transition.Time = FMath::Max(0.0f, Description.Time);
	Transition.Delay = FMath::Max(0.0f, Description.Delay);
	Transition.Easing = MoveTemp(Description.Easing);
	Transition.RepeatCount = FMath::Max(-1, Description.RepeatCount);
	Transition.bFrom = Description.bUseFrom;
	Transition.bChangeFromPropertyAfterDelay = !Description.bApplyValueBeforeDelay;
	Transition.bYoYo = Description.bYoYo;
	Transition.bRemoveFromParent = Description.bRemoveFromParent;
	Transition.bSpring = Description.bUseSpring;
	Transition.SpringSpeed = Description.SpringSpeed;
	Transition.SpringBounce = Description.SpringBounce;
	Transition.OnUpdate = MoveTemp(Description.OnUpdate);
	if (!Transition.PropertyBinding.Resolve(Widget, Description.WidgetProperty.ToString())) return;
	if (!WidgetTransition::NormalizeValue(Description.ToValue, Transition.PropertyBinding.ChannelCount, Transition.ToValue))
	{
		UE_LOG(LogTemp, Warning, TEXT("Widget Transition: target value type is incompatible with '%s'."), *Description.WidgetProperty.ToString());
		return;
	}
	if (Description.bUseFrom)
	{
		if (!WidgetTransition::NormalizeValue(Description.FromValue, Transition.PropertyBinding.ChannelCount, Transition.FromValue))
		{
			UE_LOG(LogTemp, Warning, TEXT("Widget Transition: From value type is incompatible with '%s'."), *Description.WidgetProperty.ToString());
			return;
		}
	}
	else if (!Transition.PropertyBinding.Read(Widget, Transition.FromValue)) return;
	if (Transition.Delay > 0.0f && !Transition.bChangeFromPropertyAfterDelay) Transition.PropertyBinding.Apply(Widget, Transition.FromValue);
	if (!WidgetTransition::IsSpringCompatible(Transition.PropertyBinding.ChannelCount)) Transition.bSpring = false;
	Transition.TransitionId = Subsystem->NextTransitionId++;
	if (Subsystem->NextTransitionId == 0) ++Subsystem->NextTransitionId;
	WidgetTransition::StartSprings(Transition);
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

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateFloatWidgetTransition(float ToValue, float Delay, float Time) { FWidgetTransition Transition; Transition.ToValue = WidgetTransition::MakeValue(ToValue); Transition.Delay = FMath::Max(0.0f, Delay); Transition.Time = FMath::Max(0.0f, Time); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::CreateVectorWidgetTransition(FVector2D ToValue, float Delay, float Time) { FWidgetTransition Transition; Transition.ToValue = WidgetTransition::MakeValue(ToValue); Transition.Delay = FMath::Max(0.0f, Delay); Transition.Time = FMath::Max(0.0f, Time); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::CreateColorWidgetTransition(FLinearColor ToValue, float Delay, float Time) { FWidgetTransition Transition; Transition.ToValue = WidgetTransition::MakeValue(ToValue); Transition.Delay = FMath::Max(0.0f, Delay); Transition.Time = FMath::Max(0.0f, Time); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithBinding(FWidgetTransition Transition, UWidget* Widget, const FString& WidgetProperty) { Transition.Widget = Widget; Transition.WidgetProperty = FName(*WidgetProperty); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithFloatFrom(FWidgetTransition Transition, float FromValue) { Transition.FromValue = WidgetTransition::MakeValue(FromValue); Transition.bUseFrom = true; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithVectorFrom(FWidgetTransition Transition, FVector2D FromValue) { Transition.FromValue = WidgetTransition::MakeValue(FromValue); Transition.bUseFrom = true; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithColorFrom(FWidgetTransition Transition, FLinearColor FromValue) { Transition.FromValue = WidgetTransition::MakeValue(FromValue); Transition.bUseFrom = true; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithDelay(FWidgetTransition Transition, float Delay, bool bApplyValueBeforeDelay) { Transition.Delay = FMath::Max(0.0f, Delay); Transition.bApplyValueBeforeDelay = bApplyValueBeforeDelay; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithEasing(FWidgetTransition Transition, FWidgetTransitionEasingValue Easing) { Transition.Easing = MoveTemp(Easing); Transition.bUseSpring = false; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithRepeat(FWidgetTransition Transition, int32 RepeatCount, bool bYoYo) { Transition.RepeatCount = FMath::Max(-1, RepeatCount); Transition.bYoYo = bYoYo; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithSpring(FWidgetTransition Transition, float SpringSpeed, float SpringBounce) { Transition.bUseSpring = true; Transition.SpringSpeed = FMath::Clamp(SpringSpeed, 0.0f, 1.0f); Transition.SpringBounce = FMath::Clamp(SpringBounce, 0.0f, 1.0f); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithRemoveFromParent(FWidgetTransition Transition, bool bRemoveFromParent) { Transition.bRemoveFromParent = bRemoveFromParent; return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithEvents(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished) { Transition.Events.OnStarted = MoveTemp(OnStarted); Transition.Events.OnFinished = MoveTemp(OnFinished); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithOnStart(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted) { Transition.Events.OnStarted = MoveTemp(OnStarted); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithOnFinish(FWidgetTransition Transition, FOnWidgetTransitionEvent OnFinished) { Transition.Events.OnFinished = MoveTemp(OnFinished); return Transition; }
FWidgetTransition UWidgetTransitionFunctionLibrary::WithUpdate(FWidgetTransition Transition, FOnWidgetTransitionUpdate OnUpdate) { Transition.OnUpdate = MoveTemp(OnUpdate); return Transition; }

void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (IsValid(Widget) && Subsystem) WidgetTransition::ClearTransitionsForWidget(*Subsystem, Widget);
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }
void UWidgetTransitionSubsystem::Tick(float DeltaTime) { Super::Tick(DeltaTime); WidgetTransition::TickTransitions(*this, DeltaTime); }

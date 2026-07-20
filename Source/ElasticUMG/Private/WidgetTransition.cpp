#include "WidgetTransition.h"

#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "PropertyPathHelpers.h"
#include "UObject/UnrealType.h"

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

bool FWidgetTransitionPropertyBinding::Apply(float Value) const
{
	return bResolved && Widget.IsValid() && (bUsesDouble
		? PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, static_cast<double>(Value))
		: PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value));
}

bool FWidgetTransitionPropertyBinding::Apply(bool Value) const { return bResolved && Widget.IsValid() && PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value); }
bool FWidgetTransitionPropertyBinding::Apply(const FVector2D& Value) const { return bResolved && Widget.IsValid() && PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value); }
bool FWidgetTransitionPropertyBinding::Apply(const FLinearColor& Value) const { return bResolved && Widget.IsValid() && PropertyPathHelpers::SetPropertyValue(Widget.Get(), CachedPropertyPath, Value); }

bool FWidgetTransitionPropertyBinding::Read(float& OutValue) const
{
	if (!bResolved || !Widget.IsValid()) return false;
	if (!bUsesDouble) return PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue);
	double DoubleValue = 0.0;
	if (!PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, DoubleValue)) return false;
	OutValue = static_cast<float>(DoubleValue);
	return true;
}

bool FWidgetTransitionPropertyBinding::Read(bool& OutValue) const { return bResolved && Widget.IsValid() && PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue); }
bool FWidgetTransitionPropertyBinding::Read(FVector2D& OutValue) const { return bResolved && Widget.IsValid() && PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue); }
bool FWidgetTransitionPropertyBinding::Read(FLinearColor& OutValue) const { return bResolved && Widget.IsValid() && PropertyPathHelpers::GetPropertyValue(Widget.Get(), CachedPropertyPath, OutValue); }

template <typename TValue>
static void ClearTransitionsForWidget(TWidgetTransitionStorage<TValue>& Storage, UWidget* Widget)
{
	for (auto It = Storage.Transitions.CreateIterator(); It; ++It)
	{
		if (It->Widget == Widget) It.RemoveCurrent();
	}
}

template <typename TValue>
static void TickTransitions(TWidgetTransitionStorage<TValue>& Storage, float DeltaTime)
{
	for (auto It = Storage.Transitions.CreateIterator(); It; ++It)
	{
		if (!It->Widget.IsValid()) { It.RemoveCurrent(); continue; }
		It->CurrentTime += FMath::Min(DeltaTime, 1.0f / 20.0f);
		if (It->CurrentTime < It->Delay) continue;
		const bool bEnd = It->Time <= 0.0f || It->CurrentTime >= It->Delay + It->Time;
		const float Alpha = It->Time <= 0.0f ? 1.0f : FMath::Clamp((It->CurrentTime - It->Delay) / It->Time, 0.0f, 1.0f);
		if constexpr (std::is_same_v<TValue, bool>) It->PropertyBinding.Apply(bEnd ? It->ToValue : It->FromValue);
		else It->PropertyBinding.Apply(FMath::Lerp(It->FromValue, It->ToValue, Alpha));
		if (bEnd) It.RemoveCurrent();
	}
}

void UWidgetTransitionFunctionLibrary::CreateFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float TargetValue, float Time, float Delay)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	TWidgetTransition<float> Transition; Transition.Widget = Widget; Transition.WidgetProperty = WidgetProperty; Transition.ToValue = TargetValue; Transition.Time = Time; Transition.Delay = Delay;
	if (!Transition.PropertyBinding.Resolve(Widget, WidgetProperty) || Transition.PropertyBinding.ValueType != EWidgetTransitionValueType::Float || !Transition.PropertyBinding.Read(Transition.FromValue)) return;
	for (auto It = Subsystem->FloatTransitions.Transitions.CreateIterator(); It; ++It) if (It->Widget == Widget && It->WidgetProperty == WidgetProperty) It.RemoveCurrent();
	Subsystem->FloatTransitions.Transitions.Emplace(MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::CreateBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool TargetValue, float Time, float Delay)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	TWidgetTransition<bool> Transition; Transition.Widget = Widget; Transition.WidgetProperty = WidgetProperty; Transition.ToValue = TargetValue; Transition.Time = Time; Transition.Delay = Delay;
	if (!Transition.PropertyBinding.Resolve(Widget, WidgetProperty) || Transition.PropertyBinding.ValueType != EWidgetTransitionValueType::Bool || !Transition.PropertyBinding.Read(Transition.FromValue)) return;
	for (auto It = Subsystem->BoolTransitions.Transitions.CreateIterator(); It; ++It) if (It->Widget == Widget && It->WidgetProperty == WidgetProperty) It.RemoveCurrent();
	Subsystem->BoolTransitions.Transitions.Emplace(MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::CreateVectorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D TargetValue, float Time, float Delay)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	TWidgetTransition<FVector2D> Transition; Transition.Widget = Widget; Transition.WidgetProperty = WidgetProperty; Transition.ToValue = TargetValue; Transition.Time = Time; Transition.Delay = Delay;
	if (!Transition.PropertyBinding.Resolve(Widget, WidgetProperty) || Transition.PropertyBinding.ValueType != EWidgetTransitionValueType::Vector2D || !Transition.PropertyBinding.Read(Transition.FromValue)) return;
	for (auto It = Subsystem->VectorTransitions.Transitions.CreateIterator(); It; ++It) if (It->Widget == Widget && It->WidgetProperty == WidgetProperty) It.RemoveCurrent();
	Subsystem->VectorTransitions.Transitions.Emplace(MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::CreateColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor TargetValue, float Time, float Delay)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	TWidgetTransition<FLinearColor> Transition; Transition.Widget = Widget; Transition.WidgetProperty = WidgetProperty; Transition.ToValue = TargetValue; Transition.Time = Time; Transition.Delay = Delay;
	if (!Transition.PropertyBinding.Resolve(Widget, WidgetProperty) || Transition.PropertyBinding.ValueType != EWidgetTransitionValueType::LinearColor || !Transition.PropertyBinding.Read(Transition.FromValue)) return;
	for (auto It = Subsystem->ColorTransitions.Transitions.CreateIterator(); It; ++It) if (It->Widget == Widget && It->WidgetProperty == WidgetProperty) It.RemoveCurrent();
	Subsystem->ColorTransitions.Transitions.Emplace(MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!IsValid(Widget) || !Subsystem) return;
	ClearTransitionsForWidget(Subsystem->FloatTransitions, Widget);
	ClearTransitionsForWidget(Subsystem->BoolTransitions, Widget);
	ClearTransitionsForWidget(Subsystem->VectorTransitions, Widget);
	ClearTransitionsForWidget(Subsystem->ColorTransitions, Widget);
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }
void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickTransitions(FloatTransitions, DeltaTime);
	TickTransitions(BoolTransitions, DeltaTime);
	TickTransitions(VectorTransitions, DeltaTime);
	TickTransitions(ColorTransitions, DeltaTime);
}

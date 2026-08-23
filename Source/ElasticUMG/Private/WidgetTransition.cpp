#include "WidgetTransition.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Curves/RealCurve.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
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
		if (ChannelCount == 0 || ChannelCount > 4)
		{
			return false;
		}
		switch (Value.Type)
		{
		case EWidgetTransitionValueType::Float:
		{
			OutValue = FVector4f(Value.Channels.X, Value.Channels.X, Value.Channels.X, Value.Channels.X);
			return true;
		}
		case EWidgetTransitionValueType::Vector2D:
		{
			if (ChannelCount != 2)
			{
				return false;
			}
			OutValue = Value.Channels;
			return true;
		}
		case EWidgetTransitionValueType::LinearColor:
		{
			if (ChannelCount != 4)
			{
				return false;
			}
			OutValue = Value.Channels;
			return true;
		}
		default:
		{
			return false;
		}
		}
	}

	static bool IsSpringCompatible(uint8 ChannelCount)
	{
		return ChannelCount >= 1 && ChannelCount <= 4;
	}

	static bool IsMaterialBinding(FName WidgetProperty)
	{
		return WidgetProperty.ToString().StartsWith(TEXT("Material."));
	}

	static FName GetMaterialParameter(FName WidgetProperty)
	{
		return FName(*WidgetProperty.ToString().RightChop(9));
	}

	static void ClearTransitionsForWidget(UWidgetTransitionSubsystem& Subsystem, UWidget* Widget)
	{
		for (auto It = Subsystem.Transitions.CreateIterator(); It; ++It)
		{
			if (It->Widget == Widget)
			{
				Subsystem.Callbacks.Remove(It->TransitionId);
				It.RemoveCurrent();
			}
		}
	}

	static void StartSprings(FWidgetTransition& Transition)
	{
		if (!Transition.bUseSpring || !IsSpringCompatible(Transition.PropertyBinding.ChannelCount))
		{
			return;
		}
		const float DampingRatio = FMath::Lerp(1.0f, 0.15f, FMath::Clamp(Transition.SpringBounce, 0.0f, 1.0f));
		// e^(-zeta * omega * Time) <= 0.001: choose omega so the envelope
		// reaches the same relative tolerance used by FSpringVector4f completion tests.
		const float Frequency = Transition.bFitSpringToTime && Transition.Time > UE_SMALL_NUMBER
									? (-FMath::Loge(0.001f) / (DampingRatio * Transition.Time)) * FMath::Lerp(1.0f, 3.0f, FMath::Clamp(Transition.SpringSpeed, 0.0f, 1.0f))
									: 4.0f * FMath::Pow(6.0f, FMath::Clamp(Transition.SpringSpeed, 0.0f, 1.0f));
		const float SpringFactor = Frequency * Frequency;
		const float DampingFactor = 2.0f * DampingRatio * Frequency;
		Transition.SpringState = MakeShared<FWidgetTransitionSpringState>(SpringFactor, DampingFactor);
		Transition.SpringState->Spring.Start(Transition.FromValue.Channels, Transition.ToValue.Channels);
	}

	static bool RestartTransition(FWidgetTransition& Transition)
	{
		if (Transition.RepeatCount == 0)
		{
			return false;
		}
		if (Transition.RepeatCount > 0)
		{
			--Transition.RepeatCount;
		}
		Transition.CurrentTime = 0.0f;
		if (Transition.bYoYo)
		{
			Swap(Transition.FromValue, Transition.ToValue);
		}
		StartSprings(Transition);
		return true;
	}

	static void TickTransitions(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
	{
		for (auto It = Subsystem.Transitions.CreateIterator(); It; ++It)
		{
			if (It->bBound && !It->Widget.IsValid())
			{
				Subsystem.Callbacks.Remove(It->TransitionId);
				It.RemoveCurrent();
				continue;
			}
			It->CurrentTime += FMath::Min(DeltaTime, 1.0f / 20.0f);
			if (It->CurrentTime < It->Delay)
			{
				continue;
			}
			if (!It->bStarted)
			{
				It->bStarted = true;
				if (It->bHasCallbacks)
				{
					if (const FWidgetTransitionCallbacks* Callbacks = Subsystem.Callbacks.Find(It->TransitionId))
					{
						Callbacks->OnStarted.ExecuteIfBound(It->Widget.Get());
					}
				}
			}
			bool bEnd = !It->bUseSpring && (It->Time <= 0.0f || It->CurrentTime >= It->Delay + It->Time);
			const float Alpha = It->Time <= 0.0f ? 1.0f : FMath::Clamp((It->CurrentTime - It->Delay) / It->Time, 0.0f, 1.0f);
			float EasedAlpha = Alpha;
			if (!It->Easing.IsNull())
			{
#if UE_BUILD_SHIPPING
				EasedAlpha = It->EasingCurve ? It->EasingCurve->Eval(Alpha) : 0.0f;
#else
				EasedAlpha = It->Easing.Eval(Alpha, TEXT("Widget Transition"));
#endif
			}
			FVector4f Value = FMath::Lerp(It->FromValue.Channels, It->ToValue.Channels, EasedAlpha);
			const bool bReachedSpringDeadline = It->bUseSpring && It->bFitSpringToTime && It->CurrentTime >= It->Delay + It->Time;
			if (It->bUseSpring && It->SpringState.IsValid() && !bReachedSpringDeadline)
			{
				It->SpringState->Spring.Tick(DeltaTime);
				Value = It->SpringState->Spring.GetValue();
				bEnd = It->SpringState->Spring.IsCompleted();
			}
			if (bReachedSpringDeadline)
			{
				Value = It->ToValue.Channels;
				bEnd = true;
			}
			if (It->bBound)
			{
				It->PropertyBinding.Apply(It->Widget.Get(), Value);
			}
			if (It->bHasCallbacks)
			{
				if (const FWidgetTransitionCallbacks* Callbacks = Subsystem.Callbacks.Find(It->TransitionId))
				{
					Callbacks->OnUpdated.ExecuteIfBound(It->Widget.Get(), Alpha, EasedAlpha);
				}
			}
			if (bEnd && !RestartTransition(*It))
			{
				if (It->bHasCallbacks)
				{
					if (const FWidgetTransitionCallbacks* Callbacks = Subsystem.Callbacks.Find(It->TransitionId))
					{
						Callbacks->OnFinished.ExecuteIfBound(It->Widget.Get());
					}
				}
				if (It->bRemoveFromParent && It->Widget.IsValid())
				{
					It->Widget->RemoveFromParent();
				}
				Subsystem.Callbacks.Remove(It->TransitionId);
				It.RemoveCurrent();
			}
		}
	}
} // namespace WidgetTransition

bool FWidgetTransitionPropertyBinding::Resolve(UWidget* InWidget, const FString& InPropertyPath)
{
	Kind = EWidgetTransitionBindingKind::Property;
	MaterialInstance.Reset();
	MaterialParameter = NAME_None;
	CachedPropertyPath = FDynamicPropertyPath(InPropertyPath);
	bResolved = IsValid(InWidget) && CachedPropertyPath.IsValid() && CachedPropertyPath.Resolve(InWidget);
	bUsesDouble = false;
	ChannelCount = 0;
	if (!bResolved)
	{
		return false;
	}
	const FProperty* LeafProperty = CastField<FProperty>(CachedPropertyPath.GetLastSegment().GetField().ToField());
	const FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty);
	bUsesDouble = LeafProperty && LeafProperty->IsA<FDoubleProperty>();
	if (LeafProperty && (LeafProperty->IsA<FFloatProperty>() || bUsesDouble))
	{
		ValueType = EWidgetTransitionValueType::Float;
		ChannelCount = 1;
	}
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FVector2D>::Get())
	{
		ValueType = EWidgetTransitionValueType::Vector2D;
		ChannelCount = 2;
	}
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
	{
		ValueType = EWidgetTransitionValueType::LinearColor;
		ChannelCount = 4;
	}
	else
	{
		bResolved = false;
	}
	return bResolved;
}

bool FWidgetTransitionPropertyBinding::ResolveMaterial(UWidget* InWidget, FName InParameter)
{
	Invalidate();
	UMaterialInstanceDynamic* DynamicMaterial = nullptr;
	if (UImage* Image = Cast<UImage>(InWidget))
	{
		DynamicMaterial = Image->GetDynamicMaterial();
	}
	else if (UBorder* Border = Cast<UBorder>(InWidget))
	{
		DynamicMaterial = Border->GetDynamicMaterial();
	}
	if (!IsValid(DynamicMaterial) || InParameter.IsNone())
	{
		return false;
	}
	TArray<FMaterialParameterInfo> Parameters;
	TArray<FGuid> ParameterIds;
	DynamicMaterial->GetAllScalarParameterInfo(Parameters, ParameterIds);
	const bool bIsScalar = Parameters.ContainsByPredicate([InParameter](const FMaterialParameterInfo& Info)
														  { return Info.Association == EMaterialParameterAssociation::GlobalParameter && Info.Name == InParameter; });
	if (!bIsScalar)
	{
		Parameters.Reset();
		ParameterIds.Reset();
		DynamicMaterial->GetAllVectorParameterInfo(Parameters, ParameterIds);
	}
	const bool bIsVector = !bIsScalar && Parameters.ContainsByPredicate([InParameter](const FMaterialParameterInfo& Info)
																		{ return Info.Association == EMaterialParameterAssociation::GlobalParameter && Info.Name == InParameter; });
	if (!bIsScalar && !bIsVector)
	{
		return false;
	}
	Kind = bIsScalar ? EWidgetTransitionBindingKind::MaterialScalar : EWidgetTransitionBindingKind::MaterialVector;
	MaterialInstance = DynamicMaterial;
	MaterialParameter = InParameter;
	ValueType = bIsScalar ? EWidgetTransitionValueType::Float : EWidgetTransitionValueType::LinearColor;
	ChannelCount = bIsScalar ? 1 : 4;
	bResolved = true;
	return true;
}

void FWidgetTransitionPropertyBinding::Invalidate()
{
	CachedPropertyPath = FDynamicPropertyPath();
	bResolved = false;
	bUsesDouble = false;
	ChannelCount = 0;
	ValueType = EWidgetTransitionValueType::Float;
	Kind = EWidgetTransitionBindingKind::Property;
	MaterialInstance.Reset();
	MaterialParameter = NAME_None;
}

bool FWidgetTransitionPropertyBinding::Apply(UWidget* Widget, const FVector4f& Value) const
{
	if (!bResolved || !IsValid(Widget))
	{
		return false;
	}
	if (Kind == EWidgetTransitionBindingKind::MaterialScalar || Kind == EWidgetTransitionBindingKind::MaterialVector)
	{
		UMaterialInstanceDynamic* DynamicMaterial = MaterialInstance.Get();
		if (!IsValid(DynamicMaterial))
		{
			return false;
		}
		if (Kind == EWidgetTransitionBindingKind::MaterialScalar)
		{
			DynamicMaterial->SetScalarParameterValue(MaterialParameter, Value.X);
		}
		else
		{
			DynamicMaterial->SetVectorParameterValue(MaterialParameter, FLinearColor(Value.X, Value.Y, Value.Z, Value.W));
		}
		return true;
	}
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
	{
		return bUsesDouble ? PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, static_cast<double>(Value.X)) : PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.X);
	}
	case EWidgetTransitionValueType::Vector2D:
	{
		return PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, FVector2D(Value.X, Value.Y));
	}
	case EWidgetTransitionValueType::LinearColor:
	{
		return PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, FLinearColor(Value.X, Value.Y, Value.Z, Value.W));
	}
	default:
	{
		return false;
	}
	}
}

bool FWidgetTransitionPropertyBinding::Read(UWidget* Widget, FVector4f& OutValue) const
{
	if (!bResolved || !IsValid(Widget))
	{
		return false;
	}
	if (Kind == EWidgetTransitionBindingKind::MaterialScalar || Kind == EWidgetTransitionBindingKind::MaterialVector)
	{
		UMaterialInstanceDynamic* DynamicMaterial = MaterialInstance.Get();
		if (!IsValid(DynamicMaterial))
		{
			return false;
		}
		if (Kind == EWidgetTransitionBindingKind::MaterialScalar)
		{
			const float Value = DynamicMaterial->K2_GetScalarParameterValue(MaterialParameter);
			OutValue = FVector4f(Value, Value, Value, Value);
		}
		else
		{
			const FLinearColor Value = DynamicMaterial->K2_GetVectorParameterValue(MaterialParameter);
			OutValue = FVector4f(Value.R, Value.G, Value.B, Value.A);
		}
		return true;
	}
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
	{
		float Value = 0.0f;
		if (!bUsesDouble && !PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, Value))
		{
			return false;
		}
		if (bUsesDouble)
		{
			double DoubleValue = 0.0;
			if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, DoubleValue))
			{
				return false;
			}
			Value = static_cast<float>(DoubleValue);
		}
		OutValue = FVector4f(Value, Value, Value, Value);
		return true;
	}
	case EWidgetTransitionValueType::Vector2D:
	{
		FVector2D Value;
		if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, Value))
		{
			return false;
		}
		OutValue = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		return true;
	}
	case EWidgetTransitionValueType::LinearColor:
	{
		FLinearColor Value;
		if (!PropertyPathHelpers::GetPropertyValue(Widget, CachedPropertyPath, Value))
		{
			return false;
		}
		OutValue = FVector4f(Value.R, Value.G, Value.B, Value.A);
		return true;
	}
	default:
	{
		return false;
	}
	}
}

namespace WidgetTransition
{
	static void StartTransition(const UObject* WorldContextObject, FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks = {})
	{
		const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
		UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
		UWidget* TargetWidget = Transition.Widget.Get();
		if (!Subsystem)
		{
			return;
		}
		Transition.Time = FMath::Max(0.0f, Transition.Time);
		Transition.Delay = FMath::Max(0.0f, Transition.Delay);
		Transition.RepeatCount = FMath::Max(-1, Transition.RepeatCount);
		Transition.bBound = IsValid(TargetWidget) && !Transition.WidgetProperty.IsNone();
		Transition.bHasCallbacks = Callbacks.HasBoundCallbacks();
#if UE_BUILD_SHIPPING
		Transition.EasingCurve = Transition.Easing.IsNull() ? nullptr : Transition.Easing.GetCurve(TEXT("Widget Transition"), false);
#endif
		if (Transition.bBound)
		{
			const bool bResolved = IsMaterialBinding(Transition.WidgetProperty)
									   ? Transition.PropertyBinding.ResolveMaterial(TargetWidget, GetMaterialParameter(Transition.WidgetProperty))
									   : Transition.PropertyBinding.Resolve(TargetWidget, Transition.WidgetProperty.ToString());
			if (!bResolved)
			{
				return;
			}
			if (!WidgetTransition::NormalizeValue(Transition.ToValue, Transition.PropertyBinding.ChannelCount, Transition.ToValue.Channels))
			{
				UE_LOG(LogTemp, Warning, TEXT("Widget Transition: target value type is incompatible with '%s'."), *Transition.WidgetProperty.ToString());
				return;
			}
			if (Transition.bUseFrom)
			{
				if (!WidgetTransition::NormalizeValue(Transition.FromValue, Transition.PropertyBinding.ChannelCount, Transition.FromValue.Channels))
				{
					UE_LOG(LogTemp, Warning, TEXT("Widget Transition: From value type is incompatible with '%s'."), *Transition.WidgetProperty.ToString());
					return;
				}
			}
			else if (!Transition.PropertyBinding.Read(TargetWidget, Transition.FromValue.Channels))
			{
				return;
			}
			if (Transition.Delay > 0.0f && Transition.bUseFrom && !Transition.bChangeFromPropertyAfterDelay)
			{
				Transition.PropertyBinding.Apply(TargetWidget, Transition.FromValue.Channels);
			}
		}
		else
		{
			Transition.PropertyBinding.ChannelCount = Transition.ToValue.Type == EWidgetTransitionValueType::Float ? 1 : Transition.ToValue.Type == EWidgetTransitionValueType::Vector2D ? 2
																																   : 4;
			Transition.FromValue.Channels = Transition.bUseFrom ? Transition.FromValue.Channels : FVector4f::Zero();
		}
		if (!WidgetTransition::IsSpringCompatible(Transition.PropertyBinding.ChannelCount))
		{
			Transition.bUseSpring = false;
		}
		Transition.TransitionId = Subsystem->NextTransitionId++;
		if (Subsystem->NextTransitionId == 0)
		{
			++Subsystem->NextTransitionId;
		}
		WidgetTransition::StartSprings(Transition);
		for (auto It = Subsystem->Transitions.CreateIterator(); Transition.bBound && It; ++It)
		{
			if (It->Widget == TargetWidget && It->WidgetProperty == Transition.WidgetProperty)
			{
				Subsystem->Callbacks.Remove(It->TransitionId);
				It.RemoveCurrent();
			}
		}
		if (Transition.bHasCallbacks)
		{
			Subsystem->Callbacks.Add(Transition.TransitionId, MoveTemp(Callbacks));
		}
		Subsystem->Transitions.Emplace(MoveTemp(Transition));
	}
} // namespace WidgetTransition

void UWidgetTransitionFunctionLibrary::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	WidgetTransition::StartTransition(WorldContextObject, MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::AddWidgetTransitionArray(const UObject* WorldContextObject, TArray<FWidgetTransition> Transitions)
{
	for (FWidgetTransition& Transition : Transitions)
	{
		WidgetTransition::StartTransition(WorldContextObject, MoveTemp(Transition));
	}
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateWidgetTransition(UWidget* Widget, FName WidgetProperty, FWidgetTransitionValue ToValue, float Time, float Delay)
{
	FWidgetTransition Transition;
	Transition.Widget = Widget;
	Transition.WidgetProperty = WidgetProperty;
	Transition.ToValue = MoveTemp(ToValue);
	Transition.Delay = FMath::Max(0.0f, Delay);
	Transition.Time = FMath::Max(0.0f, Time);
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::From(FWidgetTransition Transition, bool bUseFrom, FWidgetTransitionValue FromValue)
{
	Transition.FromValue = MoveTemp(FromValue);
	Transition.bUseFrom = bUseFrom;
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Options(FWidgetTransition Transition, bool bChangeFromPropertyAfterDelay)
{
	Transition.bChangeFromPropertyAfterDelay = bChangeFromPropertyAfterDelay;
	return Transition;
}

FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(float Value)
{
	return WidgetTransition::MakeValue(Value);
}
FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D Value)
{
	return WidgetTransition::MakeValue(Value);
}
FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(FLinearColor Value)
{
	return WidgetTransition::MakeValue(Value);
}
float UWidgetTransitionFunctionLibrary::AsFloat(FWidgetTransitionValue Value)
{
	return Value.Channels.X;
}
FVector2D UWidgetTransitionFunctionLibrary::AsVector2D(FWidgetTransitionValue Value)
{
	return FVector2D(Value.Channels.X, Value.Channels.Y);
}
FLinearColor UWidgetTransitionFunctionLibrary::AsColor(FWidgetTransitionValue Value)
{
	return FLinearColor(Value.Channels.X, Value.Channels.Y, Value.Channels.Z, Value.Channels.W);
}

FWidgetTransition UWidgetTransitionFunctionLibrary::Easing(FWidgetTransition Transition, UCurveTable* CurveTable, FName RowName)
{
	Transition.Easing.CurveTable = CurveTable;
	Transition.Easing.RowName = RowName;
	Transition.bUseSpring = false;
	return Transition;
}
FWidgetTransition UWidgetTransitionFunctionLibrary::Repeat(FWidgetTransition Transition, int32 RepeatCount)
{
	Transition.RepeatCount = FMath::Max(-1, RepeatCount);
	return Transition;
}
FWidgetTransition UWidgetTransitionFunctionLibrary::YoYo(FWidgetTransition Transition, bool bYoYo)
{
	Transition.bYoYo = bYoYo;
	return Transition;
}
FWidgetTransition UWidgetTransitionFunctionLibrary::Spring(FWidgetTransition Transition, float SpringSpeed, float SpringBounce, bool bFitSimulationToTime)
{
	Transition.bUseSpring = true;
	Transition.SpringSpeed = FMath::Clamp(SpringSpeed, 0.0f, 1.0f);
	Transition.SpringBounce = FMath::Clamp(SpringBounce, 0.0f, 1.0f);
	Transition.bFitSpringToTime = bFitSimulationToTime;
	return Transition;
}
FWidgetTransition UWidgetTransitionFunctionLibrary::RemoveFromParent(FWidgetTransition Transition, bool bRemoveFromParent)
{
	Transition.bRemoveFromParent = bRemoveFromParent;
	return Transition;
}

void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (IsValid(Widget) && Subsystem)
	{
		WidgetTransition::ClearTransitionsForWidget(*Subsystem, Widget);
	}
}

UWidgetTransitionAsyncAction* UWidgetTransitionAsyncAction::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	UWidgetTransitionAsyncAction* Action = NewObject<UWidgetTransitionAsyncAction>();
	Action->PendingTransition = MoveTemp(Transition);
	Action->EventTargetValue = Action->PendingTransition.ToValue;
	Action->EventStartValue = Action->PendingTransition.bUseFrom ? Action->PendingTransition.FromValue : FWidgetTransitionValue();
	Action->EventStartValue.Type = Action->EventTargetValue.Type;
	Action->EventValue = Action->EventStartValue;
	Action->WorldContextObject = WorldContextObject;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UWidgetTransitionAsyncAction::Activate()
{
	const UObject* Context = WorldContextObject.Get();
	if (!IsValid(Context))
	{
		Finished.Broadcast(EventValue);
		SetReadyToDestroy();
		return;
	}
	UWidget* Widget = PendingTransition.Widget.Get();
	const bool bHasBinding = IsValid(Widget) && !PendingTransition.WidgetProperty.IsNone();
	const bool bResolved = bHasBinding && (WidgetTransition::IsMaterialBinding(PendingTransition.WidgetProperty)
											   ? EventBinding.ResolveMaterial(Widget, WidgetTransition::GetMaterialParameter(PendingTransition.WidgetProperty))
											   : EventBinding.Resolve(Widget, PendingTransition.WidgetProperty.ToString()));
	if (bResolved)
	{
		EventValue.Type = EventBinding.ValueType;
		EventStartValue.Type = EventBinding.ValueType;
		EventTargetValue.Type = EventBinding.ValueType;
		RefreshEventValue(Widget);
		EventStartValue = EventValue;
	}
	Callbacks.OnStarted.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleStarted);
	Callbacks.OnFinished.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleFinished);
	Callbacks.OnUpdated.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleUpdated);
	WidgetTransition::StartTransition(Context, MoveTemp(PendingTransition), MoveTemp(Callbacks));
}

void UWidgetTransitionAsyncAction::RefreshEventValue(UWidget* Widget)
{
	FVector4f Channels;
	if (EventBinding.Read(Widget, Channels))
	{
		EventValue.Channels = Channels;
	}
}

void UWidgetTransitionAsyncAction::HandleStarted(UWidget* Widget)
{
	RefreshEventValue(Widget);
	Started.Broadcast(EventValue);
}

void UWidgetTransitionAsyncAction::HandleUpdated(UWidget* Widget, float NormalizedProgress, float EasedProgress)
{
	FVector4f Channels;
	if (EventBinding.Read(Widget, Channels))
	{
		EventValue.Channels = Channels;
	}
	else
	{
		EventValue.Channels = FMath::Lerp(EventStartValue.Channels, EventTargetValue.Channels, EasedProgress);
	}
	Updated.Broadcast(EventValue, NormalizedProgress, EasedProgress);
}

void UWidgetTransitionAsyncAction::HandleFinished(UWidget* Widget)
{
	RefreshEventValue(Widget);
	Finished.Broadcast(EventValue);
	SetReadyToDestroy();
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}
void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	WidgetTransition::TickTransitions(*this, DeltaTime);
}

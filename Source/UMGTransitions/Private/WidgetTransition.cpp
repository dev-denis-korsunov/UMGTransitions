#include "WidgetTransition.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PropertyPathHelpers.h"
#include "UObject/UnrealType.h"
#include "WidgetTransitionSubsystem.h"

namespace
{
	constexpr float MinimumEasingY = -2.0f;
	constexpr float MaximumEasingY = 2.0f;

	static FWidgetTransitionValue MakeTransitionValue(float Value)
	{
		FWidgetTransitionValue Result;
		Result.Channels.X = Value;
		Result.Type = EWidgetTransitionValueType::Float;
		return Result;
	}

	static FWidgetTransitionValue MakeTransitionValue(const FVector2D& Value)
	{
		FWidgetTransitionValue Result;
		Result.Channels = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		Result.Type = EWidgetTransitionValueType::Vector2D;
		return Result;
	}

	static FWidgetTransitionValue MakeTransitionValue(const FLinearColor& Value)
	{
		FWidgetTransitionValue Result;
		Result.Channels = FVector4f(Value.R, Value.G, Value.B, Value.A);
		Result.Type = EWidgetTransitionValueType::LinearColor;
		return Result;
	}

	static bool ResolveFastWidgetProperty(FWidgetTransitionPropertyBinding& Binding, const FString& PropertyPath)
	{
		Binding.bUsesDouble = false;
		if (PropertyPath == TEXT("RenderOpacity"))
		{
			Binding.Kind = EWidgetTransitionBindingKind::RenderOpacity;
			Binding.ValueType = EWidgetTransitionValueType::Float;
			Binding.ChannelCount = 1;
			return true;
		}
		if (PropertyPath == TEXT("RenderTransform.Translation"))
		{
			Binding.Kind = EWidgetTransitionBindingKind::RenderTransformTranslation;
		}
		else if (PropertyPath == TEXT("RenderTransform.Scale"))
		{
			Binding.Kind = EWidgetTransitionBindingKind::RenderTransformScale;
		}
		else if (PropertyPath == TEXT("RenderTransform.Shear"))
		{
			Binding.Kind = EWidgetTransitionBindingKind::RenderTransformShear;
		}
		else if (PropertyPath == TEXT("RenderTransform.Angle"))
		{
			Binding.Kind = EWidgetTransitionBindingKind::RenderTransformAngle;
			Binding.ValueType = EWidgetTransitionValueType::Float;
			Binding.ChannelCount = 1;
			return true;
		}
		else if (PropertyPath == TEXT("RenderTransformPivot"))
		{
			Binding.Kind = EWidgetTransitionBindingKind::RenderTransformPivot;
		}
		else
		{
			return false;
		}
		Binding.ValueType = EWidgetTransitionValueType::Vector2D;
		Binding.ChannelCount = 2;
		return true;
	}

} // namespace

void FWidgetTransitionEasing::Clamp()
{
	FirstControlPoint.X = FMath::Clamp(FirstControlPoint.X, 0.0, 1.0);
	SecondControlPoint.X = FMath::Clamp(SecondControlPoint.X, 0.0, 1.0);
	FirstControlPoint.Y = FMath::Clamp(FirstControlPoint.Y, MinimumEasingY, MaximumEasingY);
	SecondControlPoint.Y = FMath::Clamp(SecondControlPoint.Y, MinimumEasingY, MaximumEasingY);
}

float FWidgetTransitionEasing::Evaluate(float Progress) const
{
	const double ClampedProgress = FMath::Clamp(static_cast<double>(Progress), 0.0, 1.0);
	const double FirstX = FMath::Clamp(FirstControlPoint.X, 0.0, 1.0);
	const double SecondX = FMath::Clamp(SecondControlPoint.X, 0.0, 1.0);
	const double FirstY = FMath::Clamp(FirstControlPoint.Y, MinimumEasingY, MaximumEasingY);
	const double SecondY = FMath::Clamp(SecondControlPoint.Y, MinimumEasingY, MaximumEasingY);
	const auto EvaluateAxis = [FirstX, SecondX, FirstY, SecondY](double T, bool bX)
	{
		const double InverseT = 1.0 - T;
		const double First = bX ? FirstX : FirstY;
		const double Second = bX ? SecondX : SecondY;
		return 3.0 * InverseT * InverseT * T * First + 3.0 * InverseT * T * T * Second + T * T * T;
	};

	double Lower = 0.0;
	double Upper = 1.0;
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		const double T = (Lower + Upper) * 0.5;
		if (EvaluateAxis(T, true) < ClampedProgress)
		{
			Lower = T;
		}
		else
		{
			Upper = T;
		}
	}
	return static_cast<float>(EvaluateAxis((Lower + Upper) * 0.5, false));
}

bool FWidgetTransitionPropertyBinding::Resolve(UWidget* InWidget, const FString& InPropertyPath)
{
	Invalidate();
	if (!IsValid(InWidget))
	{
		return false;
	}
	if (ResolveFastWidgetProperty(*this, InPropertyPath))
	{
		bResolved = true;
		return true;
	}
	Kind = EWidgetTransitionBindingKind::Property;
	CachedPropertyPath = FDynamicPropertyPath(InPropertyPath);
	bResolved = CachedPropertyPath.IsValid() && CachedPropertyPath.Resolve(InWidget);
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
	if (bResolved && LeafProperty)
	{
		const UE::FieldNotification::FFieldId FieldId = InWidget->GetFieldNotificationDescriptor().GetField(InWidget->GetClass(), LeafProperty->GetFName());
		if (FieldId.IsValid())
		{
			Kind = EWidgetTransitionBindingKind::PropertyFieldNotify;
			MaterialParameter = FieldId.GetName();
		}
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
	const bool bIsScalar =
		Parameters.ContainsByPredicate([InParameter](const FMaterialParameterInfo& Info) { return Info.Association == EMaterialParameterAssociation::GlobalParameter && Info.Name == InParameter; });
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

bool FWidgetTransitionPropertyBinding::IsFieldNotify() const
{
	return Kind == EWidgetTransitionBindingKind::PropertyFieldNotify;
}

bool FWidgetTransitionPropertyBinding::Apply(UWidget* Widget, const FVector4f& Value, bool bBroadcastFieldNotify) const
{
	if (!bResolved || !IsValid(Widget))
	{
		return false;
	}
	switch (Kind)
	{
	case EWidgetTransitionBindingKind::RenderOpacity:
	{
		Widget->SetRenderOpacity(Value.X);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformTranslation:
	{
		Widget->SetRenderTranslation(FVector2D(Value.X, Value.Y));
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformScale:
	{
		Widget->SetRenderScale(FVector2D(Value.X, Value.Y));
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformShear:
	{
		Widget->SetRenderShear(FVector2D(Value.X, Value.Y));
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformAngle:
	{
		Widget->SetRenderTransformAngle(Value.X);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformPivot:
	{
		Widget->SetRenderTransformPivot(FVector2D(Value.X, Value.Y));
		return true;
	}
	default:
	{
		break;
	}
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
	bool bApplied = false;
	switch (ValueType)
	{
	case EWidgetTransitionValueType::Float:
	{
		bApplied =
			bUsesDouble ? PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, static_cast<double>(Value.X)) : PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.X);
		break;
	}
	case EWidgetTransitionValueType::Vector2D:
	{
		bApplied = PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, FVector2D(Value.X, Value.Y));
		break;
	}
	case EWidgetTransitionValueType::LinearColor:
	{
		bApplied = PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, FLinearColor(Value.X, Value.Y, Value.Z, Value.W));
		break;
	}
	default:
	{
		return false;
	}
	}
	if (bApplied && bBroadcastFieldNotify)
	{
		BroadcastFieldNotify(Widget);
	}
	return bApplied;
}

void FWidgetTransitionPropertyBinding::BroadcastFieldNotify(UWidget* Widget) const
{
	if (Kind != EWidgetTransitionBindingKind::PropertyFieldNotify || !IsValid(Widget))
	{
		return;
	}
	const UE::FieldNotification::FFieldId FieldId = Widget->GetFieldNotificationDescriptor().GetField(Widget->GetClass(), MaterialParameter);
	if (FieldId.IsValid())
	{
		Widget->BroadcastFieldValueChanged(FieldId);
	}
}

bool FWidgetTransitionPropertyBinding::Read(UWidget* Widget, FVector4f& OutValue) const
{
	if (!bResolved || !IsValid(Widget))
	{
		return false;
	}
	switch (Kind)
	{
	case EWidgetTransitionBindingKind::RenderOpacity:
	{
		const float Value = Widget->GetRenderOpacity();
		OutValue = FVector4f(Value, Value, Value, Value);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformTranslation:
	{
		const FVector2D Value = Widget->GetRenderTransform().Translation;
		OutValue = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformScale:
	{
		const FVector2D Value = Widget->GetRenderTransform().Scale;
		OutValue = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformShear:
	{
		const FVector2D Value = Widget->GetRenderTransform().Shear;
		OutValue = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformAngle:
	{
		const float Value = Widget->GetRenderTransformAngle();
		OutValue = FVector4f(Value, Value, Value, Value);
		return true;
	}
	case EWidgetTransitionBindingKind::RenderTransformPivot:
	{
		const FVector2D Value = Widget->GetRenderTransformPivot();
		OutValue = FVector4f(Value.X, Value.Y, 0.0f, 0.0f);
		return true;
	}
	default:
	{
		break;
	}
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

void UWidgetTransitionFunctionLibrary::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr)
	{
		Subsystem->StartTransition(MoveTemp(Transition));
	}
}

void UWidgetTransitionFunctionLibrary::AddWidgetTransitionArray(const UObject* WorldContextObject, TArray<FWidgetTransition> Transitions)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}
	for (FWidgetTransition& Transition : Transitions)
	{
		Subsystem->StartTransition(MoveTemp(Transition));
	}
}

FWidgetTransition UWidgetTransitionFunctionLibrary::CreateWidgetTransition(UWidget* Widget, FName WidgetProperty, FWidgetTransitionValue ToValue, float Time, float Delay, bool bYoYo, int32 RepeatCount, bool bRepeatDelay, EWidgetTransitionAddMode AddMode, EWidgetTransitionColorMix ColorMix, float EventInterval)
{
	FWidgetTransition Transition;
	Transition.Widget = Widget;
	Transition.WidgetProperty = WidgetProperty;
	Transition.ToValue = MoveTemp(ToValue);
	Transition.Delay = FMath::Max(0.0f, Delay);
	Transition.Time = FMath::Max(0.0f, Time);
	Transition.RepeatCount = FMath::Max(-1, RepeatCount);
	Transition.bYoYo = bYoYo;
	Transition.bRepeatDelay = bRepeatDelay;
	Transition.EventInterval = FMath::Max(0.0f, EventInterval);
	Transition.ColorMix = ColorMix;
	Transition.AddMode = AddMode;
	return Transition;
}

FWidgetTransition UWidgetTransitionFunctionLibrary::From(FWidgetTransition Transition, bool bUseFrom, FWidgetTransitionValue FromValue, bool bIgnoreDelay)
{
	Transition.FromValue = MoveTemp(FromValue);
	Transition.bUseFrom = bUseFrom;
	Transition.bIgnoreDelay = bIgnoreDelay;
	return Transition;
}

FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(float Value)
{
	return MakeTransitionValue(Value);
}
FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D Value)
{
	return MakeTransitionValue(Value);
}
FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(FLinearColor Value)
{
	return MakeTransitionValue(Value);
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

FWidgetTransition UWidgetTransitionFunctionLibrary::Easing(FWidgetTransition Transition, FWidgetTransitionEasing Easing)
{
	Easing.Clamp();
	Transition.Easing = MoveTemp(Easing);
	Transition.bUseEasing = true;
	Transition.bUseSpring = false;
	return Transition;
}
FWidgetTransition UWidgetTransitionFunctionLibrary::Spring(FWidgetTransition Transition, float SpringForce, float SpringDamping, float SpringMaxSpeed, bool bFitToTime)
{
	Transition.bUseSpring = true;
	Transition.SpringForce = FMath::Max(1.0f, SpringForce);
	Transition.SpringDamping = FMath::Clamp(SpringDamping, 0.0f, 1.0f);
	Transition.SpringMaxSpeed = FMath::Max(0.0f, SpringMaxSpeed);
	Transition.bFitToTime = bFitToTime;
	return Transition;
}
void UWidgetTransitionFunctionLibrary::ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UWidgetTransitionSubsystem* Subsystem = World ? World->GetSubsystem<UWidgetTransitionSubsystem>() : nullptr;
	if (IsValid(Widget) && Subsystem)
	{
		Subsystem->ClearTransitions(Widget);
	}
}

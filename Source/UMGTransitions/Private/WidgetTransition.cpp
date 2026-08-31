#include "WidgetTransition.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Curves/RealCurve.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PropertyPathHelpers.h"
#include "UObject/UnrealType.h"

namespace WidgetTransitionPrivate
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

	static void RemoveSpring(UWidgetTransitionSubsystem& Subsystem, FWidgetTransition& Transition)
	{
		if (Transition.SpringIndex == INDEX_NONE)
		{
			return;
		}
		const int32 SpringIndex = Transition.SpringIndex;
		const int32 LastSpringIndex = Subsystem.Springs.Num() - 1;
		Subsystem.Springs.RemoveAtSwap(SpringIndex);
		Subsystem.SpringTransitionIndices.RemoveAtSwap(SpringIndex);
		if (SpringIndex < LastSpringIndex)
		{
			Subsystem.Transitions[Subsystem.SpringTransitionIndices[SpringIndex]].SpringIndex = SpringIndex;
		}
		Transition.SpringIndex = INDEX_NONE;
	}

	static void RemoveCallbacks(UWidgetTransitionSubsystem& Subsystem, FWidgetTransition& Transition)
	{
		if (Transition.CallbackIndex == INDEX_NONE)
		{
			return;
		}
		const int32 CallbackIndex = Transition.CallbackIndex;
		const int32 UpdateCallbackIndex = Subsystem.Callbacks[CallbackIndex].UpdateCallbackIndex;
		if (UpdateCallbackIndex != INDEX_NONE)
		{
			const int32 LastUpdateCallbackIndex = Subsystem.UpdateCallbackIndices.Num() - 1;
			Subsystem.UpdateCallbackIndices.RemoveAtSwap(UpdateCallbackIndex);
			if (UpdateCallbackIndex < LastUpdateCallbackIndex)
			{
				Subsystem.Callbacks[Subsystem.UpdateCallbackIndices[UpdateCallbackIndex]].UpdateCallbackIndex = UpdateCallbackIndex;
			}
		}
		const int32 LastCallbackIndex = Subsystem.Callbacks.Num() - 1;
		Subsystem.Callbacks.RemoveAtSwap(CallbackIndex);
		if (CallbackIndex < LastCallbackIndex)
		{
			FWidgetTransitionCallbacks& MovedCallbacks = Subsystem.Callbacks[CallbackIndex];
			if (Subsystem.Transitions.IsValidIndex(MovedCallbacks.TransitionIndex))
			{
				Subsystem.Transitions[MovedCallbacks.TransitionIndex].CallbackIndex = CallbackIndex;
			}
			if (MovedCallbacks.UpdateCallbackIndex != INDEX_NONE)
			{
				Subsystem.UpdateCallbackIndices[MovedCallbacks.UpdateCallbackIndex] = CallbackIndex;
			}
		}
		Transition.CallbackIndex = INDEX_NONE;
	}

	static void RemoveTransition(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex)
	{
		FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
		const int32 LastTransitionIndex = Subsystem.Transitions.Num() - 1;
		RemoveSpring(Subsystem, Transition);
		RemoveCallbacks(Subsystem, Transition);
		if (TransitionIndex < LastTransitionIndex)
		{
			const FWidgetTransition& LastTransition = Subsystem.Transitions.Last();
			if (LastTransition.SpringIndex != INDEX_NONE)
			{
				Subsystem.SpringTransitionIndices[LastTransition.SpringIndex] = TransitionIndex;
			}
		}
		Subsystem.Transitions.RemoveAtSwap(TransitionIndex);
	}

	static void ClearTransitionsForWidget(UWidgetTransitionSubsystem& Subsystem, UWidget* Widget)
	{
		for (int32 TransitionIndex = 0; TransitionIndex < Subsystem.Transitions.Num();)
		{
			if (Subsystem.Transitions[TransitionIndex].Widget == Widget)
			{
				RemoveTransition(Subsystem, TransitionIndex);
				continue;
			}
			++TransitionIndex;
		}
	}

	static bool IsTransitionAtIndex(const UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, uint64 TransitionId)
	{
		return Subsystem.Transitions.IsValidIndex(TransitionIndex) && Subsystem.Transitions[TransitionIndex].TransitionId == TransitionId;
	}

	static bool IsCallbackAtIndex(const UWidgetTransitionSubsystem& Subsystem, int32 CallbackIndex, uint64 TransitionId)
	{
		return Subsystem.Callbacks.IsValidIndex(CallbackIndex) && Subsystem.Callbacks[CallbackIndex].TransitionId == TransitionId && IsTransitionAtIndex(Subsystem, Subsystem.Callbacks[CallbackIndex].TransitionIndex, TransitionId);
	}

	static void StartSpring(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransition& Transition)
	{
		if (!Transition.bUseSpring || !IsSpringCompatible(Transition.PropertyBinding.ChannelCount))
		{
			return;
		}
		const float DampingRatio = FMath::Lerp(1.0f, 0.15f, FMath::Clamp(Transition.SpringBounce, 0.0f, 1.0f));
		// e^(-zeta * omega * Time) <= 0.001: choose omega so the envelope
		// reaches the same relative tolerance used by FWidgetTransitionSpring completion tests.
		const float Frequency = Transition.bFitSpringToTime && Transition.Time > UE_SMALL_NUMBER
									? (-FMath::Loge(0.001f) / (DampingRatio * Transition.Time)) * FMath::Lerp(1.0f, 3.0f, FMath::Clamp(Transition.SpringSpeed, 0.0f, 1.0f))
									: 4.0f * FMath::Pow(6.0f, FMath::Clamp(Transition.SpringSpeed, 0.0f, 1.0f));
		const float SpringFactor = Frequency * Frequency;
		const float DampingFactor = 2.0f * DampingRatio * Frequency;
		if (Transition.SpringIndex == INDEX_NONE)
		{
			Transition.SpringIndex = Subsystem.Springs.Emplace(SpringFactor, DampingFactor);
			Subsystem.SpringTransitionIndices.Add(TransitionIndex);
		}
		FWidgetTransitionSpring& Spring = Subsystem.Springs[Transition.SpringIndex];
		Spring.Start(Transition.FromValue.Channels, Transition.ToValue.Channels, FMath::Max(0.0f, Transition.Delay - Transition.CurrentTime));
	}

	static bool RestartTransition(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransition& Transition)
	{
		if (Transition.RepeatCount == 0)
		{
			return false;
		}
		if (Transition.RepeatCount > 0)
		{
			--Transition.RepeatCount;
		}
		Transition.CurrentTime = Transition.bIgnoreDelayOnRepeat ? Transition.Delay : 0.0f;
		if (Transition.bYoYo)
		{
			Swap(Transition.FromValue, Transition.ToValue);
		}
		StartSpring(Subsystem, TransitionIndex, Transition);
		return true;
	}

	static void DispatchLifecycleEvents(UWidgetTransitionSubsystem& Subsystem, TArray<FWidgetTransitionLifecycleEvent>& Events)
	{
		for (const FWidgetTransitionLifecycleEvent& Event : Events)
		{
			UWidget* Widget = Event.Widget.Get();
			const FOnWidgetTransitionEvent Callback = Event.Callback;
			Callback.ExecuteIfBound(Widget);
			if (Event.bRemoveFromParent && IsValid(Widget))
			{
				Widget->RemoveFromParent();
			}
		}
		Events.Reset();
	}

	static void DispatchFinalUpdatedCallbacks(TArray<FWidgetTransitionUpdateEvent>& Events)
	{
		for (const FWidgetTransitionUpdateEvent& Event : Events)
		{
			const FOnWidgetTransitionUpdate Callback = Event.Callback;
			Callback.ExecuteIfBound(Event.Widget.Get(), Event.NormalizedProgress, Event.EasedProgress);
		}
		Events.Reset();
	}

	static void TickUpdatedCallbacks(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
	{
		for (int32 UpdateCallbackIndex = 0; UpdateCallbackIndex < Subsystem.UpdateCallbackIndices.Num();)
		{
			const int32 CallbackIndex = Subsystem.UpdateCallbackIndices[UpdateCallbackIndex];
			if (!Subsystem.Callbacks.IsValidIndex(CallbackIndex))
			{
				++UpdateCallbackIndex;
				continue;
			}
			FWidgetTransitionCallbacks& CallbackState = Subsystem.Callbacks[CallbackIndex];
			if (!IsTransitionAtIndex(Subsystem, CallbackState.TransitionIndex, CallbackState.TransitionId))
			{
				++UpdateCallbackIndex;
				continue;
			}
			FWidgetTransition& Transition = Subsystem.Transitions[CallbackState.TransitionIndex];
			const bool bDispatchUpdate = Transition.UpdateInterval <= 0.0f || (CallbackState.UpdateElapsed += DeltaTime) >= Transition.UpdateInterval;
			if (bDispatchUpdate)
			{
				CallbackState.UpdateElapsed = Transition.UpdateInterval <= 0.0f ? 0.0f : FMath::Fmod(CallbackState.UpdateElapsed, Transition.UpdateInterval);
				const FOnWidgetTransitionUpdate Updated = CallbackState.OnUpdated;
				UWidget* Widget = Transition.Widget.Get();
				const float NormalizedProgress = CallbackState.NormalizedProgress;
				const float EasedProgress = CallbackState.EasedProgress;
				Updated.ExecuteIfBound(Widget, NormalizedProgress, EasedProgress);
			}
			if (Subsystem.Callbacks.IsValidIndex(CallbackIndex) && Subsystem.Callbacks[CallbackIndex].UpdateCallbackIndex == UpdateCallbackIndex)
			{
				++UpdateCallbackIndex;
			}
		}
	}

	static void TickTransitions(UWidgetTransitionSubsystem& Subsystem, float DeltaTime)
	{
		const float EffectiveDeltaTime = FMath::Clamp(DeltaTime, 0.0f, 1.0f / 20.0f);
		for (FWidgetTransitionSpring& Spring : Subsystem.Springs)
		{
			Spring.Tick(EffectiveDeltaTime);
		}
		for (int32 TransitionIndex = 0; TransitionIndex < Subsystem.Transitions.Num();)
		{
			FWidgetTransition& InitialTransition = Subsystem.Transitions[TransitionIndex];
			if (!InitialTransition.Widget.IsValid())
			{
				RemoveTransition(Subsystem, TransitionIndex);
				continue;
			}
			const uint64 TransitionId = InitialTransition.TransitionId;
			InitialTransition.CurrentTime += EffectiveDeltaTime;
			if (InitialTransition.CurrentTime < InitialTransition.Delay)
			{
				++TransitionIndex;
				continue;
			}
			if (!InitialTransition.bStarted)
			{
				InitialTransition.bStarted = true;
				if (InitialTransition.CallbackIndex != INDEX_NONE && Subsystem.Callbacks.IsValidIndex(InitialTransition.CallbackIndex))
				{
					const FWidgetTransitionCallbacks& CallbackState = Subsystem.Callbacks[InitialTransition.CallbackIndex];
					if (CallbackState.OnStarted.IsBound())
					{
						Subsystem.StartedCallbackEvents.Add({ CallbackState.OnStarted, InitialTransition.Widget, false });
					}
				}
			}
			FWidgetTransition& Transition = Subsystem.Transitions[TransitionIndex];
			bool bEnd = !Transition.bUseSpring && (Transition.Time <= 0.0f || Transition.CurrentTime >= Transition.Delay + Transition.Time);
			const float Alpha = Transition.Time <= 0.0f ? 1.0f : FMath::Clamp((Transition.CurrentTime - Transition.Delay) / Transition.Time, 0.0f, 1.0f);
			float EasedAlpha = Alpha;
			if (Transition.EasingCurve)
			{
				EasedAlpha = Transition.EasingCurve->Eval(Alpha);
			}
			FVector4f Value = FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedAlpha);
			const bool bReachedSpringDeadline = Transition.bUseSpring && Transition.bFitSpringToTime && Transition.CurrentTime >= Transition.Delay + Transition.Time;
			if (Transition.bUseSpring && Subsystem.Springs.IsValidIndex(Transition.SpringIndex) && !bReachedSpringDeadline)
			{
				const FWidgetTransitionSpring& Spring = Subsystem.Springs[Transition.SpringIndex];
				Value = Spring.GetValue();
				bEnd = Spring.IsCompleted();
			}
			if (bReachedSpringDeadline)
			{
				Value = Transition.ToValue.Channels;
				bEnd = true;
			}
			const bool bNeedsRateLimitedUpdate = Transition.bBound && Transition.PropertyBinding.IsFieldNotify();
			bool bDispatchRateLimitedUpdate = false;
			if (bNeedsRateLimitedUpdate)
			{
				bDispatchRateLimitedUpdate = bEnd || Transition.UpdateInterval <= 0.0f || (Transition.UpdateElapsed += EffectiveDeltaTime) >= Transition.UpdateInterval;
				if (bDispatchRateLimitedUpdate)
				{
					Transition.UpdateElapsed = bEnd || Transition.UpdateInterval <= 0.0f ? 0.0f : FMath::Fmod(Transition.UpdateElapsed, Transition.UpdateInterval);
				}
			}
			if (Transition.bBound)
			{
				Transition.PropertyBinding.Apply(Transition.Widget.Get(), Value, bDispatchRateLimitedUpdate);
			}
			if (Transition.CallbackIndex != INDEX_NONE && Subsystem.Callbacks.IsValidIndex(Transition.CallbackIndex))
			{
				FWidgetTransitionCallbacks& CallbackState = Subsystem.Callbacks[Transition.CallbackIndex];
				CallbackState.NormalizedProgress = Alpha;
				CallbackState.EasedProgress = EasedAlpha;
			}
			FWidgetTransition& CurrentTransition = Subsystem.Transitions[TransitionIndex];
			if (bEnd && !RestartTransition(Subsystem, TransitionIndex, CurrentTransition))
			{
				if (CurrentTransition.CallbackIndex != INDEX_NONE)
				{
					const FWidgetTransitionCallbacks& CallbackState = Subsystem.Callbacks[CurrentTransition.CallbackIndex];
					if (CallbackState.OnUpdated.IsBound())
					{
						Subsystem.FinalUpdatedCallbackEvents.Add({ CallbackState.OnUpdated, CurrentTransition.Widget, Alpha, EasedAlpha });
					}
					Subsystem.FinishedCallbackEvents.Add({ CallbackState.OnFinished, CurrentTransition.Widget, CurrentTransition.bRemoveFromParent });
					RemoveTransition(Subsystem, TransitionIndex);
					continue;
				}
				const bool bRemoveFromParent = CurrentTransition.bRemoveFromParent;
				UWidget* Widget = CurrentTransition.Widget.Get();
				RemoveTransition(Subsystem, TransitionIndex);
				if (bRemoveFromParent && IsValid(Widget))
				{
					Widget->RemoveFromParent();
				}
				continue;
			}
			++TransitionIndex;
		}
		DispatchLifecycleEvents(Subsystem, Subsystem.StartedCallbackEvents);
		TickUpdatedCallbacks(Subsystem, EffectiveDeltaTime);
		DispatchFinalUpdatedCallbacks(Subsystem.FinalUpdatedCallbackEvents);
		DispatchLifecycleEvents(Subsystem, Subsystem.FinishedCallbackEvents);
	}
} // namespace WidgetTransitionPrivate

bool FWidgetTransitionPropertyBinding::Resolve(UWidget* InWidget, const FString& InPropertyPath)
{
	Invalidate();
	if (!IsValid(InWidget))
	{
		return false;
	}
	if (WidgetTransitionPrivate::ResolveFastWidgetProperty(*this, InPropertyPath))
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
		bApplied = bUsesDouble ? PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, static_cast<double>(Value.X)) : PropertyPathHelpers::SetPropertyValue(Widget, CachedPropertyPath, Value.X);
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
	if (bApplied && bBroadcastFieldNotify && Kind == EWidgetTransitionBindingKind::PropertyFieldNotify)
	{
		const UE::FieldNotification::FFieldId FieldId = Widget->GetFieldNotificationDescriptor().GetField(Widget->GetClass(), MaterialParameter);
		if (FieldId.IsValid())
		{
			Widget->BroadcastFieldValueChanged(FieldId);
		}
	}
	return bApplied;
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

namespace WidgetTransitionPrivate
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
		if (!IsValid(TargetWidget))
		{
			return;
		}
		Transition.Time = FMath::Max(0.0f, Transition.Time);
		Transition.Delay = FMath::Max(0.0f, Transition.Delay);
		Transition.RepeatCount = FMath::Max(-1, Transition.RepeatCount);
		Transition.UpdateInterval = FMath::Max(0.0f, Transition.UpdateInterval);
		Transition.bBound = IsValid(TargetWidget) && !Transition.WidgetProperty.IsNone();
		Transition.EasingCurve = Transition.Easing.IsNull() ? nullptr : Transition.Easing.GetCurve(TEXT("Widget Transition"), false);
		if (!Transition.Easing.IsNull() && !Transition.EasingCurve)
		{
			UE_LOG(LogTemp, Warning, TEXT("Widget Transition: easing row '%s' could not be resolved; using linear interpolation."), *Transition.Easing.RowName.ToString());
			Transition.Easing = FCurveTableRowHandle();
		}
		if (Transition.bBound)
		{
			const bool bResolved = IsMaterialBinding(Transition.WidgetProperty)
									   ? Transition.PropertyBinding.ResolveMaterial(TargetWidget, GetMaterialParameter(Transition.WidgetProperty))
									   : Transition.PropertyBinding.Resolve(TargetWidget, Transition.WidgetProperty.ToString());
			if (!bResolved)
			{
				return;
			}
			if (!WidgetTransitionPrivate::NormalizeValue(Transition.ToValue, Transition.PropertyBinding.ChannelCount, Transition.ToValue.Channels))
			{
				UE_LOG(LogTemp, Warning, TEXT("Widget Transition: target value type is incompatible with '%s'."), *Transition.WidgetProperty.ToString());
				return;
			}
			if (Transition.bUseFrom)
			{
				if (!WidgetTransitionPrivate::NormalizeValue(Transition.FromValue, Transition.PropertyBinding.ChannelCount, Transition.FromValue.Channels))
				{
					UE_LOG(LogTemp, Warning, TEXT("Widget Transition: From value type is incompatible with '%s'."), *Transition.WidgetProperty.ToString());
					return;
				}
			}
			else if (!Transition.PropertyBinding.Read(TargetWidget, Transition.FromValue.Channels))
			{
				return;
			}
			if (Transition.Delay > 0.0f && Transition.bUseFrom && !Transition.bDeferFromValue)
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
		if (!WidgetTransitionPrivate::IsSpringCompatible(Transition.PropertyBinding.ChannelCount))
		{
			Transition.bUseSpring = false;
		}
		Transition.TransitionId = Subsystem->NextTransitionId++;
		if (Subsystem->NextTransitionId == 0)
		{
			++Subsystem->NextTransitionId;
		}
		for (int32 TransitionIndex = 0; Transition.bBound && TransitionIndex < Subsystem->Transitions.Num();)
		{
			const FWidgetTransition& ExistingTransition = Subsystem->Transitions[TransitionIndex];
			if (ExistingTransition.Widget == TargetWidget && ExistingTransition.WidgetProperty == Transition.WidgetProperty)
			{
				WidgetTransitionPrivate::RemoveTransition(*Subsystem, TransitionIndex);
				continue;
			}
			++TransitionIndex;
		}
		const int32 TransitionIndex = Subsystem->Transitions.Emplace(MoveTemp(Transition));
		if (Callbacks.HasBoundCallbacks())
		{
			Callbacks.TransitionIndex = TransitionIndex;
			Callbacks.TransitionId = Subsystem->Transitions[TransitionIndex].TransitionId;
			Subsystem->Transitions[TransitionIndex].CallbackIndex = Subsystem->Callbacks.Emplace(MoveTemp(Callbacks));
			FWidgetTransitionCallbacks& StoredCallbacks = Subsystem->Callbacks.Last();
			if (StoredCallbacks.OnUpdated.IsBound())
			{
				StoredCallbacks.UpdateCallbackIndex = Subsystem->UpdateCallbackIndices.Add(Subsystem->Transitions[TransitionIndex].CallbackIndex);
			}
		}
		WidgetTransitionPrivate::StartSpring(*Subsystem, TransitionIndex, Subsystem->Transitions[TransitionIndex]);
	}
} // namespace WidgetTransitionPrivate

void UWidgetTransitionFunctionLibrary::AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition)
{
	WidgetTransitionPrivate::StartTransition(WorldContextObject, MoveTemp(Transition));
}

void UWidgetTransitionFunctionLibrary::AddWidgetTransitionArray(const UObject* WorldContextObject, TArray<FWidgetTransition> Transitions)
{
	for (FWidgetTransition& Transition : Transitions)
	{
		WidgetTransitionPrivate::StartTransition(WorldContextObject, MoveTemp(Transition));
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

FWidgetTransition UWidgetTransitionFunctionLibrary::Options(FWidgetTransition Transition, bool bDeferFromValue, bool bIgnoreDelayOnRepeat, float CallbackUpdateInterval)
{
	Transition.bDeferFromValue = bDeferFromValue;
	Transition.bIgnoreDelayOnRepeat = bIgnoreDelayOnRepeat;
	Transition.UpdateInterval = FMath::Max(0.0f, CallbackUpdateInterval);
	return Transition;
}

FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(float Value)
{
	return WidgetTransitionPrivate::MakeValue(Value);
}
FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D Value)
{
	return WidgetTransitionPrivate::MakeValue(Value);
}
FWidgetTransitionValue UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(FLinearColor Value)
{
	return WidgetTransitionPrivate::MakeValue(Value);
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
		WidgetTransitionPrivate::ClearTransitionsForWidget(*Subsystem, Widget);
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
	const bool bResolved = bHasBinding && (WidgetTransitionPrivate::IsMaterialBinding(PendingTransition.WidgetProperty)
										   ? EventBinding.ResolveMaterial(Widget, WidgetTransitionPrivate::GetMaterialParameter(PendingTransition.WidgetProperty))
											   : EventBinding.Resolve(Widget, PendingTransition.WidgetProperty.ToString()));
	if (bResolved)
	{
		EventValue.Type = EventBinding.ValueType;
		EventStartValue.Type = EventBinding.ValueType;
		EventTargetValue.Type = EventBinding.ValueType;
		RefreshEventValue(Widget);
		EventStartValue = EventValue;
	}
	if (Started.IsBound())
	{
		Callbacks.OnStarted.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleStarted);
	}
	bBroadcastUpdateValue = Updated.IsBound();
	if (bBroadcastUpdateValue)
	{
		Callbacks.OnUpdated.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleUpdated);
	}
	// Finished must remain bound even when its execution output is unused so this action can release itself.
	Callbacks.OnFinished.BindDynamic(this, &UWidgetTransitionAsyncAction::HandleFinished);
	WidgetTransitionPrivate::StartTransition(Context, MoveTemp(PendingTransition), MoveTemp(Callbacks));
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
	if (bBroadcastUpdateValue)
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
}

void UWidgetTransitionAsyncAction::HandleFinished(UWidget* Widget)
{
	RefreshEventValue(Widget);
	Finished.Broadcast(EventValue);
	SetReadyToDestroy();
}

#if WITH_DEV_AUTOMATION_TESTS
bool UWidgetTransitionAsyncAction::InitializeUpdateForTesting(FWidgetTransition Transition)
{
	PendingTransition = MoveTemp(Transition);
	EventTargetValue = PendingTransition.ToValue;
	EventStartValue = PendingTransition.bUseFrom ? PendingTransition.FromValue : FWidgetTransitionValue();
	EventStartValue.Type = EventTargetValue.Type;
	EventValue = EventStartValue;
	EventBinding.Invalidate();
	bBroadcastUpdateValue = Updated.IsBound();
	return bBroadcastUpdateValue;
}

void UWidgetTransitionAsyncAction::DispatchUpdatedForTesting(UWidget* Widget, float NormalizedProgress, float EasedProgress)
{
	HandleUpdated(Widget, NormalizedProgress, EasedProgress);
}
#endif

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}
void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	WidgetTransitionPrivate::TickTransitions(*this, DeltaTime);
}

#if WITH_DEV_AUTOMATION_TESTS
void UWidgetTransitionSubsystem::TickTransitionsForTesting(float DeltaTime)
{
	WidgetTransitionPrivate::TickTransitions(*this, DeltaTime);
}

void UWidgetTransitionSubsystem::ClearTransitionsForTesting(UWidget* Widget)
{
	WidgetTransitionPrivate::ClearTransitionsForWidget(*this, Widget);
}
#endif

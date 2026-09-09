#include "WidgetTransitionSubsystem.h"

#include "Components/Widget.h"
#include "Curves/RealCurve.h"

namespace
{
	bool NormalizeValue(const FWidgetTransitionValue& Value, uint8 ChannelCount, FVector4f& OutValue)
	{
		if (ChannelCount == 0 || ChannelCount > 4)
		{
			return false;
		}
		switch (Value.Type)
		{
		case EWidgetTransitionValueType::Float:
		{
			if (ChannelCount < 1)
			{
				return false;
			}
			OutValue = FVector4f(Value.Channels.X, Value.Channels.X, Value.Channels.X, Value.Channels.X);
			return true;
		}
		case EWidgetTransitionValueType::Vector2D:
		{
			if (ChannelCount < 2)
			{
				return false;
			}
			OutValue = FVector4f(Value.Channels.X, Value.Channels.Y, 0.0f, 0.0f);
			return true;
		}
		case EWidgetTransitionValueType::LinearColor:
		{
			if (ChannelCount < 4)
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

	bool IsSpringCompatible(uint8 ChannelCount)
	{
		return ChannelCount >= 1 && ChannelCount <= 4;
	}

	FVector4f InterpolateColorHSV(const FVector4f& From, const FVector4f& To, float Alpha)
	{
		const FLinearColor FromHSV = FLinearColor(From.X, From.Y, From.Z, From.W).LinearRGBToHSV();
		const FLinearColor ToHSV = FLinearColor(To.X, To.Y, To.Z, To.W).LinearRGBToHSV();
		float HueDelta = ToHSV.R - FromHSV.R;
		if (HueDelta > 180.0f)
		{
			HueDelta -= 360.0f;
		}
		else if (HueDelta < -180.0f)
		{
			HueDelta += 360.0f;
		}
		const FLinearColor HSVValue(
			FromHSV.R + HueDelta * Alpha,
			FMath::Lerp(FromHSV.G, ToHSV.G, Alpha),
			FMath::Lerp(FromHSV.B, ToHSV.B, Alpha),
			FMath::Lerp(From.W, To.W, Alpha));
		const FLinearColor RGBValue = HSVValue.HSVToLinearRGB();
		return FVector4f(RGBValue.R, RGBValue.G, RGBValue.B, RGBValue.A);
	}

	float SignedCbrt(float Value)
	{
		return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), 1.0f / 3.0f);
	}

	FVector4f InterpolateColorOKLCH(const FVector4f& From, const FVector4f& To, float Alpha)
	{
		const auto ToOKLab = [](const FVector4f& Color)
		{
			const float L = 0.4122214708f * Color.X + 0.5363325363f * Color.Y + 0.0514459929f * Color.Z;
			const float M = 0.2119034982f * Color.X + 0.6806995451f * Color.Y + 0.1073969566f * Color.Z;
			const float S = 0.0883024619f * Color.X + 0.2817188376f * Color.Y + 0.6299787005f * Color.Z;
			const float LRoot = SignedCbrt(L);
			const float MRoot = SignedCbrt(M);
			const float SRoot = SignedCbrt(S);
			return FVector4f(
				0.2104542553f * LRoot + 0.7936177850f * MRoot - 0.0040720468f * SRoot,
				1.9779984951f * LRoot - 2.4285922050f * MRoot + 0.4505937099f * SRoot,
				0.0259040371f * LRoot + 0.7827717662f * MRoot - 0.8086757660f * SRoot,
				Color.W);
		};
		const auto FromOKLab = [](const FVector4f& Lab)
		{
			const float LRoot = Lab.X + 0.3963377774f * Lab.Y + 0.2158037573f * Lab.Z;
			const float MRoot = Lab.X - 0.1055613458f * Lab.Y - 0.0638541728f * Lab.Z;
			const float SRoot = Lab.X - 0.0894841775f * Lab.Y - 1.2914855480f * Lab.Z;
			const float L = LRoot * LRoot * LRoot;
			const float M = MRoot * MRoot * MRoot;
			const float S = SRoot * SRoot * SRoot;
			return FVector4f(
				4.0767416621f * L - 3.3077115913f * M + 0.2309699292f * S,
				-1.2684380046f * L + 2.6097574011f * M - 0.3413193965f * S,
				-0.0041960863f * L - 0.7034186147f * M + 1.7076147010f * S,
				Lab.W);
		};
		const FVector4f FromLab = ToOKLab(From);
		const FVector4f ToLab = ToOKLab(To);
		const float FromC = FMath::Sqrt(FromLab.Y * FromLab.Y + FromLab.Z * FromLab.Z);
		const float ToC = FMath::Sqrt(ToLab.Y * ToLab.Y + ToLab.Z * ToLab.Z);
		const float FromH = FMath::Atan2(FromLab.Z, FromLab.Y);
		const float ToH = FMath::Atan2(ToLab.Z, ToLab.Y);
		float HueDelta = ToH - FromH;
		if (HueDelta > PI)
		{
			HueDelta -= 2.0f * PI;
		}
		else if (HueDelta < -PI)
		{
			HueDelta += 2.0f * PI;
		}
		const float Chroma = FMath::Lerp(FromC, ToC, Alpha);
		const float Hue = FromH + HueDelta * Alpha;
		const FVector4f InterpolatedLab(
			FMath::Lerp(FromLab.X, ToLab.X, Alpha),
			Chroma * FMath::Cos(Hue),
			Chroma * FMath::Sin(Hue),
			FMath::Lerp(From.W, To.W, Alpha));
		return FromOKLab(InterpolatedLab);
	}

	bool IsMaterialBinding(FName WidgetProperty)
	{
		return WidgetProperty.ToString().StartsWith(TEXT("Material."));
	}

	FName GetMaterialParameter(FName WidgetProperty)
	{
		return FName(*WidgetProperty.ToString().RightChop(9));
	}
}

ETickableTickType UWidgetTransitionSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

void UWidgetTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickTransitions(DeltaTime);
}

bool UWidgetTransitionSubsystem::StartTransition(FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks)
{
	UWidget* TargetWidget = Transition.Widget.Get();
	if (!IsValid(TargetWidget))
	{
		return false;
	}
	Transition.Time = FMath::Max(0.0f, Transition.Time);
	Transition.Delay = FMath::Max(0.0f, Transition.Delay);
	Transition.RepeatCount = FMath::Max(-1, Transition.RepeatCount);
	Transition.UpdateInterval = FMath::Max(0.0f, Transition.UpdateInterval);
	Transition.bBound = !Transition.WidgetProperty.IsNone();
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
			UE_LOG(LogTemp, Warning, TEXT("Widget Transition: could not resolve property '%s' on widget '%s'. For material bindings, the widget must be an Image or Border with a material parameter."), *Transition.WidgetProperty.ToString(), *GetNameSafe(TargetWidget));
			return false;
		}
		if (!NormalizeValue(Transition.ToValue, Transition.PropertyBinding.ChannelCount, Transition.ToValue.Channels))
		{
			UE_LOG(LogTemp, Warning, TEXT("Widget Transition: target value type is incompatible with '%s'."), *Transition.WidgetProperty.ToString());
			return false;
		}
		if (Transition.bUseFrom)
		{
			if (!NormalizeValue(Transition.FromValue, Transition.PropertyBinding.ChannelCount, Transition.FromValue.Channels))
			{
				UE_LOG(LogTemp, Warning, TEXT("Widget Transition: From value type is incompatible with '%s'."), *Transition.WidgetProperty.ToString());
				return false;
			}
		}
		else if (!Transition.PropertyBinding.Read(TargetWidget, Transition.FromValue.Channels))
		{
			return false;
		}
		if (Transition.bUseFrom && !Transition.bApplyFromAfterDelay)
		{
			Transition.PropertyBinding.Apply(TargetWidget, Transition.FromValue.Channels);
		}
	}
	else
	{
		Transition.PropertyBinding.ChannelCount = Transition.ToValue.Type == EWidgetTransitionValueType::Float ? 1 : Transition.ToValue.Type == EWidgetTransitionValueType::Vector2D ? 2 : 4;
		Transition.FromValue.Channels = Transition.bUseFrom ? Transition.FromValue.Channels : FVector4f::Zero();
	}
	if (!IsSpringCompatible(Transition.PropertyBinding.ChannelCount))
	{
		Transition.bUseSpring = false;
	}
	FVector4f HandoffVelocity = FVector4f::Zero();
	bool bHasHandoffVelocity = false;
	for (int32 TransitionIndex = 0; Transition.bBound && TransitionIndex < Transitions.Num();)
	{
		const FWidgetTransition& ExistingTransition = Transitions[TransitionIndex];
		if (ExistingTransition.Widget == TargetWidget && ExistingTransition.WidgetProperty == Transition.WidgetProperty)
		{
			if (ExistingTransition.SpringIndex != INDEX_NONE && Springs.IsValidIndex(ExistingTransition.SpringIndex))
			{
				HandoffVelocity = Springs[ExistingTransition.SpringIndex].GetVelocity();
				bHasHandoffVelocity = true;
			}
			UE_LOG(LogTemp, Verbose, TEXT("Widget Transition: replacing existing transition for '%s' on widget '%s'."), *Transition.WidgetProperty.ToString(), *GetNameSafe(TargetWidget));
			RequestTransitionRemoval(TransitionIndex);
			if (!bDeferringTransitionRemovals)
			{
				continue;
			}
		}
		++TransitionIndex;
	}
	const int32 TransitionIndex = Transitions.Emplace(MoveTemp(Transition));
	CallbackStore.Register(*this, TransitionIndex, MoveTemp(Callbacks));
	StartSpring(TransitionIndex, Transitions[TransitionIndex], bHasHandoffVelocity ? HandoffVelocity : FVector4f::Zero());
	return true;
}

void UWidgetTransitionSubsystem::ClearTransitions(UWidget* Widget)
{
	for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num();)
	{
		if (Transitions[TransitionIndex].Widget == Widget)
		{
			RequestTransitionRemoval(TransitionIndex);
			if (!bDeferringTransitionRemovals)
			{
				continue;
			}
		}
		++TransitionIndex;
	}
}

FWidgetTransitionSample UWidgetTransitionSubsystem::SampleTransition(const FWidgetTransition& Transition) const
{
	FWidgetTransitionSample Sample;
	Sample.bCompleted = !Transition.bUseSpring && (Transition.Time <= 0.0f || Transition.CurrentTime >= Transition.Delay + Transition.Time);
	const float NormalizedProgress = Transition.Time <= 0.0f ? 1.0f : FMath::Clamp((Transition.CurrentTime - Transition.Delay) / Transition.Time, 0.0f, 1.0f);
	const float EasedProgress = Transition.EasingCurve ? Transition.EasingCurve->Eval(NormalizedProgress) : NormalizedProgress;
	if (Transition.ToValue.Type != EWidgetTransitionValueType::LinearColor)
	{
		Sample.Value = FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedProgress);
	}
	else
	{
		switch (Transition.ColorInterpolation)
		{
		case EWidgetTransitionColorInterpolation::HSV:
		{
			Sample.Value = InterpolateColorHSV(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedProgress);
			break;
		}
		case EWidgetTransitionColorInterpolation::OKLCH:
		{
			Sample.Value = InterpolateColorOKLCH(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedProgress);
			break;
		}
		default:
		{
			Sample.Value = FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedProgress);
			break;
		}
		}
	}
	const bool bReachedSpringDeadline = Transition.bUseSpring && Transition.bFitSpringToTime && Transition.CurrentTime >= Transition.Delay + Transition.Time;
	if (Transition.bUseSpring && Springs.IsValidIndex(Transition.SpringIndex) && !bReachedSpringDeadline)
	{
		const FWidgetTransitionSpring& Spring = Springs[Transition.SpringIndex];
		Sample.Value = Spring.GetValue();
		Sample.bCompleted = Spring.IsCompleted();
	}
	if (bReachedSpringDeadline)
	{
		Sample.Value = Transition.ToValue.Channels;
		Sample.bCompleted = true;
	}
	return Sample;
}

void UWidgetTransitionSubsystem::RemoveSpring(FWidgetTransition& Transition)
{
	if (Transition.SpringIndex == INDEX_NONE)
	{
		return;
	}
	const int32 SpringIndex = Transition.SpringIndex;
	const int32 LastSpringIndex = Springs.Num() - 1;
	Springs.RemoveAtSwap(SpringIndex);
	SpringTransitionIndices.RemoveAtSwap(SpringIndex);
	if (SpringIndex < LastSpringIndex)
	{
		Transitions[SpringTransitionIndices[SpringIndex]].SpringIndex = SpringIndex;
	}
	Transition.SpringIndex = INDEX_NONE;
}

void UWidgetTransitionSubsystem::RemoveTransition(int32 TransitionIndex)
{
	FWidgetTransition& Transition = Transitions[TransitionIndex];
	const int32 LastTransitionIndex = Transitions.Num() - 1;
	RemoveSpring(Transition);
	CallbackStore.Remove(*this, TransitionIndex);
	if (TransitionIndex < LastTransitionIndex)
	{
		const FWidgetTransition& LastTransition = Transitions.Last();
		if (LastTransition.SpringIndex != INDEX_NONE)
		{
			SpringTransitionIndices[LastTransition.SpringIndex] = TransitionIndex;
		}
	}
	Transitions.RemoveAtSwap(TransitionIndex);
}

void UWidgetTransitionSubsystem::RequestTransitionRemoval(int32 TransitionIndex)
{
	if (bDeferringTransitionRemovals)
	{
		PendingRemovalIndices.AddUnique(TransitionIndex);
		return;
	}
	RemoveTransition(TransitionIndex);
}

void UWidgetTransitionSubsystem::FlushPendingRemovals()
{
	PendingRemovalIndices.Sort([](int32 Left, int32 Right)
	{
		return Left > Right;
	});
	int32 PreviousIndex = INDEX_NONE;
	for (const int32 TransitionIndex : PendingRemovalIndices)
	{
		if (TransitionIndex != PreviousIndex && Transitions.IsValidIndex(TransitionIndex))
		{
			RemoveTransition(TransitionIndex);
		}
		PreviousIndex = TransitionIndex;
	}
	PendingRemovalIndices.Reset();
}

void UWidgetTransitionSubsystem::StartSpring(int32 TransitionIndex, FWidgetTransition& Transition, FVector4f InitialVelocity)
{
	if (!Transition.bUseSpring || !IsSpringCompatible(Transition.PropertyBinding.ChannelCount))
	{
		return;
	}
	const float DampingRatio = FMath::Lerp(0.15f, 1.0f, FMath::Clamp(Transition.SpringDamping, 0.0f, 1.0f));
	// e^(-zeta * omega * Time) <= 0.001: choose omega so the envelope
	// reaches the same relative tolerance used by FWidgetTransitionSpring completion tests.
	const float Frequency = Transition.bFitSpringToTime && Transition.Time > UE_SMALL_NUMBER
								? (-FMath::Loge(0.001f) / (DampingRatio * Transition.Time)) * FMath::Lerp(1.0f, 3.0f, FMath::Clamp(Transition.SpringForce, 0.0f, 1.0f))
								: 4.0f * FMath::Pow(6.0f, FMath::Clamp(Transition.SpringForce, 0.0f, 1.0f));
	const float SpringFactor = Frequency * Frequency;
	const float DampingFactor = 2.0f * DampingRatio * Frequency;
	if (Transition.SpringIndex == INDEX_NONE)
	{
		Transition.SpringIndex = Springs.Emplace(SpringFactor, DampingFactor, Transition.SpringMaxSpeed);
		SpringTransitionIndices.Add(TransitionIndex);
	}
	FWidgetTransitionSpring& Spring = Springs[Transition.SpringIndex];
	Spring.Start(Transition.FromValue.Channels, Transition.ToValue.Channels, FMath::Max(0.0f, Transition.Delay - Transition.CurrentTime), InitialVelocity);
}

bool UWidgetTransitionSubsystem::RestartTransition(int32 TransitionIndex, FWidgetTransition& Transition)
{
	if (Transition.bYoYo && !Transition.bYoYoReverse)
	{
		Transition.bYoYoReverse = true;
		Swap(Transition.FromValue, Transition.ToValue);
		Transition.CurrentTime = Transition.Delay;
		StartSpring(TransitionIndex, Transition);
		return true;
	}
	if (Transition.bYoYoReverse)
	{
		Transition.bYoYoReverse = false;
		Swap(Transition.FromValue, Transition.ToValue);
	}
	if (Transition.RepeatCount == 0)
	{
		return false;
	}
	if (Transition.RepeatCount > 0)
	{
		--Transition.RepeatCount;
	}
	Transition.CurrentTime = Transition.bIgnoreDelayOnRepeat ? Transition.Delay : 0.0f;
	StartSpring(TransitionIndex, Transition);
	return true;
}

void UWidgetTransitionSubsystem::TickTransitions(float DeltaTime)
{
	bDeferringTransitionRemovals = true;
	CallbackStore.EnsureLinks(*this);
	const float EffectiveDeltaTime = FMath::Clamp(DeltaTime, 0.0f, 1.0f / 20.0f);
	for (int32 SpringIndex = 0; SpringIndex < Springs.Num(); ++SpringIndex)
	{
		FWidgetTransitionSpring& Spring = Springs[SpringIndex];
		const int32 TransitionIndex = SpringTransitionIndices.IsValidIndex(SpringIndex) ? SpringTransitionIndices[SpringIndex] : INDEX_NONE;
		if (Transitions.IsValidIndex(TransitionIndex) && !Spring.GetTarget().Equals(Transitions[TransitionIndex].ToValue.Channels, 0.0f))
		{
			Spring.SetTarget(Transitions[TransitionIndex].ToValue.Channels);
		}
		Spring.Tick(EffectiveDeltaTime);
	}
	for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num();)
	{
		FWidgetTransition& InitialTransition = Transitions[TransitionIndex];
		if (!InitialTransition.Widget.IsValid())
		{
			RequestTransitionRemoval(TransitionIndex);
			++TransitionIndex;
			continue;
		}
		InitialTransition.CurrentTime += EffectiveDeltaTime;
		if (InitialTransition.CurrentTime < InitialTransition.Delay)
		{
			++TransitionIndex;
			continue;
		}
		if (!InitialTransition.bStarted)
		{
			InitialTransition.bStarted = true;
			CallbackStore.QueueStarted(*this, TransitionIndex);
		}
		FWidgetTransition& Transition = Transitions[TransitionIndex];
		const FWidgetTransitionSample Sample = SampleTransition(Transition);
		const bool bFieldNotify = Transition.bBound && Transition.PropertyBinding.IsFieldNotify();
		if (Transition.bBound && !bFieldNotify)
		{
			Transition.PropertyBinding.Apply(Transition.Widget.Get(), Sample.Value);
		}
		FWidgetTransition& CurrentTransition = Transitions[TransitionIndex];
		if (Sample.bCompleted)
		{
			FWidgetTransitionValue SampleValue;
			SampleValue.Channels = Sample.Value;
			SampleValue.Type = CurrentTransition.ToValue.Type;
			if (RestartTransition(TransitionIndex, CurrentTransition))
			{
				CallbackStore.StoreOverrideValue(*this, TransitionIndex, MoveTemp(SampleValue));
				++TransitionIndex;
				continue;
			}
			if (bFieldNotify && CurrentTransition.PropertyBinding.Apply(CurrentTransition.Widget.Get(), Sample.Value, false))
			{
				CurrentTransition.PropertyBinding.BroadcastFieldNotify(CurrentTransition.Widget.Get());
			}
			CallbackStore.QueueCompleted(*this, TransitionIndex, MoveTemp(SampleValue));
			RequestTransitionRemoval(TransitionIndex);
			++TransitionIndex;
			continue;
		}
		++TransitionIndex;
	}
	CallbackStore.TickAndDispatch(*this, EffectiveDeltaTime);
	bDeferringTransitionRemovals = false;
	FlushPendingRemovals();
}

#if WITH_DEV_AUTOMATION_TESTS
void UWidgetTransitionSubsystem::AddTransitionForTesting(FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks)
{
	const int32 TransitionIndex = Transitions.Add(MoveTemp(Transition));
	CallbackStore.Register(*this, TransitionIndex, MoveTemp(Callbacks));
}

void UWidgetTransitionSubsystem::TickTransitionsForTesting(float DeltaTime)
{
	TickTransitions(DeltaTime);
}

void UWidgetTransitionSubsystem::ClearTransitionsForTesting(UWidget* Widget)
{
	ClearTransitions(Widget);
}
#endif

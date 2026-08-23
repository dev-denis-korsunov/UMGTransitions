#include "WidgetTransition.h"

namespace
{
	static float LengthSquared(const FVector4f& Value)
	{
		return Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z + Value.W * Value.W;
	}
}

FWidgetTransitionSpring::FWidgetTransitionSpring(float InSpringFactor, float InDampingFactor)
	: SpringFactor(InSpringFactor)
	, Frequency(FMath::Sqrt(InSpringFactor))
	, DampingRatio(InDampingFactor / (2.0f * Frequency))
	, DampedFrequency(DampingRatio < 1.0f ? Frequency * FMath::Sqrt(1.0f - DampingRatio * DampingRatio) : 0.0f)
	, bStarted(false)
	, bCompleted(false)
{
}

void FWidgetTransitionSpring::Start(FVector4f InStartValue, FVector4f InTargetValue)
{
	CurrentValue = InStartValue;
	TargetValue = InTargetValue;
	Velocity = FVector4f::Zero();
	CompletionThresholdSquared = FMath::Max(0.00000001f, LengthSquared(TargetValue - CurrentValue) * 0.000001f);
	bStarted = true;
	bCompleted = false;
}

void FWidgetTransitionSpring::Tick(float DeltaTime)
{
	if (!bStarted || bCompleted)
	{
		return;
	}
	const FVector4f Offset = CurrentValue - TargetValue;
	if (DampingRatio < 1.0f)
	{
		const float Exponential = FMath::Exp(-DampingRatio * Frequency * DeltaTime);
		const float Cosine = FMath::Cos(DampedFrequency * DeltaTime);
		const float Sine = FMath::Sin(DampedFrequency * DeltaTime);
		const FVector4f Coefficient = (Velocity + DampingRatio * Frequency * Offset) / DampedFrequency;
		const FVector4f NewOffset = Exponential * (Offset * Cosine + Coefficient * Sine);
		Velocity = Exponential * (-DampingRatio * Frequency * (Offset * Cosine + Coefficient * Sine) - Offset * DampedFrequency * Sine + Coefficient * DampedFrequency * Cosine);
		CurrentValue = TargetValue + NewOffset;
	}
	else
	{
		const float Exponential = FMath::Exp(-Frequency * DeltaTime);
		const FVector4f Coefficient = Velocity + Frequency * Offset;
		CurrentValue = TargetValue + Exponential * (Offset + Coefficient * DeltaTime);
		Velocity = Exponential * (Velocity - Frequency * Coefficient * DeltaTime);
	}
	bCompleted = LengthSquared(CurrentValue - TargetValue) <= CompletionThresholdSquared && LengthSquared(Velocity) <= CompletionThresholdSquared * SpringFactor;
	if (bCompleted)
	{
		CurrentValue = TargetValue;
	}
}
